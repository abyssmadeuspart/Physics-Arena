import csv
import statistics

from .common import (
    FIXTURE_VALIDATION_CURRENT_RUN,
    FIXTURE_VALIDATION_HISTORICAL_OR_CURRENT,
    OPTIONAL_NORMALIZED_COLUMNS,
    REQUIRED_NORMALIZED_COLUMNS,
    apply_normalized_metadata,
    emit,
    load_json,
    metadata_path_text,
    repo_path,
    validate_fixture_collection,
    validate_fixture_row,
)
from .host import validate_host_metadata
from .release_files import validate_result_artifact_snapshot, validate_result_visual_artifact_snapshot

def validate_normalized_rows(rows, case, engines, repeats, case_fixture_contract_by_id, fixture_validation_mode=FIXTURE_VALIDATION_CURRENT_RUN, engine_sources=None):
    expected_rows = len(engines) * len(case["thread_counts"]) * repeats
    if len(rows) != expected_rows:
        emit("normalize", "invalid_result", f"row_count={len(rows)} expected={expected_rows}")
        return 2
    if engine_sources is None:
        engine_sources = {}
    thread_support_by_engine_id = normalized_thread_support_by_engine_id(engines, engine_sources)
    fixture_contract = case_fixture_contract_by_id.get(case["id"], {})
    collection_status = validate_fixture_collection(rows, case, engines, fixture_contract)
    if collection_status["status"] != "ok":
        emit("normalize", collection_status["status"], collection_status["detail"])
        return 2
    expected_engine_ids = [engine["id"] for engine in engines]
    expected_thread_counts = [int(thread_count) for thread_count in case["thread_counts"]]
    expected_keys = {
        (engine_id, thread_count, repeat_index)
        for engine_id in expected_engine_ids
        for thread_count in expected_thread_counts
        for repeat_index in range(repeats)
    }
    observed_keys = set()
    allowed_benchmark_modes = {case["benchmark_mode"], "headless_api"}
    for row in sorted(rows, key=normalized_row_key):
        if row["case_id"] != case["id"] or row["benchmark_mode"] not in allowed_benchmark_modes:
            emit("normalize", "invalid_result", f"case_or_mode engine={row['engine_id']}")
            return 2
        engine_id = row["engine_id"]
        try:
            thread_count = int(row["thread_count"])
            repeat_index = int(row["repeat_index"])
        except ValueError:
            emit("normalize", "invalid_result", f"selection_key engine={engine_id}")
            return 2
        if engine_id not in expected_engine_ids:
            emit("normalize", "invalid_result", f"unexpected_engine={engine_id}")
            return 2
        if thread_count not in expected_thread_counts:
            emit("normalize", "invalid_result", f"unexpected_thread_count={thread_count} engine={engine_id}")
            return 2
        if repeat_index < 0 or repeat_index >= repeats:
            emit("normalize", "invalid_result", f"repeat_index={repeat_index} engine={engine_id}")
            return 2
        key = (engine_id, thread_count, repeat_index)
        if key in observed_keys:
            emit("normalize", "invalid_result", f"duplicate_row engine={engine_id} thread_count={thread_count} repeat_index={repeat_index}")
            return 2
        observed_keys.add(key)
        worker_status = validate_thread_worker_count(row, thread_count, thread_support_by_engine_id.get(engine_id, {}))
        if worker_status["status"] != "ok":
            emit("normalize", worker_status["status"], worker_status["detail"])
            return 2
        fixture_status = validate_fixture_row(row, case, fixture_validation_mode, fixture_contract)
        if fixture_status["status"] != "ok":
            emit("normalize", fixture_status["status"], fixture_status["detail"])
            return 2
        for counter in case["stability_counters"]:
            if int(row[counter]) != 0:
                emit("normalize", "invalid_result", f"{counter}={row[counter]} engine={row['engine_id']}")
                return 2
        if row["case_status"] != "ok" or row["metric_status"] != "ok":
            emit("normalize", "invalid_result", f"status engine={row['engine_id']}")
            return 2
    if observed_keys != expected_keys:
        missing = sorted(expected_keys.difference(observed_keys))
        extra = sorted(observed_keys.difference(expected_keys))
        emit("normalize", "invalid_result", f"selection_keys missing={len(missing)} extra={len(extra)}")
        return 2
    return 0

