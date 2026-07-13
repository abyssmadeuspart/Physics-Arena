import csv
import json
import time

from .common import (
    DEFAULT_RELEASE_PLATFORM,
    REPO_ROOT,
    emit,
    load_json,
    metadata_path_text,
    process_diagnostic_detail,
    repo_path,
    run_process,
    sha256_file,
    write_process_logs,
    write_text_lf,
)
from .contracts import load_contracts, selected_engines, validate_case_route_support
from .host import collect_host_metadata, run_state_slug
from .options import parse_positive_int, parse_value_options, single_option_value
from .release_files import (
    load_runtime_records,
    load_visual_runtime_records,
    release_artifacts_status,
    release_run_environment,
    visual_release_artifacts_status,
)
from .reports import command_report
from .result_contracts import write_summary
from .runner import (
    case_with_thread_counts,
    default_repeat_count,
    dotnet_launcher_status,
    normalize_results,
    parse_engine_ids,
    parse_thread_selection_request,
    process_path_for_command,
    raw_output_name,
    resolve_thread_selection,
    thread_selection_diagnostic,
    validate_engine_thread_support,
    write_raw_slice_summaries,
)

VISUAL_RELEASE_MODE = "visualized_release"

def command_visual_run(args):
    contracts = load_contracts()
    if not args:
        emit("visual_arguments", "invalid_result", "missing_case_slug")
        return 2
    case_slug = args[0]
    case = contracts["case_by_slug"].get(case_slug)
    if case is None:
        emit("visual_arguments", "invalid_result", f"case={case_slug}")
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
        emit("visual_arguments", options_status["status"], options_status["detail"])
        return 2
    if options_status["positionals"]:
        emit("visual_arguments", "invalid_result", "unexpected_args=" + ",".join(options_status["positionals"]))
        return 2
    engine_set_status = single_option_value(options_status, "--engine-set", "default_visual_release")
    if engine_set_status["status"] != "ok":
        emit("visual_arguments", engine_set_status["status"], engine_set_status["detail"])
        return 2
    engine_ids_status = parse_engine_ids(options_status)
    if engine_ids_status["status"] != "ok":
        emit("visual_arguments", engine_ids_status["status"], engine_ids_status["detail"])
        return 2
    engines = selected_engines(contracts, engine_set_status["value"], "", engine_ids_status["items"])
    case_support_status = validate_case_route_support(contracts, engines, case["id"], "visual")
    if case_support_status["status"] != "ok":
        emit("visual_arguments", case_support_status["status"], case_support_status["detail"])
        return 2
    thread_request_status = parse_thread_selection_request(options_status, case)
    if thread_request_status["status"] != "ok":
        emit("visual_arguments", thread_request_status["status"], thread_request_status["detail"])
        return 2
    release_platform_status = single_option_value(options_status, "--platform", DEFAULT_RELEASE_PLATFORM)
    if release_platform_status["status"] != "ok":
        emit("visual_arguments", release_platform_status["status"], release_platform_status["detail"])
        return 2
    release_platform = release_platform_status["value"]
    repeats_value_status = single_option_value(options_status, "--repeats", str(default_repeat_count(case)))
    if repeats_value_status["status"] != "ok":
        emit("visual_arguments", repeats_value_status["status"], repeats_value_status["detail"])
        return 2
    repeats_status = parse_positive_int(repeats_value_status["value"])
    if repeats_status["status"] != "ok":
        emit("visual_arguments", "invalid_result", f"repeats={repeats_status['detail']}")
        return 2
    repeats = repeats_status["value"]
    artifact_status = release_artifacts_status(contracts, release_platform, engines)
    if artifact_status["status"] != "ok":
        for failure in artifact_status["failures"]:
            emit(failure["component"], failure["status"], failure["detail"])
        failed_components = "/".join(failure["component"] for failure in artifact_status["failures"])
        emit("visual_gate", artifact_status["status"], f"release_artifacts_failed platform={release_platform} failure_count={artifact_status['failure_count']} components={failed_components}")
        emit("visual_action", "required", "restore the matching checked-in release/windows-x64 package and rerun")
        return 2
    host_status = collect_host_metadata()
    if host_status["status"] != "ok":
        emit("visual_host", host_status["status"], host_status["detail"])
        return 2
    thread_counts_status = resolve_thread_selection(thread_request_status, case, host_status["host"])
    if thread_counts_status["status"] != "ok":
        emit("visual_arguments", thread_counts_status["status"], thread_counts_status["detail"])
        return 2
    engine_thread_status = validate_engine_thread_support(contracts, engines, thread_counts_status["thread_counts"])
    if engine_thread_status["status"] != "ok":
        emit("visual_arguments", engine_thread_status["status"], engine_thread_status["detail"])
        return 2
    emit("visual_thread_selection", "ok", thread_selection_diagnostic(thread_counts_status["selection"]))
    visual_case = visual_case_with_thread_counts(case, thread_counts_status["thread_counts"])
    run_slug_status = select_visual_run_slug(visual_case, host_status["cpu_slug"], host_status["host"], thread_counts_status["selection"], repeats)
    if run_slug_status["status"] != "ok":
        emit("visual_arguments", run_slug_status["status"], run_slug_status["detail"])
        return 2
    visual_artifact_status = visual_release_artifacts_status(contracts, release_platform, engines)
    if visual_artifact_status["status"] != "ok":
        emit("visual_gate", visual_artifact_status["status"], visual_artifact_status["detail"])
        return 2
    runtime_records = load_runtime_records(contracts, engines, release_platform)
    if runtime_records["status"] != "ok":
        emit("visual_gate", runtime_records["status"], runtime_records["detail"])
        return 2
    visual_records = load_visual_runtime_records(contracts, release_platform, engines)
    if visual_records["status"] != "ok":
        emit("visual_gate", visual_records["status"], visual_records["detail"])
        return 2
    result_dir = run_slug_status["result_dir"]
    raw_dir = result_dir / "raw"
    raw_dir.mkdir(parents=True)
    emit("visual_component", "status", "detail")
    manifest = write_visual_run_manifest(
        result_dir,
        engines,
        visual_case,
        repeats,
        runtime_records["records"],
        visual_records,
        host_status["host"],
        thread_counts_status["selection"],
        run_slug_status["slug_metadata"]
    )
    exit_code = 0
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        result = run_visual_engine(
            result_dir,
            raw_dir,
            engine,
            adapter,
            visual_case,
            repeats,
            visual_records["renderer"],
            visual_records["engines"].get(engine["id"]))
        if result != 0:
            exit_code = 2
    if exit_code != 0:
        emit("visual_gate", "run_failed", str(raw_dir.relative_to(REPO_ROOT)))
        return exit_code
    normalize_status = normalize_results(
        result_dir,
        visual_case,
        engines,
        repeats,
        contracts["engine_report_metadata_by_id"],
        contracts["build_settings_rewrite_by_value"],
        contracts["case_fixture_contract_by_id"],
        contracts["engine_sources"])
    if normalize_status != 0:
        emit("visual_gate", "invalid_result", str((result_dir / "normalized.csv").relative_to(REPO_ROOT)))
        return 2
    summary_status = write_summary(
        result_dir,
        contracts["engine_report_metadata_by_id"],
        contracts["build_settings_rewrite_by_value"])
    if summary_status != 0:
        return 2
    slice_summary_status = write_raw_slice_summaries(result_dir, contracts, load_json(manifest))
    if slice_summary_status != 0:
        return 2
    report_status = command_report([str(result_dir.relative_to(REPO_ROOT))])
    if report_status != 0:
        emit("visual_report", "invalid_result", str(result_dir.relative_to(REPO_ROOT)))
        return 2
    emit("visual_gate", "ok", str((result_dir / "normalized.csv").relative_to(REPO_ROOT)))
    emit("visual_summary", "ok", str((result_dir / "summary.csv").relative_to(REPO_ROOT)))
    emit("visual_svg", "ok", str((result_dir / "summary.svg").relative_to(REPO_ROOT)))
    emit("visual_report", "ok", str((result_dir / "report.md").relative_to(REPO_ROOT)))
    emit("visual_manifest", "ok", str(manifest.relative_to(REPO_ROOT)))
    emit("visual_result", "ok", str(result_dir.relative_to(REPO_ROOT)))
    return 0

