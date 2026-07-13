import csv
import json
import math
import os
import re
import shutil
import time
from pathlib import Path

from .common import (
    COMPACT_RAW_RESULT_COLUMNS,
    DEFAULT_RELEASE_PLATFORM,
    OPTIONAL_NORMALIZED_COLUMNS,
    REQUIRED_NORMALIZED_COLUMNS,
    REPO_ROOT,
    RUNNABLE_ROUTE_STATUSES,
    apply_normalized_metadata,
    emit,
    metadata_path_text,
    platform_id,
    process_diagnostic_detail,
    repo_path,
    run_process,
    write_process_logs,
    write_text_lf,
)
from .options import parse_positive_int, parse_value_options, single_option_value
from .reports import write_report_artifacts, write_summary_svg
from .release_files import (
    artifact_run_metadata,
    load_runtime_records,
    release_run_environment,
)
from .result_contracts import (
    expected_worker_count_for_policy,
    validate_normalized_rows,
    write_summary_rows,
)
from .contracts import load_contracts, selected_engines, validate_case_route_support
from .host import collect_host_metadata, int_value, run_state_slug

def command_run(args):
    contracts = load_contracts()
    if not args:
        emit("run_arguments", "invalid_result", "missing_case_slug")
        return 2
    case_slug = args[0]
    case = contracts["case_by_slug"].get(case_slug)
    if case is None:
        emit("run_arguments", "invalid_result", f"case={case_slug}")
        return 2
    remaining = args[1:]
    options_status = parse_value_options(remaining, {
        "--engine",
        "--engine-set",
        "--engines",
        "--max-thread-count",
        "--platform",
        "--repeats",
        "--thread-count",
        "--threads"
    })
    if options_status["status"] != "ok":
        emit("run_arguments", options_status["status"], options_status["detail"])
        return 2
    if options_status["positionals"]:
        emit("run_arguments", "invalid_result", "unexpected_args=" + ",".join(options_status["positionals"]))
        return 2
    engine_set_status = single_option_value(options_status, "--engine-set", "default_release")
    if engine_set_status["status"] != "ok":
        emit("run_arguments", engine_set_status["status"], engine_set_status["detail"])
        return 2
    engine_set = engine_set_status["value"]
    engine_ids_status = parse_engine_ids(options_status)
    if engine_ids_status["status"] != "ok":
        emit("run_arguments", engine_ids_status["status"], engine_ids_status["detail"])
        return 2
    engines = selected_engines(contracts, engine_set, "", engine_ids_status["items"])
    case_support_status = validate_case_route_support(contracts, engines, case["id"], "headless")
    if case_support_status["status"] != "ok":
        emit("run_arguments", case_support_status["status"], case_support_status["detail"])
        return 2
    thread_request_status = parse_thread_selection_request(options_status, case)
    if thread_request_status["status"] != "ok":
        emit("run_arguments", thread_request_status["status"], thread_request_status["detail"])
        return 2
    release_platform_status = single_option_value(options_status, "--platform", DEFAULT_RELEASE_PLATFORM)
    if release_platform_status["status"] != "ok":
        emit("run_arguments", release_platform_status["status"], release_platform_status["detail"])
        return 2
    release_platform = release_platform_status["value"]
    repeats_value_status = single_option_value(options_status, "--repeats", str(default_repeat_count(case)))
    if repeats_value_status["status"] != "ok":
        emit("run_arguments", repeats_value_status["status"], repeats_value_status["detail"])
        return 2
    repeats_status = parse_positive_int(repeats_value_status["value"])
    if repeats_status["status"] != "ok":
        emit("run_arguments", "invalid_result", f"repeats={repeats_status['detail']}")
        return 2
    repeats = repeats_status["value"]
    runtime_records = load_runtime_records(contracts, engines, release_platform)
    if runtime_records["status"] != "ok":
        for failure in runtime_records["failures"]:
            emit(failure["component"], failure["status"], failure["detail"])
        failed_components = "/".join(failure["component"] for failure in runtime_records["failures"])
        emit("run_gate", runtime_records["status"], f"release_artifacts_failed platform={release_platform} failure_count={runtime_records['failure_count']} components={failed_components}")
        emit("run_action", "required", "restore the matching checked-in release/windows-x64 package and rerun")
        return 2
    host_status = collect_host_metadata()
    if host_status["status"] != "ok":
        emit("run_host", host_status["status"], host_status["detail"])
        return 2
    thread_counts_status = resolve_thread_selection(thread_request_status, case, host_status["host"])
    if thread_counts_status["status"] != "ok":
        emit("run_arguments", thread_counts_status["status"], thread_counts_status["detail"])
        return 2
    engine_thread_status = validate_engine_thread_support(contracts, engines, thread_counts_status["thread_counts"])
    if engine_thread_status["status"] != "ok":
        emit("run_arguments", engine_thread_status["status"], engine_thread_status["detail"])
        return 2
    emit("run_thread_selection", "ok", thread_selection_diagnostic(thread_counts_status["selection"]))
    run_case = case_with_thread_counts(case, thread_counts_status["thread_counts"])
    run_slug_status = select_run_slug(run_case, host_status["cpu_slug"], host_status["host"], thread_counts_status["selection"], repeats)
    if run_slug_status["status"] != "ok":
        emit("run_arguments", run_slug_status["status"], run_slug_status["detail"])
        return 2
    result_dir = run_slug_status["result_dir"]
    raw_dir = result_dir / "raw"
    raw_dir.mkdir(parents=True)
    emit("run_component", "status", "detail")
    manifest_record = write_run_manifest(
        result_dir,
        contracts,
        engines,
        run_case,
        repeats,
        runtime_records["records"],
        host_status["host"],
        thread_counts_status["selection"],
        run_slug_status["slug_metadata"]
    )
    exit_code = 0
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        run_status = adapter.get("run", {}).get("route_status", engine["support_status"])
        if engine["support_status"] not in RUNNABLE_ROUTE_STATUSES or run_status not in RUNNABLE_ROUTE_STATUSES:
            emit(f"route_{engine['id']}", engine["support_status"], f"run_status={run_status}")
            exit_code = 2
            continue
        result = run_engine(result_dir, raw_dir, engine, adapter, run_case, repeats, runtime_records["records"].get(engine["id"]))
        if result != 0:
            exit_code = 2
    if exit_code != 0:
        emit("run_gate", "run_failed", str(raw_dir.relative_to(REPO_ROOT)))
        return exit_code
    normalize_status = normalize_results(
        result_dir,
        run_case,
        engines,
        repeats,
        contracts["engine_report_metadata_by_id"],
        contracts["build_settings_rewrite_by_value"],
        contracts["case_fixture_contract_by_id"],
        contracts["engine_sources"],
        manifest_record["document"],
        runtime_records["records"])
    if normalize_status["status"] != "ok":
        emit("run_gate", "invalid_result", str((result_dir / "normalized.csv").relative_to(REPO_ROOT)))
        return 2
    finalization_status = finalize_fresh_result(
        result_dir, contracts, manifest_record["document"], normalize_status["rows"])
    if finalization_status["status"] != "ok":
        emit("run_report", "invalid_result", str(result_dir.relative_to(REPO_ROOT)))
        return 2
    emit("run_gate", "ok", str((result_dir / "normalized.csv").relative_to(REPO_ROOT)))
    emit("run_summary", "ok", str((result_dir / "summary.csv").relative_to(REPO_ROOT)))
    emit("run_svg", "ok", str((result_dir / "summary.svg").relative_to(REPO_ROOT)))
    emit("run_report", "ok", str((result_dir / "report.md").relative_to(REPO_ROOT)))
    emit("run_manifest", "ok", str(manifest_record["path"].relative_to(REPO_ROOT)))
    emit("run_result", "ok", str(result_dir.relative_to(REPO_ROOT)))
    return 0