def normalized_thread_support_by_engine_id(engines, engine_sources):
    support_by_engine_id = {}
    for engine in engines:
        engine_id = engine["id"]
        support_by_engine_id[engine_id] = engine_sources.get(engine_id, {}).get("thread_support", {})
    return support_by_engine_id

def validate_thread_worker_count(row, thread_count, thread_support):
    engine_id = row["engine_id"]
    if not thread_support:
        return {"status": "invalid_result", "detail": f"worker_contract_missing engine={engine_id}"}
    try:
        requested_thread_count = int(row["requested_thread_count"])
        effective_thread_count = int(row["effective_thread_count"])
        requested_worker_count = int(row["requested_worker_count"])
        effective_worker_count = int(row["effective_worker_count"])
    except (KeyError, ValueError):
        return {"status": "invalid_result", "detail": f"worker_count_parse engine={engine_id}"}
    requested_policy = thread_support.get("requested_worker_count", "")
    effective_policy = thread_support.get("effective_worker_count", "")
    expected_requested_worker_count = expected_worker_count_for_policy(requested_policy, thread_count)
    expected_effective_worker_count = expected_worker_count_for_policy(effective_policy, thread_count)
    if expected_requested_worker_count < 0:
        return {"status": "invalid_result", "detail": f"requested_worker_policy={requested_policy} engine={engine_id}"}
    if expected_effective_worker_count < 0:
        return {"status": "invalid_result", "detail": f"effective_worker_policy={effective_policy} engine={engine_id}"}
    if requested_thread_count != thread_count:
        return {"status": "invalid_result", "detail": f"requested_thread_count={requested_thread_count} expected={thread_count} engine={engine_id}"}
    if effective_thread_count != thread_count:
        return {"status": "invalid_result", "detail": f"effective_thread_count={effective_thread_count} expected={thread_count} engine={engine_id}"}
    if requested_worker_count != expected_requested_worker_count:
        return {"status": "invalid_result", "detail": f"requested_worker_count={requested_worker_count} expected={expected_requested_worker_count} engine={engine_id}"}
    if effective_worker_count != expected_effective_worker_count:
        return {"status": "invalid_result", "detail": f"effective_worker_count={effective_worker_count} expected={expected_effective_worker_count} engine={engine_id}"}
    expected_main_thread_participates = thread_support.get("main_thread_participates", "")
    if expected_main_thread_participates not in {"yes", "no"}:
        return {"status": "invalid_result", "detail": f"main_thread_participates_policy={expected_main_thread_participates} engine={engine_id}"}
    if row.get("main_thread_participates", "") != expected_main_thread_participates:
        return {"status": "invalid_result", "detail": f"main_thread_participates={row.get('main_thread_participates', '')} expected={expected_main_thread_participates} engine={engine_id}"}
    if engine_id == "unreal_chaos":
        return validate_unreal_chaos_taskgraph_worker_count(row, thread_count)
    return {"status": "ok", "detail": engine_id}

def expected_worker_count_for_policy(policy, thread_count):
    if isinstance(policy, int):
        return policy
    if policy == "thread_count":
        return thread_count
    if policy == "thread_count_minus_one":
        return thread_count - 1 if thread_count > 1 else 0
    return -1

def validate_unreal_chaos_taskgraph_worker_count(row, thread_count):
    expected_taskgraph_worker_count = thread_count - 1 if thread_count > 1 else 1
    requested_taskgraph_text = row.get("requested_taskgraph_worker_count", "")
    if requested_taskgraph_text:
        try:
            requested_taskgraph_worker_count = int(requested_taskgraph_text)
        except ValueError:
            return {"status": "invalid_result", "detail": "requested_taskgraph_worker_count_parse engine=unreal_chaos"}
        if requested_taskgraph_worker_count != expected_taskgraph_worker_count:
            return {"status": "invalid_result", "detail": f"requested_taskgraph_worker_count={requested_taskgraph_worker_count} expected={expected_taskgraph_worker_count} engine=unreal_chaos"}
    actual_taskgraph_text = row.get("actual_taskgraph_worker_count", "")
    if actual_taskgraph_text:
        try:
            actual_taskgraph_worker_count = int(actual_taskgraph_text)
        except ValueError:
            return {"status": "invalid_result", "detail": "actual_taskgraph_worker_count_parse engine=unreal_chaos"}
        if actual_taskgraph_worker_count < 0:
            return {"status": "invalid_result", "detail": f"actual_taskgraph_worker_count={actual_taskgraph_worker_count} engine=unreal_chaos"}
        if thread_count > 1 and actual_taskgraph_worker_count != expected_taskgraph_worker_count:
            return {"status": "invalid_result", "detail": f"actual_taskgraph_worker_count={actual_taskgraph_worker_count} expected={expected_taskgraph_worker_count} engine=unreal_chaos"}
    return {"status": "ok", "detail": "unreal_chaos"}