def visual_case_with_thread_counts(case, thread_counts):
    selected_case = case_with_thread_counts(case, thread_counts)
    selected_case["benchmark_mode"] = VISUAL_RELEASE_MODE
    return selected_case

def select_visual_run_slug(case, cpu_slug, host, thread_selection, repeats):
    run_timestamp = time.strftime("%Y-%m-%d_%H%M")
    thread_token = visual_thread_selection_slug(thread_selection["thread_counts"])
    base_slug = run_state_slug(host, repeats, thread_token, run_timestamp) + "_visual"
    suffix_index = 0
    while suffix_index < 1000:
        collision_suffix = "" if suffix_index == 0 else str(suffix_index + 1)
        run_slug = base_slug if not collision_suffix else f"{base_slug}-{collision_suffix}"
        result_dir = repo_path(f"results/{case['slug']}/{cpu_slug}/{run_slug}")
        if not result_dir.exists():
            return {
                "status": "ok",
                "detail": run_slug,
                "result_dir": result_dir,
                "slug_metadata": {
                    "run_slug": run_slug,
                    "base_run_slug": base_slug,
                    "timestamp": run_timestamp,
                    "timestamp_source": "local_time",
                    "timestamp_format": "%Y-%m-%d_%H%M",
                    "thread_selection_token": thread_token,
                    "collision_suffix": collision_suffix,
                    "run_kind": "visual_release"
                }
            }
        suffix_index += 1
    return {
        "status": "invalid_result",
        "detail": f"run_slug_collision_exhausted base={base_slug}",
        "result_dir": repo_path(f"results/{case['slug']}/{cpu_slug}/{base_slug}"),
        "slug_metadata": {}
    }

