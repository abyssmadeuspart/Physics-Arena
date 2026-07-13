import csv
import hashlib
import io
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tarfile
import time
from pathlib import Path
from xml.sax.saxutils import escape

REPO_ROOT = Path(__file__).resolve().parents[2]
CONFIG_DIR = REPO_ROOT / "config"
ENGINES_PATH = CONFIG_DIR / "engines.json"
CASES_PATH = CONFIG_DIR / "cases.json"
REPORTS_PATH = CONFIG_DIR / "reports.json"
RELEASE_DIR = REPO_ROOT / "release"
DEFAULT_RELEASE_PLATFORM = "windows-x64"
PLACEHOLDER_PATTERN = re.compile(r"{([a-z_]+)}")
PROCESS_LOG_STEM_PATTERN = re.compile(r"^[A-Za-z0-9._-]+$")
ALLOWED_PLACEHOLDERS = {
    "case_id",
    "repeat_index",
    "raw_output",
    "raw_output_path",
    "step_count",
    "thread_count",
    "warmup_steps"
}
SUPPORTED_BUILD_ROUTES = {
    "cargo_release",
    "clang_cmake_ninja_release",
    "clang_cmake_ninja_distribution",
    "dotnet8_release",
    "dotnet8_release_no_profiling",
    "dotnet10_release_no_profiling",
    "physx34_clangcl_sdk_release",
    "nvidia_physx34_optimized_release",
    "nvidia_physx5_vc17_cpu_release",
    "route_unavailable",
    "unreal_chaos_ubt_program_win64_development",
    "unreal_chaos_ubt_program_win64_shipping",
    "unity_batchmode",
    "unity_player"
}
SUPPORTED_TOOL_REQUIREMENTS = {
    "cargo",
    "clang",
    "cmake",
    "dotnet",
    "git",
    "msbuild",
    "ninja",
    "rustc",
    "unity"
}
SUPPORTED_REPEAT_MODES = {
    "bulk",
    "per_repeat",
    "route_unavailable"
}
SUPPORTED_VISUAL_MODES = {
    "live"
}
SUPPORTED_VISUAL_ROUTES = {
    "shared_visual_renderer_cli",
    "route_unavailable"
}
SUPPORTED_VISUAL_TRANSPORTS = {
    "shared_visual_transport",
    "route_unavailable"
}
SUPPORTED_REPORT_STATUSES = {
    "current_headless_api"
}
RUNNABLE_ROUTE_STATUSES = {
    "supported",
    "working",
    "experimental"
}
SPECIAL_ENGINE_SETS = {"all"}
REQUIRED_NORMALIZED_COLUMNS = [
    "engine_id",
    "engine_ref",
    "host_route",
    "toolchain_id",
    "case_id",
    "benchmark_mode",
    "thread_count",
    "step_count",
    "warmup_steps",
    "repeat_index",
    "body_count",
    "shape_count",
    "query_count",
    "ms_per_step",
    "steps_per_second",
    "invalid_transform_count",
    "below_floor_count",
    "out_of_bounds_count",
    "case_status",
    "metric_status"
]
OPTIONAL_NORMALIZED_COLUMNS = [
    "runtime_route",
    "timing_scope",
    "effective_thread_count",
    "sleeping_status",
    "fixture_semantic",
    "fixture_version",
    "static_box_count",
    "physics_settings",
    "build_settings",
    "requested_thread_count",
    "requested_worker_count",
    "requested_taskgraph_worker_count",
    "actual_taskgraph_worker_count",
    "effective_worker_count",
    "main_thread_participates",
    "unity_editor_version",
    "unity_physics_package_version",
    "entities_package_version",
    "burst_package_version",
    "collections_package_version",
    "build_target",
    "backend",
    "burst_enabled_state",
    "safety_check_state",
    "package_lock_sha256",
    "completed_step_count",
    "physics_elapsed_ms",
    "render_elapsed_ms",
    "present_wait_ms",
    "visual_validation_status",
    "proof_path"
]
COMPACT_RAW_RESULT_COLUMNS = [
    "body_count",
    "shape_count",
    "invalid_transform_count",
    "below_floor_count",
    "out_of_bounds_count",
    "case_status",
    "metric_status",
    "effective_thread_count",
    "effective_worker_count",
    "actual_taskgraph_worker_count",
    "completed_step_count",
    "physics_elapsed_ms",
    "render_elapsed_ms",
    "present_wait_ms",
    "visual_validation_status",
    "proof_path",
]
FIXTURE_VALIDATION_CURRENT_RUN = "current_run"
FIXTURE_VALIDATION_HISTORICAL_OR_CURRENT = "historical_or_current"