def normalized_row_key(row):
    return (row["engine_id"], int(row["thread_count"]), int(row["repeat_index"]))

def write_summary(result_dir, engine_report_metadata_by_id, build_settings_rewrite_by_value):
    normalized_path = result_dir / "normalized.csv"
    summary_path = result_dir / "summary.csv"
    rows_status = read_summary_source_rows(normalized_path, engine_report_metadata_by_id, build_settings_rewrite_by_value)
    if rows_status["status"] != "ok":
        emit("summary", rows_status["status"], rows_status["detail"])
        return 2
    return write_summary_rows(summary_path, result_dir.name, rows_status["rows"])

def read_summary_source_rows(normalized_path, engine_report_metadata_by_id, build_settings_rewrite_by_value):
    with normalized_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            return {"status": "invalid_result", "detail": f"missing_header={normalized_path}", "rows": []}
        for column in ["run_id"] + REQUIRED_NORMALIZED_COLUMNS + ["raw_path"]:
            if column not in reader.fieldnames:
                return {"status": "invalid_result", "detail": f"missing_column={column} file={normalized_path}", "rows": []}
        rows = [dict(row) for row in reader]
    return {"status": "ok", "detail": str(normalized_path), "rows": rows}

def write_summary_rows(summary_path, run_id, rows):
    if not rows:
        emit("summary", "invalid_result", f"empty_normalized={summary_path}")
        return 2
    groups = {}
    for row in rows:
        key = (row["engine_id"], int(row["thread_count"]))
        groups.setdefault(key, []).append(row)
    fieldnames = [
        "run_id", "engine_id", "case_id", "benchmark_mode", "thread_count", "repeat_count",
        "warmup_steps", "physics_settings", "build_settings", "min_ms_per_step",
        "mean_ms_per_step", "median_ms_per_step", "max_steps_per_second",
        "mean_steps_per_second", "body_count", "shape_count", "invalid_transform_count",
        "below_floor_count", "out_of_bounds_count"
    ]
    with summary_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        summary_records = []
        for key in sorted(groups):
            group_rows = groups[key]
            ms_values = [float(row["ms_per_step"]) for row in group_rows]
            step_values = [float(row["steps_per_second"]) for row in group_rows]
            first = group_rows[0]
            summary_records.append({
                "run_id": run_id,
                "engine_id": key[0],
                "case_id": first["case_id"],
                "benchmark_mode": first["benchmark_mode"],
                "thread_count": key[1],
                "repeat_count": len(group_rows),
                "warmup_steps": first["warmup_steps"],
                "physics_settings": first["physics_settings"],
                "build_settings": first["build_settings"],
                "min_ms_per_step": f"{min(ms_values):.9f}",
                "mean_ms_per_step": f"{statistics.mean(ms_values):.9f}",
                "median_ms_per_step": f"{statistics.median(ms_values):.9f}",
                "max_steps_per_second": f"{max(step_values):.9f}",
                "mean_steps_per_second": f"{statistics.mean(step_values):.9f}",
                "body_count": first["body_count"],
                "shape_count": first["shape_count"],
                "invalid_transform_count": first["invalid_transform_count"],
                "below_floor_count": first["below_floor_count"],
                "out_of_bounds_count": first["out_of_bounds_count"]
            })
        for row in sorted(summary_records, key=lambda row: (
            int(row["thread_count"]),
            float(row["median_ms_per_step"]),
            row["engine_id"]
        )):
            writer.writerow(row)
    return 0