def visual_thread_selection_slug(thread_counts):
    if not thread_counts:
        return "threads-none"
    return "threads-" + "-".join(str(thread_count) for thread_count in thread_counts)

def write_visual_run_manifest(result_dir, engines, case, repeats, runtime_records, visual_records, host, thread_selection, run_slug_metadata):
    manifest_path = result_dir / "manifest.json"
    renderer_path = visual_records["renderer"]["path"]
    payload = {
        "schema_version": 1,
        "run_id": result_dir.name,
        "host_route": "windows",
        "case_id": case["id"],
        "benchmark_mode": case["benchmark_mode"],
        "thread_counts": case["thread_counts"],
        "thread_selection": thread_selection,
        "step_count": case["step_count"],
        "warmup_steps": case.get("warmup_steps", 0),
        "repeat_count": repeats,
        "run_slug_metadata": run_slug_metadata,
        "host": host,
        "visual_shared_renderer_artifact_manifest": metadata_path_text(renderer_path),
        "visual_shared_renderer_artifact_manifest_sha256": sha256_file(renderer_path),
        "engines": [
            visual_manifest_engine_entry(engine, runtime_records.get(engine["id"]), visual_records["engines"].get(engine["id"]))
            for engine in engines
        ]
    }
    write_text_lf(manifest_path, json.dumps(payload, indent=2) + "\n")
    return manifest_path

def visual_manifest_engine_entry(engine, runtime_record, visual_engine_record):
    entry = {"id": engine["id"]}
    if runtime_record is not None:
        entry["release_artifact_manifest"] = metadata_path_text(runtime_record["path"])
        entry["artifact_manifest_sha256"] = sha256_file(runtime_record["path"])
    if visual_engine_record is not None:
        entry["visual_release_mode"] = visual_engine_record["manifest"].get("visual", {}).get("producer_mode_arg", "")
    return entry