def fixture_metadata_state(row, fixture_contract):
    columns = fixture_contract.get("required_metadata_columns", [])
    present_count = sum(1 for column in columns if row.get(column, "") != "")
    if present_count == 0:
        return "absent"
    if present_count == len(columns):
        return "present"
    return "partial"

def validate_case_fixture_counts(row, case):
    try:
        body_count = int(row["body_count"])
        shape_count = int(row["shape_count"])
    except ValueError:
        return {"status": "invalid_result", "detail": f"fixture_count_parse engine={row.get('engine_id', '')}"}
    if body_count != int(case["body_count"]) or shape_count != int(case["shape_count"]):
        return {"status": "invalid_result", "detail": f"fixture_counts engine={row.get('engine_id', '')}"}
    return {"status": "ok", "detail": row.get("engine_id", "")}

def validate_box3d_open_container_fixture_row(row):
    return validate_native_cpp_open_container_fixture_row(row, {}, "box3d")

def validate_native_cpp_open_container_fixture_row(row, fixture_contract, expected_engine_id=None):
    engine_id = expected_engine_id or row.get("engine_id", "")
    expected_version = fixture_contract.get("engine_fixture_versions", {}).get(engine_id, "")
    if not expected_version:
        return {"status": "invalid_result", "detail": f"fixture_engine={engine_id}"}
    expected_strings = {
        "fixture_semantic": fixture_contract.get("semantic", ""),
        "fixture_version": expected_version,
    }
    for column, expected in expected_strings.items():
        if row.get(column, "") != expected:
            return {"status": "invalid_result", "detail": f"{column}={row.get(column, '')} expected={expected} engine={engine_id}"}
    try:
        static_box_count = int(row.get("static_box_count", ""))
        body_count = int(row["body_count"])
        shape_count = int(row["shape_count"])
    except ValueError:
        return {"status": "invalid_result", "detail": f"fixture_count_parse engine={engine_id}"}
    expected_static_box_count = int(fixture_contract.get("static_box_count", 0))
    expected_body_count = int(fixture_contract.get("body_count", 0))
    expected_shape_count = int(fixture_contract.get("shape_count", 0))
    if static_box_count != expected_static_box_count:
        return {"status": "invalid_result", "detail": f"static_box_count={static_box_count} expected={expected_static_box_count} engine={engine_id}"}
    if body_count != expected_body_count or shape_count != expected_shape_count:
        return {
            "status": "invalid_result",
            "detail": f"fixture_counts engine={engine_id} body_count={body_count} shape_count={shape_count} expected_body_count={expected_body_count} expected_shape_count={expected_shape_count}",
        }
    return {"status": "ok", "detail": expected_version}

def validate_fixture_row(row, case, fixture_validation_mode, fixture_contract=None):
    if fixture_contract is None:
        fixture_contract = {}
    engine_id = row.get("engine_id", "")
    if engine_id in fixture_contract.get("engine_fixture_versions", {}):
        metadata_state = fixture_metadata_state(row, fixture_contract)
        if fixture_validation_mode == FIXTURE_VALIDATION_CURRENT_RUN:
            return validate_native_cpp_open_container_fixture_row(row, fixture_contract, engine_id)
        if metadata_state == "present":
            return validate_native_cpp_open_container_fixture_row(row, fixture_contract, engine_id)
        if metadata_state == "partial":
            return {"status": "invalid_result", "detail": f"fixture_metadata=partial engine={engine_id}"}
    return validate_case_fixture_counts(row, case)