def collect_combined_result(contracts, result_dir, source_dirs):
    source_records = []
    expected_contract = None
    engine_ids = []
    source_run_ids = []
    source_result_dirs = []
    source_manifests = []
    rows = []
    fieldname_set = set(["run_id"] + REQUIRED_NORMALIZED_COLUMNS + OPTIONAL_NORMALIZED_COLUMNS + ["raw_path"])

    for source_dir in source_dirs:
        source_status = load_source_result_record(contracts, source_dir)
        if source_status["status"] != "ok":
            return source_status
        source_record = source_status["record"]
        source_contract = source_record["contract"]
        if expected_contract is None:
            expected_contract = source_contract
        else:
            mismatch = first_contract_mismatch(expected_contract, source_contract)
            if mismatch:
                return {"status": "invalid_result", "detail": f"source_contract_mismatch key={mismatch} source={source_dir}"}

        for engine_entry in source_record["manifest"].get("engines", []):
            engine_id = engine_entry.get("id", "")
            if engine_id in engine_ids:
                return {"status": "invalid_result", "detail": f"duplicate_engine_id={engine_id} source={source_dir}"}
            engine_ids.append(engine_id)

        source_run_id = source_record["manifest"]["run_id"]
        if source_run_id in source_run_ids:
            return {"status": "invalid_result", "detail": f"duplicate_source_run_id={source_run_id}"}
        source_run_ids.append(source_run_id)
        source_result_dirs.append(metadata_path_text(source_dir))
        source_manifests.append(metadata_path_text(source_dir / "manifest.json"))
        source_records.append(source_record)
        for fieldname in source_record["fieldnames"]:
            fieldname_set.add(fieldname)
        rows.extend(source_record["rows"])

    if expected_contract is None:
        return {"status": "invalid_result", "detail": "missing_source_results"}

    case = contracts["case_by_id"].get(expected_contract["case_id"])
    if case is None:
        return {"status": "invalid_result", "detail": f"case={expected_contract['case_id']}"}
    combined_engines = [contracts["engine_by_id"][engine_id] for engine_id in engine_ids]
    validate_status = validate_normalized_rows(
        rows,
        case,
        combined_engines,
        expected_contract["repeat_count"],
        contracts["case_fixture_contract_by_id"],
        FIXTURE_VALIDATION_HISTORICAL_OR_CURRENT,
        contracts["engine_sources"])
    if validate_status != 0:
        return {"status": "invalid_result", "detail": "combined_rows_failed_validation"}

    ordered_fieldnames = ["run_id"] + REQUIRED_NORMALIZED_COLUMNS + OPTIONAL_NORMALIZED_COLUMNS + ["raw_path"]
    extra_fieldnames = sorted(fieldname_set.difference(ordered_fieldnames))
    fieldnames = ordered_fieldnames + extra_fieldnames
    normalized_rows = []
    for row in sorted(rows, key=normalized_row_key):
        apply_normalized_metadata(row, contracts["engine_report_metadata_by_id"], contracts["build_settings_rewrite_by_value"])
        normalized_rows.append({fieldname: row.get(fieldname, "") for fieldname in fieldnames})

    manifest = {
        "schema_version": 1,
        "run_id": result_dir.name,
        "host_route": expected_contract["host_route"],
        "host": expected_contract["host"],
        "case_id": expected_contract["case_id"],
        "benchmark_mode": expected_contract["benchmark_mode"],
        "thread_counts": expected_contract["thread_counts"],
        "step_count": expected_contract["step_count"],
        "warmup_steps": expected_contract["warmup_steps"],
        "repeat_count": expected_contract["repeat_count"],
        "engines": [
            engine_entry
            for source_record in source_records
            for engine_entry in source_record["manifest"].get("engines", [])
        ],
        "source_run_ids": source_run_ids,
        "source_result_dirs": source_result_dirs,
        "source_manifests": source_manifests
    }
    return {"status": "ok", "detail": result_dir.name, "manifest": manifest, "rows": normalized_rows, "fieldnames": fieldnames}