def run_visual_engine(result_dir, raw_dir, engine, adapter, case, repeats, renderer_record, visual_engine_record):
    if visual_engine_record is None:
        emit(f"{engine['id']}_visual_release_artifact", "tool_missing", "visual_engine_artifact_manifest")
        return 2
    renderer = renderer_record["manifest"]
    producer = visual_engine_record["manifest"]
    renderer_executable = repo_path(renderer["executable_path"])
    producer_executable = repo_path(producer["executable_path"])
    if not renderer_executable.is_file():
        emit("visual_renderer", "tool_missing", metadata_path_text(renderer_executable))
        return 2
    if not producer_executable.is_file():
        emit(f"{engine['id']}_visual_producer", "tool_missing", metadata_path_text(producer_executable))
        return 2
    producer_launcher_status = visual_producer_launcher_status(producer)
    if producer_launcher_status["status"] != "ok":
        emit(f"{engine['id']}_visual_launcher", producer_launcher_status["status"], producer_launcher_status["detail"])
        return 2
    visual = adapter.get("visual", {})
    run_env = release_run_environment(renderer)
    for thread_count in case["thread_counts"]:
        engine_raw_dir = raw_dir / engine["id"] / f"t{thread_count}"
        engine_raw_dir.mkdir(parents=True)
        raw_output = raw_output_name(engine["id"], thread_count)
        raw_output_path = engine_raw_dir / raw_output
        for repeat_index in range(repeats):
            repeat_raw_path = engine_raw_dir / f"{engine['id']}_t{thread_count}_r{repeat_index}_visual.csv"
            proof_path = engine_raw_dir / visual_artifact_name(visual, engine["id"], thread_count, repeat_index)
            command = visual_renderer_command(
                renderer_executable,
                producer_executable,
                producer_launcher_status,
                producer,
                engine,
                case,
                thread_count,
                repeat_index,
                repeat_raw_path,
                proof_path)
            result = run_process(
                command,
                cwd=renderer_executable.parent,
                env=run_env)
            log_stem = f"{engine['id']}_t{thread_count}_r{repeat_index}_visual"
            log_status = write_process_logs(result, log_stem)
            process_detail = process_diagnostic_detail(result, command, renderer_executable.parent, log_status)
            if log_status["status"] not in {"ok", "not_configured"}:
                emit(f"{engine['id']}_visual_process_log", log_status["status"], f"{process_detail} {log_status['detail']}")
                return 2
            if result["status"] != "ok":
                emit(f"{engine['id']}_visual_t{thread_count}_r{repeat_index}", "run_failed", process_detail)
                return 2
            if log_status["stdout_log"] or log_status["stderr_log"]:
                emit(f"{engine['id']}_visual_process_log", "ok", process_detail)
            proof_status = validate_visual_outputs(repeat_raw_path, proof_path, result_dir)
            if proof_status["status"] != "ok":
                emit(f"{engine['id']}_visual_t{thread_count}_r{repeat_index}", proof_status["status"], proof_status["detail"])
                return 2
            append_status = append_visual_raw_row(raw_output_path, repeat_raw_path)
            if append_status["status"] != "ok":
                emit(f"{engine['id']}_visual_t{thread_count}_r{repeat_index}", append_status["status"], append_status["detail"])
                return 2
            emit(f"{engine['id']}_visual_t{thread_count}_r{repeat_index}", "ok", str(proof_path.relative_to(REPO_ROOT)))
    return 0

def visual_producer_launcher_status(producer):
    launcher = producer.get("launcher", "")
    if launcher == "direct":
        return {"status": "ok", "detail": "direct", "path": ""}
    if launcher == "dotnet":
        return dotnet_launcher_status(producer)
    return {"status": "invalid_result", "detail": f"launcher={launcher}", "path": ""}