def finalize_fresh_result(result_dir, contracts, manifest, normalized_rows):
    summary_status = write_summary_rows(result_dir / "summary.csv", result_dir.name, normalized_rows)
    if summary_status["status"] != "ok":
        return summary_status
    report = next((
        report
        for report in contracts["reports_doc"]["reports"]
        if report["case_id"] == manifest["case_id"]
    ), None)
    if report is None:
        status = {
            "status": "invalid_result",
            "detail": f"case_id={manifest['case_id']}",
        }
        emit("slice_summary", status["status"], status["detail"])
        return status
    slice_status = write_raw_slice_summaries(
        result_dir, contracts, manifest, normalized_rows, report)
    if slice_status["status"] != "ok":
        return slice_status
    return write_report_artifacts(
        result_dir, contracts, report, manifest, summary_status["rows"])

def write_raw_slice_summaries(result_dir, contracts, manifest, normalized_rows, report):
    grouped_rows = {}
    for row in normalized_rows:
        key = (row["engine_id"], int(row["thread_count"]))
        grouped_rows.setdefault(key, []).append(row)
    for engine_id, thread_count in sorted(grouped_rows):
        slice_dir = result_dir / "raw" / engine_id / f"t{thread_count}"
        slice_dir.mkdir(parents=True, exist_ok=True)
        summary_csv = slice_dir / f"{engine_id}_t{thread_count}_summary.csv"
        summary_svg = slice_dir / f"{engine_id}_t{thread_count}_summary.svg"
        summary_status = write_summary_rows(
            summary_csv, result_dir.name, grouped_rows[(engine_id, thread_count)])
        if summary_status["status"] != "ok":
            emit("run_slice_summary", summary_status["status"], summary_status["detail"])
            return summary_status
        write_summary_svg(
            summary_svg, summary_status["rows"], contracts, report, manifest)
        emit("run_slice_summary", "ok", metadata_path_text(summary_csv))
        emit("run_slice_svg", "ok", metadata_path_text(summary_svg))
    return {
        "status": "ok",
        "detail": f"slice_count={len(grouped_rows)}",
        "slice_count": len(grouped_rows),
    }

def parse_engine_ids(options):
    items = []
    for entry in options["entries"]:
        if entry["name"] == "--engine":
            item_status = parse_single_item(entry["value"], "--engine")
            if item_status["status"] != "ok":
                return {"status": item_status["status"], "detail": item_status["detail"], "items": []}
            items.append(item_status["item"])
        elif entry["name"] == "--engines":
            items_status = parse_csv_items(entry["value"], "--engines")
            if items_status["status"] != "ok":
                return {"status": items_status["status"], "detail": items_status["detail"], "items": []}
            items.extend(items_status["items"])
    return {"status": "ok", "detail": ",".join(items), "items": items}

def parse_thread_selection_request(options, case):
    exact_values = []
    max_values = []
    for entry in options["entries"]:
        if entry["name"] == "--thread-count":
            exact_values.append(entry["value"])
        elif entry["name"] == "--threads":
            items_status = parse_csv_items(entry["value"], "--threads")
            if items_status["status"] != "ok":
                return thread_selection_error(items_status["status"], items_status["detail"])
            exact_values.extend(items_status["items"])
        elif entry["name"] == "--max-thread-count":
            max_values.append(entry["value"])
    if len(max_values) > 1:
        return thread_selection_error("invalid_result", "duplicate_option=--max-thread-count")
    if max_values and exact_values:
        return thread_selection_error("invalid_result", "mixed_thread_selectors=--max-thread-count")
    if max_values:
        parsed_status = parse_positive_int(max_values[0])
        if parsed_status["status"] != "ok":
            return thread_selection_error("invalid_result", f"max_thread_count={max_values[0]}")
        return {
            "status": "ok",
            "detail": f"mode=max requested_max={parsed_status['value']}",
            "mode": "max",
            "requested_max": parsed_status["value"],
            "thread_counts": [],
            "default_thread_counts": list(case["thread_counts"])
        }
    if not exact_values:
        return {
            "status": "ok",
            "detail": "mode=default",
            "mode": "default",
            "requested_max": 0,
            "thread_counts": [],
            "default_thread_counts": list(case["thread_counts"])
        }
    selected_counts = []
    seen_counts = set()
    for item in exact_values:
        parsed_status = parse_positive_int(item)
        if parsed_status["status"] != "ok":
            return thread_selection_error("invalid_result", f"thread_count={item}")
        thread_count = parsed_status["value"]
        if thread_count in seen_counts:
            return thread_selection_error("invalid_result", f"duplicate_thread_count={thread_count}")
        seen_counts.add(thread_count)
        selected_counts.append(thread_count)
    return {
        "status": "ok",
        "detail": "mode=explicit",
        "mode": "explicit",
        "requested_max": 0,
        "thread_counts": selected_counts,
        "default_thread_counts": list(case["thread_counts"])
    }