def load_source_result_record(contracts, source_dir):
    results_root = repo_path("results").resolve()
    resolved_source = source_dir.resolve()
    try:
        resolved_source.relative_to(results_root)
    except ValueError:
        return {"status": "invalid_result", "detail": f"source_outside_results={source_dir}"}
    if not source_dir.is_dir():
        return {"status": "invalid_result", "detail": f"missing_source_dir={source_dir}"}

    manifest_path = source_dir / "manifest.json"
    normalized_path = source_dir / "normalized.csv"
    if not manifest_path.is_file():
        return {"status": "invalid_result", "detail": f"missing_manifest={manifest_path}"}
    if not normalized_path.is_file():
        return {"status": "invalid_result", "detail": f"missing_normalized={normalized_path}"}

    manifest = load_json(manifest_path)
    manifest_status = validate_source_manifest_contract(contracts, manifest, source_dir)
    if manifest_status["status"] != "ok":
        return manifest_status
    row_status = load_and_validate_source_rows(contracts, manifest, normalized_path)
    if row_status["status"] != "ok":
        return row_status
    return {
        "status": "ok",
        "detail": str(source_dir),
        "record": {
            "manifest": manifest,
            "contract": manifest_status["contract"],
            "rows": row_status["rows"],
            "fieldnames": row_status["fieldnames"]
        }
    }

def validate_source_manifest_contract(contracts, manifest, source_dir):
    required_keys = [
        "run_id",
        "host_route",
        "host",
        "case_id",
        "benchmark_mode",
        "thread_counts",
        "step_count",
        "warmup_steps",
        "repeat_count",
        "engines"
    ]
    for key in required_keys:
        if key not in manifest:
            return {"status": "invalid_result", "detail": f"missing_manifest_key={key} source={source_dir}"}
    if manifest["case_id"] not in contracts["case_by_id"]:
        return {"status": "invalid_result", "detail": f"case={manifest['case_id']} source={source_dir}"}
    host_status = validate_host_metadata(manifest["host"])
    if host_status["status"] != "ok":
        return {"status": host_status["status"], "detail": f"{host_status['detail']} source={source_dir}"}
    if not isinstance(manifest["engines"], list) or not manifest["engines"]:
        return {"status": "invalid_result", "detail": f"missing_manifest_engines source={source_dir}"}
    engine_ids = []
    for engine_entry in manifest["engines"]:
        engine_id = engine_entry.get("id", "")
        if engine_id not in contracts["engine_by_id"]:
            return {"status": "invalid_result", "detail": f"engine={engine_id} source={source_dir}"}
        if engine_id in engine_ids:
            return {"status": "invalid_result", "detail": f"duplicate_manifest_engine={engine_id} source={source_dir}"}
        artifact_status = validate_result_artifact_snapshot(contracts, engine_entry, source_dir)
        if artifact_status["status"] != "ok":
            return artifact_status
        engine_ids.append(engine_id)
    visual_artifact_status = validate_result_visual_artifact_snapshot(contracts, manifest, source_dir)
    if visual_artifact_status["status"] != "ok":
        return visual_artifact_status
    contract = {
        "host_route": manifest["host_route"],
        "host": manifest["host"],
        "case_id": manifest["case_id"],
        "benchmark_mode": manifest["benchmark_mode"],
        "thread_counts": manifest["thread_counts"],
        "step_count": manifest["step_count"],
        "warmup_steps": manifest["warmup_steps"],
        "repeat_count": manifest["repeat_count"]
    }
    return {"status": "ok", "detail": manifest["run_id"], "contract": contract}