def visual_artifact_name(visual, engine_id, thread_count, repeat_index):
    template = visual.get("artifact_name", "{engine_id}_t{thread_count}_r{repeat_index}_visual.json")
    return template.format(
        engine_id=engine_id,
        thread_count=thread_count,
        repeat_index=repeat_index)

def visual_renderer_command(renderer_executable, producer_executable, producer_launcher_status, producer, engine, case, thread_count, repeat_index, raw_output_path, proof_path):
    renderer_command = str(renderer_executable)
    producer_command = process_path_for_command(renderer_command, producer_executable)
    command = [
        renderer_command,
        f"--engine={engine['id']}",
        f"--case={case['id']}",
        f"--thread-count={thread_count}",
        f"--step-count={case['step_count']}",
        f"--warmup-steps={case.get('warmup_steps', 0)}",
        f"--repeat-index={repeat_index}",
        f"--output={process_path_for_command(renderer_command, raw_output_path)}",
        f"--producer-command={producer_command}",
        "--visual-mode=live",
        f"--visual-proof-json={process_path_for_command(renderer_command, proof_path)}",
    ]
    visual = producer.get("visual", {})
    for prefix_arg in visual.get("producer_prefix_args", []):
        command.append(f"--producer-prefix-arg={prefix_arg}")
    if producer_launcher_status.get("path", ""):
        command.append(f"--producer-launcher-command={process_path_for_command(renderer_command, producer_launcher_status['path'])}")
    return command

def validate_visual_outputs(raw_output_path, proof_path, result_dir):
    if not raw_output_path.is_file():
        return {"status": "run_failed", "detail": str(raw_output_path.relative_to(REPO_ROOT))}
    if not proof_path.is_file():
        return {"status": "run_failed", "detail": str(proof_path.relative_to(REPO_ROOT))}
    rewrite_status = rewrite_visual_raw_proof_path(raw_output_path, proof_path)
    if rewrite_status["status"] != "ok":
        return rewrite_status
    proof = load_json(proof_path)
    if proof.get("visual_validation_status", "") != "ok" or proof.get("run_status", "") != "ok":
        return {"status": "invalid_result", "detail": str(proof_path.relative_to(REPO_ROOT))}
    return {"status": "ok", "detail": str(raw_output_path.relative_to(result_dir))}

def append_visual_raw_row(canonical_raw_path, repeat_raw_path):
    with repeat_raw_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = list(reader.fieldnames or [])
        rows = [dict(row) for row in reader]
    if not fieldnames:
        return {"status": "invalid_result", "detail": f"missing_header={repeat_raw_path}"}
    if len(rows) != 1:
        return {"status": "invalid_result", "detail": f"visual_raw_rows={len(rows)} file={repeat_raw_path}"}
    write_header = not canonical_raw_path.is_file()
    if not write_header:
        with canonical_raw_path.open("r", encoding="utf-8", newline="") as handle:
            existing_reader = csv.DictReader(handle)
            if list(existing_reader.fieldnames or []) != fieldnames:
                return {"status": "invalid_result", "detail": f"raw_header_mismatch={canonical_raw_path}"}
    with canonical_raw_path.open("a", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
        if write_header:
            writer.writeheader()
        writer.writerow(rows[0])
    repeat_raw_path.unlink()
    return {"status": "ok", "detail": metadata_path_text(canonical_raw_path)}

def rewrite_visual_raw_proof_path(raw_output_path, proof_path):
    with raw_output_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None or "proof_path" not in reader.fieldnames:
            return {"status": "invalid_result", "detail": f"proof_path_column={raw_output_path}"}
        rows = [dict(row) for row in reader]
    relative_proof_path = metadata_path_text(proof_path)
    for row in rows:
        row["proof_path"] = relative_proof_path
    with raw_output_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=reader.fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return {"status": "ok", "detail": metadata_path_text(raw_output_path)}