def validate_fixture_collection(rows, case, engines, fixture_contract=None):
    if fixture_contract is None:
        fixture_contract = {}
    if not fixture_contract:
        return {"status": "ok", "detail": case["id"]}
    engine_ids = [engine["id"] for engine in engines]
    open_engine_ids = []
    fixture_versions = fixture_contract.get("engine_fixture_versions", {})
    for row in rows:
        engine_id = row.get("engine_id", "")
        if (
            engine_id in fixture_versions
            and fixture_metadata_state(row, fixture_contract) == "present"
            and row.get("fixture_semantic", "") == fixture_contract.get("semantic", "")
            and row.get("fixture_version", "") == fixture_versions[engine_id]
            and engine_id not in open_engine_ids
        ):
            open_engine_ids.append(engine_id)
    if open_engine_ids:
        historical_engine_ids = [engine_id for engine_id in engine_ids if engine_id not in open_engine_ids]
        if historical_engine_ids:
            return {
                "status": "invalid_result",
                "detail": "mixed_fixture_semantics open_container_engines=" + "/".join(open_engine_ids) + " historical_engines=" + "/".join(historical_engine_ids),
            }
    return {"status": "ok", "detail": case["id"]}

def apply_normalized_metadata(row, engine_report_metadata_by_id, build_settings_rewrite_by_value):
    engine_id = row.get("engine_id", "")
    metadata = engine_report_metadata_by_id.get(engine_id, {})
    if not row.get("physics_settings", ""):
        row["physics_settings"] = metadata.get("physics_settings", "")
    build_settings = row.get("build_settings", "")
    if build_settings:
        row["build_settings"] = build_settings_rewrite_by_value.get(build_settings, build_settings)
    else:
        row["build_settings"] = metadata.get("build_settings", "")
    return row

def emit(component, status, detail):
    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow([component, status, detail])

def run_process(args, cwd=None, stdout_path=None, stderr_path=None, input_bytes=None, env=None):
    stdout_target = subprocess.PIPE
    stderr_target = subprocess.PIPE
    stdout_handle = None
    stderr_handle = None
    if stdout_path is not None:
        stdout_handle = open(stdout_path, "wb")
        stdout_target = stdout_handle
    if stderr_path is not None:
        stderr_handle = open(stderr_path, "wb")
        stderr_target = stderr_handle
    try:
        try:
            completed = subprocess.run(
                args,
                cwd=cwd,
                input=input_bytes,
                stdout=stdout_target,
                stderr=stderr_target,
                check=False,
                env=env
            )
        except FileNotFoundError as exc:
            return {
                "status": "run_failed",
                "code": 127,
                "stdout": b"",
                "stderr": str(exc).encode("utf-8", errors="replace")
            }
        stdout_data = completed.stdout if completed.stdout is not None else b""
        stderr_data = completed.stderr if completed.stderr is not None else b""
        return {
            "status": "ok" if completed.returncode == 0 else "run_failed",
            "code": completed.returncode,
            "stdout": stdout_data,
            "stderr": stderr_data
        }
    finally:
        if stdout_handle is not None:
            stdout_handle.close()
        if stderr_handle is not None:
            stderr_handle.close()