def thread_selection_error(status, detail):
    return {
        "status": status,
        "detail": detail,
        "mode": "",
        "requested_max": 0,
        "thread_counts": [],
        "default_thread_counts": []
    }

def resolved_thread_counts_for_mode(configured_thread_counts, upper_bound, selection_mode):
    configured_counts = sorted({
        int(thread_count)
        for thread_count in configured_thread_counts
        if int(thread_count) > 0
    })
    if not configured_counts:
        return {"thread_counts": [], "generated_thread_counts": []}
    selected_counts = {thread_count for thread_count in configured_counts if thread_count <= upper_bound}
    generated_counts = set()
    largest_configured_count = max(configured_counts)
    if upper_bound > largest_configured_count:
        if selection_mode == "max":
            generated_power = 1
            while generated_power <= largest_configured_count:
                generated_power *= 2
            while generated_power <= upper_bound:
                generated_counts.add(generated_power)
                generated_power *= 2
        generated_counts.add(upper_bound)
    selected_with_generated_counts = selected_counts.union(
        thread_count
        for thread_count in generated_counts
        if thread_count > 0 and thread_count <= upper_bound
    )
    generated_selected_counts = sorted(
        thread_count
        for thread_count in generated_counts
        if thread_count in selected_with_generated_counts and thread_count not in selected_counts
    )
    return {
        "thread_counts": sorted(selected_with_generated_counts),
        "generated_thread_counts": generated_selected_counts
    }

def resolve_thread_selection(request, case, host):
    host_threads = int_value(host.get("cpu", {}).get("logical_threads", 0))
    if host_threads <= 0:
        return thread_selection_error("invalid_result", "host_logical_threads=missing_or_non_positive")
    mode = request["mode"]
    if mode == "max":
        requested_max = request["requested_max"]
        effective_max = min(requested_max, host_threads)
        default_counts = [int(thread_count) for thread_count in case["thread_counts"]]
        selected_counts = resolved_thread_counts_for_mode(default_counts, effective_max, mode)
        thread_counts = selected_counts["thread_counts"]
        if not thread_counts:
            return thread_selection_error("invalid_result", f"max_thread_counts_empty requested_max={requested_max} effective_max={effective_max} host_logical_threads={host_threads}")
        return resolved_thread_selection(mode, thread_counts, host_threads, requested_max, effective_max, [], [], selected_counts["generated_thread_counts"])
    if mode == "explicit":
        for thread_count in request["thread_counts"]:
            if thread_count > host_threads:
                return thread_selection_error("unsupported_thread_count", f"thread_count={thread_count} host_logical_threads={host_threads}")
        return resolved_thread_selection(mode, request["thread_counts"], host_threads, 0, 0, request["thread_counts"], [], [])
    default_counts = [int(thread_count) for thread_count in case["thread_counts"]]
    selected_counts = resolved_thread_counts_for_mode(default_counts, host_threads, mode)
    thread_counts = selected_counts["thread_counts"]
    dropped_counts = [thread_count for thread_count in default_counts if thread_count > host_threads]
    if not thread_counts:
        return thread_selection_error("invalid_result", f"default_thread_counts_empty host_logical_threads={host_threads}")
    return resolved_thread_selection(mode, thread_counts, host_threads, 0, 0, default_counts, dropped_counts, selected_counts["generated_thread_counts"])

def resolved_thread_selection(mode, thread_counts, host_threads, requested_max, effective_max, requested_thread_counts, dropped_thread_counts, generated_thread_counts):
    selection = {
        "mode": mode,
        "host_logical_threads": host_threads,
        "thread_counts": list(thread_counts),
        "requested_max": requested_max,
        "effective_max": effective_max,
        "requested_thread_counts": list(requested_thread_counts),
        "dropped_thread_counts": list(dropped_thread_counts),
        "generated_thread_counts": list(generated_thread_counts)
    }
    return {
        "status": "ok",
        "detail": thread_selection_diagnostic(selection),
        "mode": mode,
        "requested_max": requested_max,
        "thread_counts": list(thread_counts),
        "selection": selection,
        "default_thread_counts": []
    }

def default_repeat_count(case):
    repeat_presets = case.get("repeat_presets", {})
    for preset_name in ["full", "qualification"]:
        repeat_count = repeat_presets.get(preset_name)
        if isinstance(repeat_count, int) and repeat_count > 0:
            return repeat_count
    for repeat_count in repeat_presets.values():
        if isinstance(repeat_count, int) and repeat_count > 0:
            return repeat_count
    return 1

def validate_engine_thread_support(contracts, engines, thread_counts):
    requested_counts = [int(thread_count) for thread_count in thread_counts]
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        thread_support = adapter.get("thread_support", {})
        if not thread_support:
            continue
        mode = thread_support.get("mode", "")
        if mode == "host_bounded":
            continue
        supported_counts = [int(thread_count) for thread_count in thread_support.get("supported_thread_counts", [])]
        unsupported_counts = [thread_count for thread_count in requested_counts if thread_count not in supported_counts]
        if unsupported_counts:
            return {
                "status": "unsupported_thread_count",
                "detail": f"engine={engine['id']} thread_count={'/'.join(str(item) for item in unsupported_counts)} supported={'/'.join(str(item) for item in supported_counts)}",
            }
    return {"status": "ok", "detail": "engine_thread_support"}