def load_and_validate_source_rows(contracts, manifest, normalized_path):
    with normalized_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            return {"status": "invalid_result", "detail": f"missing_header={normalized_path}"}
        for column in ["run_id"] + REQUIRED_NORMALIZED_COLUMNS + ["raw_path"]:
            if column not in reader.fieldnames:
                return {"status": "invalid_result", "detail": f"missing_column={column} file={normalized_path}"}
        rows = [
            apply_normalized_metadata(dict(row), contracts["engine_report_metadata_by_id"], contracts["build_settings_rewrite_by_value"])
            for row in reader
        ]
    if not rows:
        return {"status": "invalid_result", "detail": f"empty_normalized={normalized_path}"}

    case = contracts["case_by_id"][manifest["case_id"]]
    engine_ids = [engine_entry["id"] for engine_entry in manifest["engines"]]
    thread_counts = [int(thread_count) for thread_count in manifest["thread_counts"]]
    repeat_count = int(manifest["repeat_count"])
    expected_keys = {
        (engine_id, thread_count, repeat_index)
        for engine_id in engine_ids
        for thread_count in thread_counts
        for repeat_index in range(repeat_count)
    }
    observed_keys = set()
    for row in rows:
        row_status = validate_combined_source_row(row, manifest, case, engine_ids, thread_counts, repeat_count, normalized_path, contracts["case_fixture_contract_by_id"].get(case["id"], {}))
        if row_status["status"] != "ok":
            return row_status
        key = (row["engine_id"], int(row["thread_count"]), int(row["repeat_index"]))
        if key in observed_keys:
            return {"status": "invalid_result", "detail": f"duplicate_row engine={key[0]} thread={key[1]} repeat={key[2]} file={normalized_path}"}
        observed_keys.add(key)
    if observed_keys != expected_keys:
        missing = sorted(expected_keys.difference(observed_keys))
        extra = sorted(observed_keys.difference(expected_keys))
        return {"status": "invalid_result", "detail": f"row_key_mismatch missing={missing[:3]} extra={extra[:3]} file={normalized_path}"}
    return {"status": "ok", "detail": str(normalized_path), "rows": rows, "fieldnames": reader.fieldnames}

def validate_combined_source_row(row, manifest, case, engine_ids, thread_counts, repeat_count, normalized_path, fixture_contract):
    if row["engine_id"] not in engine_ids:
        return {"status": "invalid_result", "detail": f"unexpected_engine={row['engine_id']} file={normalized_path}"}
    if row["case_id"] != manifest["case_id"] or row["case_id"] != case["id"]:
        return {"status": "invalid_result", "detail": f"case_id={row['case_id']} file={normalized_path}"}
    if row["benchmark_mode"] != manifest["benchmark_mode"]:
        return {"status": "invalid_result", "detail": f"benchmark_mode={row['benchmark_mode']} file={normalized_path}"}
    int_status = parse_row_ints(row, ["thread_count", "step_count", "warmup_steps", "repeat_index", "body_count", "shape_count"] + case["stability_counters"])
    if int_status["status"] != "ok":
        return {"status": "invalid_result", "detail": f"{int_status['detail']} file={normalized_path}"}
    if int(row["thread_count"]) not in thread_counts:
        return {"status": "invalid_result", "detail": f"thread_count={row['thread_count']} file={normalized_path}"}
    if int(row["step_count"]) != int(manifest["step_count"]) or int(row["step_count"]) != int(case["step_count"]):
        return {"status": "invalid_result", "detail": f"step_count={row['step_count']} file={normalized_path}"}
    if int(row["warmup_steps"]) != int(manifest["warmup_steps"]) or int(row["warmup_steps"]) != int(case.get("warmup_steps", 0)):
        return {"status": "invalid_result", "detail": f"warmup_steps={row['warmup_steps']} file={normalized_path}"}
    if int(row["repeat_index"]) < 0 or int(row["repeat_index"]) >= repeat_count:
        return {"status": "invalid_result", "detail": f"repeat_index={row['repeat_index']} file={normalized_path}"}
    fixture_status = validate_fixture_row(row, case, FIXTURE_VALIDATION_HISTORICAL_OR_CURRENT, fixture_contract)
    if fixture_status["status"] != "ok":
        return {"status": fixture_status["status"], "detail": f"{fixture_status['detail']} file={normalized_path}"}
    for counter in case["stability_counters"]:
        if int(row[counter]) != 0:
            return {"status": "invalid_result", "detail": f"{counter}={row[counter]} engine={row['engine_id']} file={normalized_path}"}
    if row["case_status"] != "ok" or row["metric_status"] != "ok":
        return {"status": "invalid_result", "detail": f"status engine={row['engine_id']} file={normalized_path}"}
    return {"status": "ok", "detail": row["engine_id"]}

def parse_row_ints(row, columns):
    for column in columns:
        try:
            int(row[column])
        except ValueError:
            return {"status": "invalid_result", "detail": f"invalid_int column={column} value={row[column]}"}
    return {"status": "ok", "detail": "parsed"}

def first_contract_mismatch(expected_contract, source_contract):
    for key in ["host_route", "case_id", "benchmark_mode", "thread_counts", "step_count", "warmup_steps", "repeat_count"]:
        if expected_contract[key] != source_contract[key]:
            return key
    return ""