def write_process_logs(result, log_stem):
    configured_log_dir = os.environ.get("BENCH_LOG_DIR", "")
    if not configured_log_dir:
        return {
            "status": "not_configured",
            "detail": "BENCH_LOG_DIR",
            "stdout_log": "",
            "stderr_log": ""
        }
    if PROCESS_LOG_STEM_PATTERN.fullmatch(log_stem) is None:
        return {
            "status": "invalid_result",
            "detail": f"log_stem={log_stem}",
            "stdout_log": "",
            "stderr_log": ""
        }
    logs_root = (REPO_ROOT / "logs").resolve()
    invocation_log_dir = Path(configured_log_dir).resolve()
    try:
        invocation_log_dir.relative_to(logs_root)
    except ValueError:
        return {
            "status": "invalid_result",
            "detail": f"BENCH_LOG_DIR={normalized_path_text(invocation_log_dir)} allowed_root={normalized_path_text(logs_root)}",
            "stdout_log": "",
            "stderr_log": ""
        }
    process_log_dir = invocation_log_dir / "process"
    retained_paths = {"stdout_log": "", "stderr_log": ""}
    retained_streams = [
        (stream_name, result.get(stream_name, b"") or b"")
        for stream_name in ["stdout", "stderr"]
        if result.get(stream_name, b"")
    ]
    if not retained_streams:
        return {
            "status": "ok",
            "detail": f"path={metadata_path_text(process_log_dir)}",
            **retained_paths
        }
    try:
        process_log_dir.mkdir(parents=True, exist_ok=True)
        for stream_name, stream_data in retained_streams:
            stream_path = process_log_dir / f"{log_stem}.{stream_name}.log"
            stream_path.write_bytes(stream_data)
            retained_paths[f"{stream_name}_log"] = metadata_path_text(stream_path)
    except OSError as exc:
        return {
            "status": "write_failed",
            "detail": f"path={metadata_path_text(process_log_dir)} error={exc}",
            **retained_paths
        }
    return {
        "status": "ok",
        "detail": f"path={metadata_path_text(process_log_dir)}",
        **retained_paths
    }

def process_diagnostic_detail(result, command, cwd, log_status):
    detail_parts = [
        f"exit_code={result['code']}",
        f"executable={metadata_path_text(command[0])}",
        f"cwd={metadata_path_text(cwd)}"
    ]
    if log_status["stdout_log"]:
        detail_parts.append(f"stdout_log={log_status['stdout_log']}")
    if log_status["stderr_log"]:
        detail_parts.append(f"stderr_log={log_status['stderr_log']}")
    return " ".join(detail_parts)

def load_json(path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        emit("json_parse", "invalid_result", f"path={path} error={exc}")
        raise SystemExit(2)

def write_text_lf(path, text):
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

def repo_relative_status(value):
    candidate = Path(value)
    if candidate.is_absolute():
        return "absolute_path"
    if ".." in candidate.parts:
        return "path_escape"
    return "ok"

def engine_ref_path_status(value):
    candidate = Path(value)
    if candidate.is_absolute():
        return "ok"
    return repo_relative_status(value)

def repo_path(value):
    return REPO_ROOT / value

def config_path(value):
    candidate = Path(value)
    if candidate.is_absolute():
        return candidate
    return repo_path(value)

def local_path(value):
    text = str(value).strip().replace("\\", "/")
    if len(text) >= 2 and text[1] == ":":
        if os.name == "nt":
            return Path(text)
        drive = text[0].lower()
        suffix = text[2:].lstrip("/")
        return Path("/mnt") / drive / suffix
    return Path(value)

def normalized_path_text(path):
    return str(path).replace("\\", "/")

def metadata_path_text(path, root=None):
    candidate = Path(path)
    if root is not None:
        try:
            return normalized_path_text(candidate.relative_to(root))
        except ValueError:
            pass
    try:
        return normalized_path_text(candidate.relative_to(REPO_ROOT))
    except ValueError:
        return normalized_path_text(candidate)

def command_path(env_name, command_names):
    env_value = os.environ.get(env_name, "")
    if env_value:
        return env_value
    for command_name in command_names:
        command = shutil.which(command_name)
        if command:
            return command
    return ""

def platform_id():
    if os.name == "nt":
        return "windows"
    return "linux"

def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def find_executable(build_dir, names):
    for path in sorted(build_dir.rglob("*")):
        if path.is_file() and path.name in names:
            return path
    return None

def tool_version(command, version_arg):
    result = run_process([command, version_arg])
    if result["status"] != "ok":
        return command
    return result["stdout"].decode("utf-8", errors="replace").splitlines()[0]