def thread_selection_diagnostic(selection):
    parts = [
        f"mode={selection['mode']}",
        f"host_logical_threads={selection['host_logical_threads']}",
        "thread_counts=" + "/".join(str(thread_count) for thread_count in selection["thread_counts"])
    ]
    if selection["mode"] == "max":
        parts.append(f"requested_max={selection['requested_max']}")
        parts.append(f"effective_max={selection['effective_max']}")
    if selection["generated_thread_counts"]:
        parts.append("generated=" + "/".join(str(thread_count) for thread_count in selection["generated_thread_counts"]))
    if selection["mode"] == "default" and selection["dropped_thread_counts"]:
        parts.append("dropped=" + "/".join(str(thread_count) for thread_count in selection["dropped_thread_counts"]))
    return " ".join(parts)

def parse_single_item(value, option_name):
    stripped = value.strip()
    if not stripped:
        return {"status": "invalid_result", "detail": f"{option_name}=empty_item", "item": ""}
    return {"status": "ok", "detail": stripped, "item": stripped}

def parse_csv_items(value, option_name):
    items = []
    for item in value.split(","):
        item_status = parse_single_item(item, option_name)
        if item_status["status"] != "ok":
            return {"status": item_status["status"], "detail": item_status["detail"], "items": []}
        items.append(item_status["item"])
    return {"status": "ok", "detail": value, "items": items}

def case_with_thread_counts(case, thread_counts):
    selected_case = dict(case)
    selected_case["thread_counts"] = list(thread_counts)
    return selected_case

def select_run_slug(case, cpu_slug, host, thread_selection, repeats):
    run_timestamp = time.strftime("%Y-%m-%d_%H%M")
    thread_token = thread_selection_slug(thread_selection["thread_counts"])
    base_slug = run_state_slug(host, repeats, thread_token, run_timestamp)
    suffix_index = 0
    while suffix_index < 1000:
        collision_suffix = "" if suffix_index == 0 else str(suffix_index + 1)
        run_slug = base_slug if not collision_suffix else f"{base_slug}-{collision_suffix}"
        result_dir = result_dir_for_run(case, cpu_slug, run_slug)
        if not result_dir.exists():
            return {
                "status": "ok",
                "detail": run_slug,
                "run_slug": run_slug,
                "result_dir": result_dir,
                "slug_metadata": {
                    "run_slug": run_slug,
                    "base_run_slug": base_slug,
                    "timestamp": run_timestamp,
                    "timestamp_source": "local_time",
                    "timestamp_format": "%Y-%m-%d_%H%M",
                    "thread_selection_token": thread_token,
                    "collision_suffix": collision_suffix
                }
            }
        suffix_index += 1
    return {
        "status": "invalid_result",
        "detail": f"run_slug_collision_exhausted base={base_slug}",
        "run_slug": "",
        "result_dir": result_dir_for_run(case, cpu_slug, base_slug),
        "slug_metadata": {}
    }

def thread_selection_slug(thread_counts):
    if not thread_counts:
        return "threads-none"
    first_thread = min(thread_counts)
    last_thread = max(thread_counts)
    expected_range = list(range(first_thread, last_thread + 1))
    if thread_counts == expected_range:
        if first_thread == last_thread:
            return f"threads-{first_thread}"
        return f"threads-{first_thread}-{last_thread}"
    return "threads-" + "-".join(str(thread_count) for thread_count in thread_counts)

def result_dir_for_run(case, cpu_slug, run_slug):
    return repo_path(f"results/{case['slug']}/{cpu_slug}/{run_slug}")

def write_run_manifest(result_dir, contracts, engines, case, repeats, runtime_records, host, thread_selection, run_slug_metadata):
    manifest_path = result_dir / "manifest.json"
    payload = {
        "schema_version": 2,
        "run_id": result_dir.name,
        "host_route": run_host_route(runtime_records),
        "case_id": case["id"],
        "benchmark_mode": case["benchmark_mode"],
        "thread_counts": case["thread_counts"],
        "thread_selection": thread_selection,
        "step_count": case["step_count"],
        "warmup_steps": case.get("warmup_steps", 0),
        "repeat_count": repeats,
        "run_slug_metadata": run_slug_metadata,
        "host": host,
        "engines": [
            manifest_engine_entry(engine, runtime_records.get(engine["id"]))
            for engine in engines
        ]
    }
    write_text_lf(manifest_path, json.dumps(payload, indent=2) + "\n")
    return {"path": manifest_path, "document": payload}

def run_host_route(runtime_records):
    host_routes = sorted({
        record["manifest"].get("host_route", "")
        for record in runtime_records.values()
        if record.get("manifest", {}).get("host_route", "")
    })
    if len(host_routes) == 1:
        return host_routes[0]
    return platform_id()

def manifest_engine_entry(engine, runtime_record):
    entry = {"id": engine["id"]}
    if runtime_record is not None:
        entry["release_artifact_manifest"] = metadata_path_text(runtime_record["path"])
        entry["source_version"] = runtime_record["manifest"]["source_version"]
        entry["toolchain_id"] = runtime_record["manifest"]["compiler_runtime"]["toolchain_id"]
    return entry

def run_engine(result_dir, raw_dir, engine, adapter, case, repeats, runtime_record):
    if runtime_record is None:
        emit(f"{engine['id']}_release_artifact", "tool_missing", "release_artifact_manifest")
        return 2
    artifact = runtime_record["manifest"]
    metadata = artifact_run_metadata(engine, artifact, runtime_record["path"])
    artifact_host_route = metadata.get("host_route", "")
    if artifact_host_route not in adapter.get("supported_platforms", []):
        emit(f"route_{engine['id']}", "unsupported_platform", f"host_route={artifact_host_route}")
        return 2
    if engine["id"] == "unity_physics":
        missing_unity_metadata = unity_metadata_missing_fields(metadata)
        if missing_unity_metadata:
            emit("unity_physics_metadata", "invalid_result", "missing=" + ",".join(missing_unity_metadata))
            return 2
        if metadata.get("backend") != "il2cpp":
            emit("unity_physics_metadata", "invalid_result", f"backend={metadata.get('backend', '')}")
            return 2
    executable = executable_path(engine, adapter, metadata, artifact)
    if executable is None:
        emit(f"{engine['id']}_executable", "tool_missing", "release_artifact_executable")
        return 2
    launcher = []
    launcher_mode = artifact.get("launcher", adapter["run"].get("launcher", "")) if artifact is not None else adapter["run"].get("launcher", "")
    executable_arg = str(executable)
    if launcher_mode == "dotnet":
        dotnet_status = dotnet_launcher_status(artifact)
        if dotnet_status["status"] != "ok":
            emit(f"{engine['id']}_dotnet", dotnet_status["status"], dotnet_status["detail"])
            return 2
        launcher = [dotnet_status["path"]]
        executable_arg = process_path_for_command(dotnet_status["path"], executable)
    elif launcher_mode not in ["", "direct"]:
        emit(f"{engine['id']}_launcher", "invalid_result", launcher_mode)
        return 2
    run_env = release_run_environment(artifact)
    for thread_count in case["thread_counts"]:
        engine_raw_dir = raw_dir / engine["id"] / f"t{thread_count}"
        engine_raw_dir.mkdir(parents=True)
        raw_output = raw_output_name(engine["id"], thread_count)
        raw_output_path = engine_raw_dir / raw_output
        path_command = launcher[0] if launcher else executable_arg
        if adapter["run"]["repeat_mode"] == "bulk":
            args = render_args(adapter["run"]["args"], case, thread_count, repeats, 0, raw_output, raw_output_path, path_command, metadata)
            log_stem = f"{engine['id']}_t{thread_count}_bulk"
            status = run_one(engine, engine_raw_dir, launcher + [executable_arg] + args, log_stem, run_env)
            if status != 0:
                return 2
        else:
            for repeat_index in range(repeats):
                args = render_args(adapter["run"]["args"], case, thread_count, repeats, repeat_index, raw_output, raw_output_path, path_command, metadata)
                log_stem = f"{engine['id']}_t{thread_count}_r{repeat_index}"
                status = run_one(engine, engine_raw_dir, launcher + [executable_arg] + args, log_stem, run_env)
                if status != 0:
                    return 2
        raw_file = raw_output_path
        if not raw_file.is_file():
            emit(f"{engine['id']}_raw_t{thread_count}", "run_failed", str(raw_file.relative_to(REPO_ROOT)))
            return 2
        emit(f"{engine['id']}_run_t{thread_count}", "ok", str(raw_file.relative_to(REPO_ROOT)))
    return 0

def raw_output_name(engine_id, thread_count):
    return f"{engine_id}_t{thread_count}_raw.csv"

def dotnet_launcher_status(artifact):
    target_framework = ""
    if artifact is not None:
        target_framework = artifact.get("package_metadata", {}).get("runtime_target_framework", "")
    candidates = dotnet_launcher_candidates(target_framework)
    details = []
    for candidate in candidates:
        runtime_status = dotnet_runtime_status(candidate, target_framework)
        if runtime_status["status"] == "ok":
            return {"status": "ok", "detail": runtime_status["detail"], "path": candidate}
        details.append(f"{candidate}:{runtime_status['detail']}")
    detail = ";".join(details) if details else f"set DOTNET_EXE for target={target_framework}"
    return {"status": "tool_missing", "detail": detail, "path": ""}

def dotnet_launcher_candidates(target_framework):
    env_value = os.environ.get("DOTNET_EXE", "")
    if env_value:
        return [env_value]
    candidates = []
    path_dotnet = shutil.which("dotnet")
    if path_dotnet:
        candidates.append(path_dotnet)
    path_dotnet_exe = shutil.which("dotnet.exe")
    if path_dotnet_exe:
        candidates.append(path_dotnet_exe)
    candidates.append("/mnt/c/Program Files/dotnet/dotnet.exe")
    unique_candidates = []
    for candidate in candidates:
        if candidate and candidate not in unique_candidates:
            unique_candidates.append(candidate)
    return unique_candidates

def dotnet_runtime_status(dotnet, target_framework):
    if not target_framework:
        return {"status": "ok", "detail": "runtime_target_unchecked"}
    version = target_framework[3:] if target_framework.startswith("net") else ""
    major = version.split(".", 1)[0] if version else ""
    if not major:
        return {"status": "invalid_result", "detail": f"target_framework={target_framework}"}
    result = run_process([dotnet, "--list-runtimes"])
    if result["status"] != "ok":
        return {"status": "tool_missing", "detail": "list_runtimes_failed"}
    lines = result["stdout"].decode("utf-8", errors="replace").splitlines()
    expected_prefix = f"Microsoft.NETCore.App {major}."
    for line in lines:
        if line.startswith(expected_prefix):
            return {"status": "ok", "detail": line}
    return {"status": "tool_missing", "detail": f"missing_Microsoft.NETCore.App_{major}.x"}

def process_path_for_command(command, path):
    command_name = Path(command).name.lower()
    candidate = Path(path).resolve()
    if not command_name.endswith(".exe"):
        return str(candidate)
    parts = candidate.parts
    if len(parts) >= 3 and parts[0] == "/" and parts[1] == "mnt" and len(parts[2]) == 1:
        drive = parts[2].upper()
        suffix = "\\".join(parts[3:])
        return f"{drive}:\\{suffix}" if suffix else f"{drive}:\\"
    return str(candidate)

def executable_path(engine, adapter, metadata, artifact=None):
    override = os.environ.get(adapter["run"].get("executable_env", ""), "")
    if override:
        return Path(override)
    if artifact is not None:
        return repo_path(artifact["executable_path"])
    metadata_path = metadata.get("executable_path", "")
    if metadata_path:
        return repo_path(metadata_path.replace("\\", "/"))
    return None

def unity_metadata_missing_fields(metadata):
    package_versions = metadata.get("unity_package_versions", {})
    required_package_ids = {
        "com.unity.physics": "unity_physics_package_version",
        "com.unity.entities": "entities_package_version",
        "com.unity.burst": "burst_package_version",
        "com.unity.collections": "collections_package_version"
    }
    missing = []
    for package_id, field_name in required_package_ids.items():
        if not package_versions.get(package_id, "").strip():
            missing.append(field_name)
    for field_name in ["engine_ref", "build_target", "backend", "burst_enabled_state", "safety_check_state"]:
        if not metadata.get(field_name, "").strip():
            missing.append(field_name)
    return missing

def render_args(tokens, case, thread_count, repeat_count, repeat_index, raw_output, raw_output_path, path_command, metadata):
    values = {
        "case_id": case["id"],
        "repeat_index": str(repeat_index),
        "raw_output": raw_output,
        "raw_output_path": process_path_for_command(path_command, raw_output_path),
        "step_count": str(case["step_count"]),
        "thread_count": str(thread_count),
        "warmup_steps": str(case.get("warmup_steps", 0))
    }
    return [token.format(**values) for token in tokens]

def run_one(engine, cwd, command, log_stem, env=None):
    result = run_process(command, cwd=cwd, env=env)
    log_status = write_process_logs(result, log_stem)
    detail = process_diagnostic_detail(result, command, cwd, log_status)
    if log_status["status"] not in {"ok", "not_configured"}:
        emit(f"{engine['id']}_process_log", log_status["status"], f"{detail} {log_status['detail']}")
        return 2
    if result["status"] != "ok":
        emit(f"{engine['id']}_run", "run_failed", detail)
        return 2
    if log_status["stdout_log"] or log_status["stderr_log"]:
        emit(f"{engine['id']}_process_log", "ok", detail)
    return 0

def normalize_results(result_dir, case, engines, repeats, engine_report_metadata_by_id, build_settings_rewrite_by_value, case_fixture_contract_by_id, engine_sources, manifest, runtime_records):
    output_path = result_dir / "normalized.csv"
    rows = []
    context_by_engine_id = normalization_context_by_engine_id(
        engines, engine_sources, manifest, runtime_records)
    for raw_file in sorted((result_dir / "raw").rglob("*_raw.csv")):
        raw_context_status = raw_file_context(raw_file, result_dir)
        if raw_context_status["status"] != "ok":
            emit("normalize", raw_context_status["status"], raw_context_status["detail"])
            return {"status": raw_context_status["status"], "detail": raw_context_status["detail"], "rows": []}
        raw_context = raw_context_status["context"]
        engine_context = context_by_engine_id.get(raw_context["engine_id"])
        if engine_context is None:
            emit("normalize", "invalid_result", f"raw_engine={raw_context['engine_id']} file={raw_file}")
            return {"status": "invalid_result", "detail": f"raw_engine={raw_context['engine_id']} file={raw_file}", "rows": []}
        with raw_file.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            header_status = validate_raw_result_header(reader.fieldnames, raw_file)
            if header_status["status"] != "ok":
                emit("normalize", header_status["status"], header_status["detail"])
                return {"status": header_status["status"], "detail": header_status["detail"], "rows": []}
            for row_index, row in enumerate(reader):
                normalized_status = normalize_raw_result_row(
                    row,
                    raw_file,
                    raw_context,
                    engine_context,
                    case,
                    repeats,
                    row_index,
                    result_dir,
                    engine_report_metadata_by_id,
                    build_settings_rewrite_by_value,
                    case_fixture_contract_by_id)
                if normalized_status["status"] != "ok":
                    emit("normalize", normalized_status["status"], normalized_status["detail"])
                    return {"status": normalized_status["status"], "detail": normalized_status["detail"], "rows": []}
                rows.append(normalized_status["row"])
    output_columns = ["run_id"] + REQUIRED_NORMALIZED_COLUMNS + OPTIONAL_NORMALIZED_COLUMNS + ["raw_path"]
    with output_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=output_columns, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    validation_status = validate_normalized_rows(
        rows, case, engines, repeats, case_fixture_contract_by_id, engine_sources=engine_sources)
    if validation_status != 0:
        return {"status": "invalid_result", "detail": str(output_path), "rows": []}
    return {"status": "ok", "detail": str(output_path), "rows": rows}

def normalization_context_by_engine_id(engines, engine_sources, manifest, runtime_records):
    manifest_engines = {
        entry.get("id", ""): entry
        for entry in manifest.get("engines", [])
        if entry.get("id", "")
    }
    contexts = {}
    for engine in engines:
        engine_id = engine["id"]
        adapter = engine_sources.get(engine_id, {})
        manifest_entry = manifest_engines.get(engine_id, {})
        runtime_record = runtime_records.get(engine_id)
        artifact_metadata = {}
        if runtime_record is not None:
            artifact_metadata = artifact_run_metadata(
                engine, runtime_record["manifest"], runtime_record["path"])
        contexts[engine_id] = {
            "engine": engine,
            "adapter": adapter,
            "manifest": manifest,
            "manifest_entry": manifest_entry,
            "artifact_metadata": artifact_metadata,
        }
    return contexts

def raw_file_context(raw_file, result_dir):
    try:
        relative_parts = raw_file.relative_to(result_dir / "raw").parts
    except ValueError:
        return {"status": "invalid_result", "detail": f"raw_path={raw_file}", "context": {}}
    if len(relative_parts) < 3:
        return {"status": "invalid_result", "detail": f"raw_path={raw_file}", "context": {}}
    engine_id = relative_parts[0]
    thread_dir = relative_parts[1]
    if not thread_dir.startswith("t"):
        return {"status": "invalid_result", "detail": f"raw_thread_dir={thread_dir} file={raw_file}", "context": {}}
    try:
        thread_count = int(thread_dir[1:])
    except ValueError:
        return {"status": "invalid_result", "detail": f"raw_thread_dir={thread_dir} file={raw_file}", "context": {}}
    if thread_count <= 0:
        return {"status": "invalid_result", "detail": f"raw_thread_count={thread_count} file={raw_file}", "context": {}}
    return {"status": "ok", "detail": str(raw_file), "context": {"engine_id": engine_id, "thread_count": thread_count}}

def validate_raw_result_header(fieldnames, raw_file):
    if fieldnames is None:
        return {"status": "invalid_result", "detail": f"missing_header={raw_file}"}
    fieldname_set = set(fieldnames)
    allowed_columns = set(COMPACT_RAW_RESULT_COLUMNS)
    unsupported_columns = sorted(fieldname_set.difference(allowed_columns))
    if unsupported_columns:
        return {"status": "invalid_result", "detail": f"unsupported_raw_column={','.join(unsupported_columns)} file={raw_file}"}
    for column in [
        "body_count",
        "shape_count",
        "invalid_transform_count",
        "below_floor_count",
        "out_of_bounds_count",
        "case_status",
        "metric_status",
        "physics_elapsed_ms",
    ]:
        if column not in fieldname_set:
            return {"status": "invalid_result", "detail": f"missing_raw_column={column} file={raw_file}"}
    return {"status": "ok", "detail": "compact_raw_schema"}

def normalize_raw_result_row(
    row,
    raw_file,
    raw_context,
    engine_context,
    case,
    repeats,
    row_index,
    result_dir,
    engine_report_metadata_by_id,
    build_settings_rewrite_by_value,
    case_fixture_contract_by_id,
):
    if row_index >= repeats:
        return {"status": "invalid_result", "detail": f"repeat_index={row_index} repeats={repeats} file={raw_file}", "row": {}}
    engine_id = raw_context["engine_id"]
    thread_count = raw_context["thread_count"]
    base_status = trusted_normalized_base(
        row,
        raw_file,
        engine_id,
        thread_count,
        row_index,
        case,
        engine_context,
        engine_report_metadata_by_id,
        build_settings_rewrite_by_value,
        case_fixture_contract_by_id)
    if base_status["status"] != "ok":
        return base_status
    normalized = base_status["row"]
    measurement_status = raw_measurements(row, raw_file, case)
    if measurement_status["status"] != "ok":
        return measurement_status
    normalized.update(measurement_status["measurements"])
    metric_status = normalized_metrics(normalized["physics_elapsed_ms"], case, raw_file)
    if metric_status["status"] != "ok":
        return metric_status
    normalized.update(metric_status["metrics"])
    apply_normalized_metadata(normalized, engine_report_metadata_by_id, build_settings_rewrite_by_value)
    normalized["run_id"] = result_dir.name
    normalized["raw_path"] = str(raw_file.relative_to(REPO_ROOT))
    return {"status": "ok", "detail": str(raw_file), "row": normalized}

def trusted_normalized_base(
    row,
    raw_file,
    engine_id,
    thread_count,
    repeat_index,
    case,
    engine_context,
    engine_report_metadata_by_id,
    build_settings_rewrite_by_value,
    case_fixture_contract_by_id,
):
    engine = engine_context["engine"]
    adapter = engine_context["adapter"]
    artifact_metadata = engine_context["artifact_metadata"]
    manifest = engine_context["manifest"]
    run_or_visual = route_metadata(adapter, case)
    thread_status = normalized_thread_metadata(row, engine_id, thread_count, adapter)
    if thread_status["status"] != "ok":
        return thread_status
    fixture_status = normalized_fixture_metadata(engine_id, case, case_fixture_contract_by_id)
    if fixture_status["status"] != "ok":
        return fixture_status
    metadata_row = {
        "engine_id": engine_id,
        "engine_ref": artifact_metadata.get("engine_ref", engine.get("commit", "")),
        "host_route": artifact_metadata.get("host_route", manifest.get("host_route", "")),
        "toolchain_id": artifact_metadata.get("toolchain_id", row.get("toolchain_id", "")),
        "case_id": case["id"],
        "benchmark_mode": case["benchmark_mode"],
        "thread_count": str(thread_count),
        "step_count": str(case["step_count"]),
        "warmup_steps": str(case.get("warmup_steps", 0)),
        "repeat_index": str(repeat_index),
        "query_count": str(case.get("query_count", 0)),
        "runtime_route": run_or_visual.get("runtime_route", run_or_visual.get("route", "")),
        "timing_scope": run_or_visual.get("timing_scope", ""),
        "sleeping_status": run_or_visual.get("sleeping_status", ""),
        "physics_settings": engine_report_metadata_by_id.get(engine_id, {}).get("physics_settings", ""),
        "build_settings": engine_report_metadata_by_id.get(engine_id, {}).get("build_settings", ""),
    }
    metadata_row.update(thread_status["row"])
    metadata_row.update(fixture_status["row"])
    metadata_row.update(unity_normalized_metadata(artifact_metadata))
    apply_normalized_metadata(metadata_row, engine_report_metadata_by_id, build_settings_rewrite_by_value)
    return {"status": "ok", "detail": engine_id, "row": metadata_row}

def route_metadata(adapter, case):
    if case.get("benchmark_mode", "") in {"visualized_local", "visualized_release"}:
        return adapter.get("visual", {})
    return adapter.get("run", {})

def normalized_thread_metadata(row, engine_id, thread_count, adapter):
    support = adapter.get("thread_support", {})
    if not support:
        return {"status": "invalid_result", "detail": f"worker_contract_missing engine={engine_id}", "row": {}}
    policy = support.get("effective_worker_count", "")
    expected_worker_count = expected_worker_count_for_policy(policy, thread_count)
    if expected_worker_count < 0:
        return {"status": "invalid_result", "detail": f"worker_policy={policy} engine={engine_id}", "row": {}}
    effective_worker_count = row.get("effective_worker_count", "")
    if not effective_worker_count:
        effective_worker_count = str(expected_worker_count)
    effective_thread_count = row.get("effective_thread_count", "")
    if not effective_thread_count:
        effective_thread_count = str(thread_count)
    requested_policy = support.get("requested_worker_count", "")
    requested_worker_count = expected_worker_count_for_policy(requested_policy, thread_count)
    if requested_worker_count < 0:
        return {"status": "invalid_result", "detail": f"requested_worker_policy={requested_policy} engine={engine_id}", "row": {}}
    main_thread_participates = support.get("main_thread_participates", "")
    if main_thread_participates not in {"yes", "no"}:
        return {"status": "invalid_result", "detail": f"main_thread_participates_policy={main_thread_participates} engine={engine_id}", "row": {}}
    taskgraph_worker_count = ""
    if engine_id == "unreal_chaos":
        taskgraph_worker_count = str(thread_count - 1 if thread_count > 1 else 1)
    return {
        "status": "ok",
        "detail": engine_id,
        "row": {
            "requested_thread_count": str(thread_count),
            "effective_thread_count": effective_thread_count,
            "requested_worker_count": str(requested_worker_count),
            "requested_taskgraph_worker_count": taskgraph_worker_count,
            "actual_taskgraph_worker_count": row.get("actual_taskgraph_worker_count", ""),
            "effective_worker_count": effective_worker_count,
            "main_thread_participates": main_thread_participates,
        },
    }

def normalized_fixture_metadata(engine_id, case, case_fixture_contract_by_id):
    fixture_contract = case_fixture_contract_by_id.get(case["id"], {})
    fixture_version = fixture_contract.get("engine_fixture_versions", {}).get(engine_id, "")
    if fixture_contract and not fixture_version:
        return {"status": "invalid_result", "detail": f"fixture_engine={engine_id}", "row": {}}
    return {
        "status": "ok",
        "detail": engine_id,
        "row": {
            "fixture_semantic": fixture_contract.get("semantic", ""),
            "fixture_version": fixture_version,
            "static_box_count": str(fixture_contract.get("static_box_count", "")),
        },
    }

def unity_normalized_metadata(artifact_metadata):
    package_versions = artifact_metadata.get("unity_package_versions", {})
    compiler_detail = artifact_metadata.get("compiler_detail", "")
    return {
        "unity_editor_version": unity_editor_version(artifact_metadata.get("engine_ref", ""), compiler_detail),
        "unity_physics_package_version": package_versions.get("com.unity.physics", ""),
        "entities_package_version": package_versions.get("com.unity.entities", ""),
        "burst_package_version": package_versions.get("com.unity.burst", ""),
        "collections_package_version": package_versions.get("com.unity.collections", ""),
        "build_target": artifact_metadata.get("build_target", ""),
        "backend": artifact_metadata.get("backend", ""),
        "burst_enabled_state": artifact_metadata.get("burst_enabled_state", ""),
        "safety_check_state": artifact_metadata.get("safety_check_state", ""),
    }

def unity_editor_version(engine_ref, compiler_detail):
    match = re.search(r"unity[-_= ]+([0-9]+\.[0-9]+\.[0-9]+[a-z][0-9]+)", engine_ref, re.IGNORECASE)
    if match:
        return match.group(1)
    match = re.search(r"unity_editor_version=\s*([^;,\s]+)", compiler_detail, re.IGNORECASE)
    if match:
        return match.group(1)
    return ""

def raw_measurements(row, raw_file, case):
    output = {}
    for column in [
        "body_count",
        "shape_count",
        "invalid_transform_count",
        "below_floor_count",
        "out_of_bounds_count",
        "case_status",
        "metric_status",
        "physics_elapsed_ms",
    ]:
        value = row.get(column, "")
        if value == "":
            return {"status": "invalid_result", "detail": f"missing_raw_column={column} file={raw_file}", "measurements": {}}
        output[column] = value
    for column in ["body_count", "shape_count", "invalid_transform_count", "below_floor_count", "out_of_bounds_count"]:
        try:
            int(output[column])
        except ValueError:
            return {"status": "invalid_result", "detail": f"raw_int column={column} value={output[column]} file={raw_file}", "measurements": {}}
    if row.get("completed_step_count", ""):
        try:
            completed_step_count = int(row["completed_step_count"])
        except ValueError:
            return {"status": "invalid_result", "detail": f"completed_step_count={row['completed_step_count']} file={raw_file}", "measurements": {}}
        if completed_step_count != int(case["step_count"]):
            return {"status": "invalid_result", "detail": f"completed_step_count={completed_step_count} expected={case['step_count']} file={raw_file}", "measurements": {}}
        output["completed_step_count"] = row["completed_step_count"]
    else:
        output["completed_step_count"] = ""
    for column in ["render_elapsed_ms", "present_wait_ms", "visual_validation_status", "proof_path"]:
        output[column] = row.get(column, "")
    return {"status": "ok", "detail": str(raw_file), "measurements": output}

def normalized_metrics(physics_elapsed_text, case, raw_file):
    try:
        physics_elapsed_ms = float(physics_elapsed_text)
    except ValueError:
        return {"status": "invalid_result", "detail": f"physics_elapsed_ms={physics_elapsed_text} file={raw_file}", "metrics": {}}
    if physics_elapsed_ms <= 0.0 or not math.isfinite(physics_elapsed_ms):
        return {"status": "invalid_result", "detail": f"physics_elapsed_ms={physics_elapsed_text} file={raw_file}", "metrics": {}}
    step_count = int(case["step_count"])
    ms_per_step = physics_elapsed_ms / step_count
    steps_per_second = 1000.0 / ms_per_step
    return {
        "status": "ok",
        "detail": str(raw_file),
        "metrics": {
            "ms_per_step": f"{ms_per_step:.9f}",
            "steps_per_second": f"{steps_per_second:.9f}",
        },
    }
