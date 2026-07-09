import os
import re

from .common import (
    DEFAULT_RELEASE_PLATFORM,
    RELEASE_DIR,
    RUNNABLE_ROUTE_STATUSES,
    emit,
    load_json,
    metadata_path_text,
    repo_path,
    repo_relative_status,
    sha256_file,
)
from .contracts import load_contracts, selected_engines

def release_platform_manifest_path(platform_name):
    return RELEASE_DIR / platform_name / "manifest.json"

def release_artifact_manifest_path(adapter, platform_name):
    release = adapter.get("release", {})
    if release.get("platform", "") != platform_name:
        return None
    manifest_path = release.get("artifact_manifest", "")
    if not manifest_path:
        return None
    return repo_path(manifest_path)

def release_host_route(contracts, platform_name):
    return contracts.get("release_platform_by_id", {}).get(platform_name, {}).get("host_route", "")

def load_release_platform_manifest(platform_name):
    manifest_path = release_platform_manifest_path(platform_name)
    if not manifest_path.is_file():
        return {"status": "tool_missing", "detail": metadata_path_text(manifest_path), "manifest": {}, "path": manifest_path}
    return {"status": "ok", "detail": metadata_path_text(manifest_path), "manifest": load_json(manifest_path), "path": manifest_path}

def visual_release_engine_ids(contracts):
    return contracts["engines_doc"].get("engine_sets", {}).get("default_visual_release", [])

def visual_release_route_status(adapter, platform_name):
    visual_release = adapter.get("visual", {}).get("release", {})
    if not isinstance(visual_release, dict) or not visual_release:
        return {"status": "missing", "detail": f"visual.release engine_platform={platform_name}"}
    release_platform = visual_release.get("platform", "")
    if release_platform and release_platform != platform_name:
        return {"status": "invalid_result", "detail": f"visual.release.platform={release_platform} expected={platform_name}"}
    route_status = visual_release.get("route_status", "")
    if route_status:
        return {"status": route_status, "detail": f"visual.release.route_status={route_status}"}
    visual = adapter.get("visual", {})
    if visual.get("route_status", "") in RUNNABLE_ROUTE_STATUSES:
        return {"status": "supported", "detail": "visual.release.capability"}
    return {"status": "missing", "detail": f"visual.release.capability platform={platform_name}"}

def missing_visual_release_artifact_status(engine, adapter, platform_name):
    release_status = visual_release_route_status(adapter, platform_name)
    if release_status["status"] == "invalid_result":
        return release_status
    if release_status["status"] not in RUNNABLE_ROUTE_STATUSES and release_status["status"] != "missing":
        return {
            "status": release_status["status"],
            "detail": f"public_visual_release_unavailable engine={engine['id']} status={release_status['status']}",
        }
    return {"status": "route_unavailable", "detail": f"missing_visual_release_capability engine={engine['id']}"}

FORBIDDEN_RELEASE_METADATA_PATH_MARKERS = [
    "." + "build/",
    "." + "generated/",
    "." + "external/",
    "tools/" + "local/",
]

ABSOLUTE_RELEASE_METADATA_PATH_PATTERNS = [
    ("windows_absolute_path", re.compile(r"(?<![A-Za-z])[A-Za-z]:[\\/]")),
    ("wsl_mount_path", re.compile(r"/mnt/[a-zA-Z]/")),
    ("home_path", re.compile(r"/home/")),
    ("users_path", re.compile(r"/Users/")),
]

def first_forbidden_release_metadata_path(value, location):
    if isinstance(value, dict):
        for key, item in value.items():
            hit = first_forbidden_release_metadata_path(item, f"{location}.{key}")
            if hit["status"] != "ok":
                return hit
        return {"status": "ok", "detail": ""}
    if isinstance(value, list):
        for index, item in enumerate(value):
            hit = first_forbidden_release_metadata_path(item, f"{location}[{index}]")
            if hit["status"] != "ok":
                return hit
        return {"status": "ok", "detail": ""}
    if isinstance(value, str):
        normalized = value.replace("\\", "/")
        for marker in FORBIDDEN_RELEASE_METADATA_PATH_MARKERS:
            if marker in normalized:
                return {"status": "invalid_result", "detail": f"{location} marker={marker}"}
        for pattern_name, pattern in ABSOLUTE_RELEASE_METADATA_PATH_PATTERNS:
            if pattern.search(value):
                return {"status": "invalid_result", "detail": f"{location} marker={pattern_name}"}
    return {"status": "ok", "detail": ""}

def verify_release_artifacts(contracts, platform_name, engines, emit_mode):
    platform_status = validate_release_platform_manifest(contracts, platform_name, engines)
    if emit_mode == "emit":
        emit("release_manifest", platform_status["status"], platform_status["detail"])
    if platform_status["status"] != "ok":
        return 2

    exit_code = 0
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        artifact_status = load_release_artifact_manifest(contracts, engine, adapter, platform_name)
        if emit_mode == "emit":
            emit(f"artifact_{engine['id']}", artifact_status["status"], artifact_status["detail"])
        if artifact_status["status"] != "ok":
            exit_code = 2
    return exit_code

def verify_visual_release_artifacts(contracts, platform_name, engines, emit_mode):
    platform_status = validate_visual_release_platform_manifest(contracts, platform_name, engines)
    if emit_mode == "emit":
        emit("visual_release_manifest", platform_status["status"], platform_status["detail"])
    if platform_status["status"] != "ok":
        return 2

    shared_status = load_shared_visual_renderer_manifest(contracts, platform_name)
    if emit_mode == "emit":
        emit("visual_shared_renderer", shared_status["status"], shared_status["detail"])
    if shared_status["status"] != "ok":
        return 2

    exit_code = 0
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        artifact_status = load_visual_engine_artifact_manifest(contracts, engine, adapter, platform_name)
        if emit_mode == "emit":
            emit(f"visual_engine_{engine['id']}", artifact_status["status"], artifact_status["detail"])
        if artifact_status["status"] != "ok":
            exit_code = 2
    return exit_code

def visual_release_artifacts_status(contracts, platform_name, engines):
    platform_status = validate_visual_release_platform_manifest(contracts, platform_name, engines)
    if platform_status["status"] != "ok":
        return {"component": "visual_release_manifest", "status": platform_status["status"], "detail": platform_status["detail"]}

    shared_status = load_shared_visual_renderer_manifest(contracts, platform_name)
    if shared_status["status"] != "ok":
        return {"component": "visual_shared_renderer", "status": shared_status["status"], "detail": shared_status["detail"]}

    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        artifact_status = load_visual_engine_artifact_manifest(contracts, engine, adapter, platform_name)
        if artifact_status["status"] != "ok":
            return {
                "component": f"visual_engine_{engine['id']}",
                "status": artifact_status["status"],
                "detail": artifact_status["detail"],
            }
    return {"component": "visual_release_artifacts", "status": "ok", "detail": platform_name}

def verify_visual_release_matrix(contracts, platform_name, emit_mode):
    manifest_status = load_release_platform_manifest(platform_name)
    if manifest_status["status"] != "ok":
        if emit_mode == "emit":
            emit("visual_release_matrix", manifest_status["status"], manifest_status["detail"])
        return 2

    visual_engine_ids = set(visual_release_engine_ids(contracts))
    exit_code = 0
    for engine in contracts["engines_doc"].get("engines", []):
        engine_id = engine.get("id", "")
        adapter = contracts["engine_sources"].get(engine_id, {})
        status = visual_release_matrix_engine_status(
            contracts,
            platform_name,
            engine,
            adapter,
            visual_engine_ids)
        if emit_mode == "emit":
            emit(f"visual_release_matrix_{engine_id}", status["status"], status["detail"])
        if status["status"] != "ok":
            exit_code = 2
    return exit_code

def verify_single_build_visual_contract(contracts, platform_name, emit_mode):
    status = single_build_visual_contract_status(contracts, platform_name)
    if emit_mode == "emit":
        emit("single_build_visual_contract", status["status"], status["detail"])
    if status["status"] != "ok":
        return 2
    return 0

def single_build_visual_contract_status(contracts, platform_name):
    manifest_status = load_release_platform_manifest(platform_name)
    if manifest_status["status"] != "ok":
        return manifest_status
    manifest = manifest_status["manifest"]
    visual = manifest.get("visual", {})
    if not isinstance(visual, dict) or not visual:
        return {"status": "invalid_result", "detail": f"missing_visual_release_manifest platform={platform_name}"}
    producer_entries = visual.get("producers", [])
    if producer_entries:
        return {"status": "invalid_result", "detail": f"split_visual_producer_index platform={platform_name}"}
    release_root = RELEASE_DIR / platform_name
    split_visual_manifest_name = "visual-" + "artifact-manifest.json"
    for path in sorted(release_root.glob("*/" + split_visual_manifest_name)):
        return {"status": "invalid_result", "detail": f"split_visual_artifact_manifest={metadata_path_text(path)}"}
    for engine in contracts["engines_doc"].get("engines", []):
        adapter = contracts["engine_sources"].get(engine.get("id", ""), {})
        status = single_build_visual_adapter_status(engine, adapter, platform_name)
        if status["status"] != "ok":
            return status
    return {"status": "ok", "detail": f"platform={platform_name}"}

def single_build_visual_adapter_status(engine, adapter, platform_name):
    engine_id = engine.get("id", "")
    visual = adapter.get("visual", {})
    for key in ["producer_" + "target", "producer_" + "source_dir", "producer_" + "build_route"]:
        if visual.get(key, ""):
            return {"status": "invalid_result", "detail": f"split_visual_metadata engine={engine_id} key=visual.{key}"}
    visual_release = visual.get("release", {})
    if isinstance(visual_release, dict):
        if visual_release.get("platform", "") and visual_release.get("platform", "") != platform_name:
            return {"status": "invalid_result", "detail": f"visual.release.platform={visual_release.get('platform', '')} expected={platform_name}"}
        release_manifest_key = "producer_" + "artifact_manifest"
        if visual_release.get(release_manifest_key, ""):
            return {"status": "invalid_result", "detail": f"split_visual_metadata engine={engine_id} key=visual.release.{release_manifest_key}"}
    return {"status": "ok", "detail": engine_id}

def visual_release_matrix_engine_status(contracts, platform_name, engine, adapter, visual_engine_ids):
    engine_id = engine["id"]
    in_visual_set = engine_id in visual_engine_ids
    visual = adapter.get("visual", {})
    case_supports = contracts.get("case_support_by_engine", {}).get(engine_id, {}).values()
    case_visual_runnable = any(entry.get("visual", "") in RUNNABLE_ROUTE_STATUSES for entry in case_supports)
    source_visual_runnable = (
        visual.get("route", "") == "shared_visual_renderer_cli"
        and visual.get("route_status", "") in RUNNABLE_ROUTE_STATUSES
    )
    artifact_status = load_release_artifact_manifest(contracts, engine, adapter, platform_name)
    visual_status = {"status": "route_unavailable", "detail": f"missing_visual_release_capability engine={engine_id}"}
    if artifact_status["status"] == "ok":
        visual_status = validate_visual_engine_capability(contracts, engine, adapter, artifact_status["manifest"], artifact_status["path"], platform_name)

    if visual_status["status"] == "ok":
        if not in_visual_set:
            if engine.get("support_status", "") == "experimental":
                return {"status": "ok", "detail": f"state=packaged_explicit_visual_release engine={engine_id}"}
            return {"status": "invalid_result", "detail": f"packaged_visual_not_in_visual_release engine={engine_id}"}
        return {"status": "ok", "detail": f"state=packaged_public_visual_release engine={engine_id}"}

    if in_visual_set:
        if artifact_status["status"] != "ok":
            return artifact_status
        return {"status": "invalid_result", "detail": f"visual_release_set_missing_capability engine={engine_id}"}
    if case_visual_runnable and source_visual_runnable:
        unavailable_status = missing_visual_release_artifact_status(engine, adapter, platform_name)
        if unavailable_status["detail"].startswith("public_visual_release_unavailable"):
            return {"status": "ok", "detail": f"state=source_private_visual_only engine={engine_id}"}
        return {"status": "invalid_result", "detail": f"ambiguous_source_visual_without_public_release engine={engine_id}"}
    return {"status": "ok", "detail": f"state=visual_release_unavailable engine={engine_id}"}

def load_visual_runtime_records(contracts, platform_name, engines):
    shared_status = load_shared_visual_renderer_manifest(contracts, platform_name)
    if shared_status["status"] != "ok":
        return {"status": shared_status["status"], "detail": shared_status["detail"], "renderer": {}, "engines": {}}
    engine_records = {}
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        visual_status = load_visual_engine_artifact_manifest(contracts, engine, adapter, platform_name)
        if visual_status["status"] != "ok":
            return {"status": visual_status["status"], "detail": visual_status["detail"], "renderer": {}, "engines": {}}
        engine_records[engine["id"]] = visual_status
    return {"status": "ok", "detail": platform_name, "renderer": shared_status, "engines": engine_records}

def validate_release_platform_manifest(contracts, platform_name, engines):
    manifest_path = release_platform_manifest_path(platform_name)
    if not manifest_path.is_file():
        for engine in engines:
            artifact_manifest_path = release_artifact_manifest_path(contracts["engine_sources"][engine["id"]], platform_name)
            if artifact_manifest_path is None:
                return {"status": "invalid_result", "detail": f"missing_artifact_manifest engine={engine['id']}"}
        return {"status": "ok", "detail": f"derived_from_config platform={platform_name}"}
    manifest = load_json(manifest_path)
    private_status = first_forbidden_release_metadata_path(manifest, metadata_path_text(manifest_path))
    if private_status["status"] != "ok":
        return private_status
    if manifest.get("schema_version") != 1:
        return {"status": "invalid_result", "detail": f"schema_version={manifest.get('schema_version')}"}
    if manifest.get("platform", "") != platform_name:
        return {"status": "invalid_result", "detail": f"platform={manifest.get('platform', '')}"}
    if manifest.get("host_route", "") != release_host_route(contracts, platform_name):
        return {"status": "invalid_result", "detail": f"host_route={manifest.get('host_route', '')}"}
    manifest_engines = {
        entry.get("engine_id", ""): entry.get("artifact_manifest", "")
        for entry in manifest.get("engines", [])
    }
    release_engine_ids = set(contracts["engines_doc"].get("engine_sets", {}).get("default_release", []))
    for engine_id, actual_path in manifest_engines.items():
        if engine_id not in release_engine_ids:
            return {"status": "invalid_result", "detail": f"unexpected_release_engine={engine_id}"}
        artifact_manifest_path = release_artifact_manifest_path(contracts["engine_sources"][engine_id], platform_name)
        if artifact_manifest_path is None:
            return {"status": "invalid_result", "detail": f"missing_artifact_manifest engine={engine_id}"}
        expected_path = metadata_path_text(artifact_manifest_path)
        if actual_path != expected_path:
            return {"status": "invalid_result", "detail": f"engine={engine_id} artifact_manifest={actual_path}"}
    for engine in engines:
        artifact_manifest_path = release_artifact_manifest_path(contracts["engine_sources"][engine["id"]], platform_name)
        if artifact_manifest_path is None:
            return {"status": "invalid_result", "detail": f"missing_artifact_manifest engine={engine['id']}"}
        expected_path = metadata_path_text(artifact_manifest_path)
        actual_path = manifest_engines.get(engine["id"], "")
        if not actual_path and engine["id"] not in release_engine_ids and artifact_manifest_path.is_file():
            continue
        if actual_path != expected_path:
            return {"status": "invalid_result", "detail": f"engine={engine['id']} artifact_manifest={actual_path}"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_visual_release_platform_manifest(contracts, platform_name, engines):
    platform_status = validate_release_platform_manifest(contracts, platform_name, engines)
    if platform_status["status"] != "ok":
        return platform_status
    manifest_status = load_release_platform_manifest(platform_name)
    if manifest_status["status"] != "ok":
        return manifest_status
    manifest = manifest_status["manifest"]
    private_status = first_forbidden_release_metadata_path(manifest, metadata_path_text(manifest_status["path"]))
    if private_status["status"] != "ok":
        return private_status
    visual = manifest.get("visual", {})
    if not isinstance(visual, dict) or not visual:
        return {"status": "invalid_result", "detail": f"missing_visual_release_manifest platform={platform_name}"}
    shared_manifest = visual.get("shared_renderer_artifact_manifest", "")
    if not shared_manifest:
        return {"status": "invalid_result", "detail": f"missing_shared_visual_renderer platform={platform_name}"}
    if repo_relative_status(shared_manifest) != "ok":
        return {"status": "invalid_result", "detail": f"shared_visual_renderer_manifest={shared_manifest}"}
    if visual.get("producers", []):
        return {"status": "invalid_result", "detail": f"split_visual_producer_index platform={platform_name}"}
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        artifact_status = load_visual_engine_artifact_manifest(contracts, engine, adapter, platform_name)
        if artifact_status["status"] != "ok":
            return artifact_status
    return {"status": "ok", "detail": metadata_path_text(manifest_status["path"])}

def load_release_artifact_manifest(contracts, engine, adapter, platform_name):
    manifest_path = release_artifact_manifest_path(adapter, platform_name)
    if manifest_path is None:
        return {"status": "tool_missing", "detail": f"engine={engine['id']} platform={platform_name}"}
    if not manifest_path.is_file():
        return {"status": "tool_missing", "detail": metadata_path_text(manifest_path)}
    manifest = load_json(manifest_path)
    validation_status = validate_release_artifact_manifest(contracts, engine, adapter, manifest, manifest_path, platform_name)
    if validation_status["status"] != "ok":
        return validation_status
    return {"status": "ok", "detail": metadata_path_text(manifest_path), "manifest": manifest, "path": manifest_path}

def load_shared_visual_renderer_manifest(contracts, platform_name):
    platform_status = load_release_platform_manifest(platform_name)
    if platform_status["status"] != "ok":
        return platform_status
    visual = platform_status["manifest"].get("visual", {})
    manifest_text = visual.get("shared_renderer_artifact_manifest", "") if isinstance(visual, dict) else ""
    if not manifest_text:
        return {"status": "tool_missing", "detail": f"shared_renderer_artifact_manifest platform={platform_name}", "manifest": {}, "path": release_platform_manifest_path(platform_name)}
    if repo_relative_status(manifest_text) != "ok":
        return {"status": "invalid_result", "detail": f"shared_renderer_artifact_manifest={manifest_text}", "manifest": {}, "path": repo_path(manifest_text)}
    manifest_path = repo_path(manifest_text)
    if not manifest_path.is_file():
        return {"status": "tool_missing", "detail": metadata_path_text(manifest_path), "manifest": {}, "path": manifest_path}
    manifest = load_json(manifest_path)
    validation_status = validate_shared_visual_renderer_manifest(contracts, manifest, manifest_path, platform_name)
    if validation_status["status"] != "ok":
        return validation_status
    return {"status": "ok", "detail": metadata_path_text(manifest_path), "manifest": manifest, "path": manifest_path}

def load_visual_engine_artifact_manifest(contracts, engine, adapter, platform_name):
    artifact_status = load_release_artifact_manifest(contracts, engine, adapter, platform_name)
    if artifact_status["status"] != "ok":
        return artifact_status
    manifest = artifact_status["manifest"]
    manifest_path = artifact_status["path"]
    validation_status = validate_visual_engine_capability(contracts, engine, adapter, manifest, manifest_path, platform_name)
    if validation_status["status"] != "ok":
        return validation_status
    return {"status": "ok", "detail": metadata_path_text(manifest_path), "manifest": manifest, "path": manifest_path}

def validate_release_artifact_manifest(contracts, engine, adapter, manifest, manifest_path, platform_name):
    private_status = first_forbidden_release_metadata_path(manifest, metadata_path_text(manifest_path))
    if private_status["status"] != "ok":
        return private_status
    required_keys = [
        "schema_version",
        "engine_id",
        "platform",
        "host_route",
        "source_version",
        "executable_path",
        "launcher",
        "required_sidecars",
        "compiler_runtime",
        "files",
        "license_id",
        "notice_license_reference"
    ]
    for key in required_keys:
        if key not in manifest:
            return {"status": "invalid_result", "detail": f"missing_key={key} manifest={metadata_path_text(manifest_path)}"}
    if manifest["schema_version"] != 1:
        return {"status": "invalid_result", "detail": f"schema_version={manifest['schema_version']} manifest={metadata_path_text(manifest_path)}"}
    if manifest["engine_id"] != engine["id"]:
        return {"status": "invalid_result", "detail": f"engine_id={manifest['engine_id']} expected={engine['id']}"}
    if manifest["platform"] != platform_name:
        return {"status": "invalid_result", "detail": f"platform={manifest['platform']} expected={platform_name}"}
    expected_host_route = release_host_route(contracts, platform_name)
    if manifest["host_route"] != expected_host_route:
        return {"status": "invalid_result", "detail": f"host_route={manifest['host_route']} expected={expected_host_route}"}
    if manifest["host_route"] not in adapter.get("supported_platforms", []):
        return {"status": "invalid_result", "detail": f"unsupported_artifact_host_route={manifest['host_route']} engine={engine['id']}"}
    if manifest["source_version"] != engine["commit"]:
        return {"status": "invalid_result", "detail": f"source_version={manifest['source_version']} expected={engine['commit']} engine={engine['id']}"}
    expected_manifest_path = release_artifact_manifest_path(adapter, platform_name)
    if expected_manifest_path is None or metadata_path_text(expected_manifest_path) != metadata_path_text(manifest_path):
        return {"status": "invalid_result", "detail": f"artifact_manifest_path={metadata_path_text(manifest_path)} engine={engine['id']}"}
    if manifest["launcher"] not in ["direct", "dotnet"]:
        return {"status": "invalid_result", "detail": f"launcher={manifest['launcher']} engine={engine['id']}"}
    compiler_status = validate_compiler_runtime(manifest.get("compiler_runtime", {}), manifest_path)
    if compiler_status["status"] != "ok":
        return compiler_status
    package_status = validate_package_metadata(manifest.get("package_metadata", {}), manifest_path)
    if package_status["status"] != "ok":
        return package_status
    if engine["id"] == "bepuphysics2":
        bepu_status = validate_bepu_release_manifest(manifest, manifest_path)
        if bepu_status["status"] != "ok":
            return bepu_status
    if engine["id"] == "rapier3d":
        rapier_status = validate_rapier_release_manifest(manifest, manifest_path)
        if rapier_status["status"] != "ok":
            return rapier_status
    if engine["id"] == "avian3d":
        avian_status = validate_avian_release_manifest(manifest, manifest_path)
        if avian_status["status"] != "ok":
            return avian_status
    executable_status = validate_release_artifact_path(manifest["executable_path"], manifest_path)
    if executable_status["status"] != "ok":
        return executable_status
    file_paths = set()
    for entry in manifest["files"]:
        file_status = validate_release_file_entry(entry, manifest_path)
        if file_status["status"] != "ok":
            return file_status
        file_paths.add(entry["path"])
    if manifest["executable_path"] not in file_paths:
        return {"status": "invalid_result", "detail": f"missing_executable_hash={manifest['executable_path']}"}
    for sidecar in manifest["required_sidecars"]:
        sidecar_status = validate_release_artifact_path(sidecar, manifest_path)
        if sidecar_status["status"] != "ok":
            return sidecar_status
        if sidecar not in file_paths:
            return {"status": "invalid_result", "detail": f"missing_sidecar_hash={sidecar}"}
    if engine["id"] in {"physx34", "nvidia_physx34", "nvidia_physx5"}:
        notice_status = validate_physx_notice(manifest)
        if notice_status["status"] != "ok":
            return notice_status
    if engine["id"] == "rapier3d":
        notice_status = validate_rapier_notice(manifest)
        if notice_status["status"] != "ok":
            return notice_status
    if engine["id"] == "avian3d":
        notice_status = validate_avian_notice(manifest)
        if notice_status["status"] != "ok":
            return notice_status
    environment_status = validate_release_environment(manifest.get("environment", {}), manifest_path)
    if environment_status["status"] != "ok":
        return environment_status
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_shared_visual_renderer_manifest(contracts, manifest, manifest_path, platform_name):
    private_status = first_forbidden_release_metadata_path(manifest, metadata_path_text(manifest_path))
    if private_status["status"] != "ok":
        return private_status
    required_keys = [
        "schema_version",
        "artifact_role",
        "platform",
        "host_route",
        "executable_path",
        "launcher",
        "required_sidecars",
        "compiler_runtime",
        "files",
        "license_id",
        "notice_license_reference"
    ]
    for key in required_keys:
        if key not in manifest:
            return {"status": "invalid_result", "detail": f"missing_key={key} manifest={metadata_path_text(manifest_path)}"}
    if manifest["schema_version"] != 1:
        return {"status": "invalid_result", "detail": f"schema_version={manifest['schema_version']} manifest={metadata_path_text(manifest_path)}"}
    if manifest["artifact_role"] != "shared_visual_renderer":
        return {"status": "invalid_result", "detail": f"artifact_role={manifest['artifact_role']} manifest={metadata_path_text(manifest_path)}"}
    if manifest["platform"] != platform_name:
        return {"status": "invalid_result", "detail": f"platform={manifest['platform']} expected={platform_name}"}
    expected_host_route = release_host_route(contracts, platform_name)
    if manifest["host_route"] != expected_host_route:
        return {"status": "invalid_result", "detail": f"host_route={manifest['host_route']} expected={expected_host_route}"}
    if manifest["launcher"] != "direct":
        return {"status": "invalid_result", "detail": f"launcher={manifest['launcher']} manifest={metadata_path_text(manifest_path)}"}
    compiler_status = validate_compiler_runtime(manifest.get("compiler_runtime", {}), manifest_path)
    if compiler_status["status"] != "ok":
        return compiler_status
    return validate_release_files_payload(manifest, manifest_path)

def validate_visual_engine_capability(contracts, engine, adapter, manifest, manifest_path, platform_name):
    visual = adapter.get("visual", {})
    if visual.get("route", "") != "shared_visual_renderer_cli" or visual.get("route_status", "") not in RUNNABLE_ROUTE_STATUSES:
        return {"status": "route_unavailable", "detail": f"engine={engine['id']} visual_route={visual.get('route', '')} status={visual.get('route_status', '')}"}
    artifact_visual = manifest.get("visual", {})
    if not isinstance(artifact_visual, dict) or not artifact_visual:
        return missing_visual_release_artifact_status(engine, adapter, platform_name)
    release_status = artifact_visual.get("route_status", "")
    if release_status not in RUNNABLE_ROUTE_STATUSES:
        if release_status:
            return {"status": release_status, "detail": f"public_visual_release_unavailable engine={engine['id']} status={release_status}"}
        return missing_visual_release_artifact_status(engine, adapter, platform_name)
    if artifact_visual.get("transport", "") != visual.get("transport", ""):
        return {"status": "invalid_result", "detail": f"visual.transport={artifact_visual.get('transport', '')} expected={visual.get('transport', '')} engine={engine['id']}"}
    if int(artifact_visual.get("protocol_version", 0)) != int(visual.get("protocol_version", 0)):
        return {"status": "invalid_result", "detail": f"visual.protocol_version={artifact_visual.get('protocol_version', '')} expected={visual.get('protocol_version', '')} engine={engine['id']}"}
    if artifact_visual.get("producer_mode_arg", "") != visual.get("producer_mode_arg", ""):
        return {"status": "invalid_result", "detail": f"visual.producer_mode_arg={artifact_visual.get('producer_mode_arg', '')} engine={engine['id']}"}
    prefix_args = artifact_visual.get("producer_prefix_args", [])
    if prefix_args and not isinstance(prefix_args, list):
        return {"status": "invalid_result", "detail": f"visual.producer_prefix_args engine={engine['id']}"}
    if isinstance(prefix_args, list):
        for prefix_arg in prefix_args:
            if not isinstance(prefix_arg, str) or not prefix_arg:
                return {"status": "invalid_result", "detail": f"visual.producer_prefix_arg engine={engine['id']}"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_release_files_payload(manifest, manifest_path):
    executable_status = validate_release_artifact_path(manifest["executable_path"], manifest_path)
    if executable_status["status"] != "ok":
        return executable_status
    file_paths = set()
    for entry in manifest["files"]:
        file_status = validate_release_file_entry(entry, manifest_path)
        if file_status["status"] != "ok":
            return file_status
        file_paths.add(entry["path"])
    if manifest["executable_path"] not in file_paths:
        return {"status": "invalid_result", "detail": f"missing_executable_hash={manifest['executable_path']}"}
    for sidecar in manifest["required_sidecars"]:
        sidecar_status = validate_release_artifact_path(sidecar, manifest_path)
        if sidecar_status["status"] != "ok":
            return sidecar_status
        if sidecar not in file_paths:
            return {"status": "invalid_result", "detail": f"missing_sidecar_hash={sidecar}"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_compiler_runtime(compiler_runtime, manifest_path):
    if not isinstance(compiler_runtime, dict):
        return {"status": "invalid_result", "detail": f"compiler_runtime_type manifest={metadata_path_text(manifest_path)}"}
    for key in ["toolchain_id", "compiler_detail", "runtime_detail"]:
        value = compiler_runtime.get(key, "")
        if not isinstance(value, str) or not value.strip():
            return {"status": "invalid_result", "detail": f"compiler_runtime.{key} manifest={metadata_path_text(manifest_path)}"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_package_metadata(package_metadata, manifest_path):
    if not isinstance(package_metadata, dict):
        return {"status": "invalid_result", "detail": f"package_metadata_type manifest={metadata_path_text(manifest_path)}"}
    for key in package_metadata:
        if not isinstance(key, str) or not key.strip():
            return {"status": "invalid_result", "detail": f"package_metadata_key manifest={metadata_path_text(manifest_path)}"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_bepu_release_manifest(manifest, manifest_path):
    compiler_runtime = manifest.get("compiler_runtime", {})
    package_metadata = manifest.get("package_metadata", {})
    if manifest.get("launcher", "") != "direct":
        return {"status": "invalid_result", "detail": f"bepu_launcher={manifest.get('launcher', '')}"}
    if not manifest.get("executable_path", "").endswith(".exe"):
        return {"status": "invalid_result", "detail": f"bepu_executable_path={manifest.get('executable_path', '')}"}
    if compiler_runtime.get("toolchain_id", "") != "dotnet10_release_no_profiling_self_contained":
        return {"status": "invalid_result", "detail": f"bepu_toolchain_id={compiler_runtime.get('toolchain_id', '')}"}
    expected = {
        "package_mode": "self_contained_apphost",
        "runtime_target_framework": "net10.0",
        "runner_configuration": "Release",
        "bepu_physics_configuration": "ReleaseNoProfiling",
        "bepu_utilities_configuration": "Release"
    }
    for key, value in expected.items():
        if package_metadata.get(key, "") != value:
            return {"status": "invalid_result", "detail": f"bepu_package_metadata.{key}={package_metadata.get(key, '')}"}
    build_label = package_metadata.get("build_label", "")
    forbidden_profile_symbol = "PRO" + "FILE"
    if "ReleaseNoProfiling" not in build_label or forbidden_profile_symbol in build_label:
        return {"status": "invalid_result", "detail": f"bepu_build_label={build_label}"}
    runtime_detail = compiler_runtime.get("runtime_detail", "")
    if "net10.0" not in runtime_detail:
        return {"status": "invalid_result", "detail": f"bepu_runtime_detail={runtime_detail}"}
    if "self-contained" not in runtime_detail or "no installed .NET 10 runtime required" not in runtime_detail:
        return {"status": "invalid_result", "detail": f"bepu_runtime_detail={runtime_detail}"}
    if "rollForward" in runtime_detail or "runtime_roll_forward" in package_metadata:
        return {"status": "invalid_result", "detail": "bepu_runtime_roll_forward_present"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_rapier_release_manifest(manifest, manifest_path):
    compiler_runtime = manifest.get("compiler_runtime", {})
    package_metadata = manifest.get("package_metadata", {})
    if manifest.get("launcher", "") != "direct":
        return {"status": "invalid_result", "detail": f"rapier_launcher={manifest.get('launcher', '')}"}
    if not manifest.get("executable_path", "").endswith(".exe"):
        return {"status": "invalid_result", "detail": f"rapier_executable_path={manifest.get('executable_path', '')}"}
    if compiler_runtime.get("toolchain_id", "") != "rust_1_89_msvc_cargo_release":
        return {"status": "invalid_result", "detail": f"rapier_toolchain_id={compiler_runtime.get('toolchain_id', '')}"}
    expected = {
        "rapier_version": "0.34.0",
        "cargo_profile": "release",
        "cargo_features": "parallel"
    }
    for key, value in expected.items():
        if package_metadata.get(key, "") != value:
            return {"status": "invalid_result", "detail": f"rapier_package_metadata.{key}={package_metadata.get(key, '')}"}
    for key in ["rust_version", "cargo_version", "cargo_lock_sha256", "runner_manifest_sha256"]:
        value = package_metadata.get(key, "")
        if not isinstance(value, str) or not value.strip():
            return {"status": "invalid_result", "detail": f"rapier_package_metadata.{key}"}
    for key in ["cargo_lock_sha256", "runner_manifest_sha256"]:
        if not re.fullmatch(r"[0-9a-f]{64}", package_metadata.get(key, "")):
            return {"status": "invalid_result", "detail": f"rapier_package_metadata.{key}={package_metadata.get(key, '')}"}
    if manifest.get("required_sidecars", []) != []:
        return {"status": "invalid_result", "detail": "rapier_required_sidecars_present"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_avian_release_manifest(manifest, manifest_path):
    compiler_runtime = manifest.get("compiler_runtime", {})
    package_metadata = manifest.get("package_metadata", {})
    if manifest.get("launcher", "") != "direct":
        return {"status": "invalid_result", "detail": f"avian_launcher={manifest.get('launcher', '')}"}
    if not manifest.get("executable_path", "").endswith(".exe"):
        return {"status": "invalid_result", "detail": f"avian_executable_path={manifest.get('executable_path', '')}"}
    if compiler_runtime.get("toolchain_id", "") != "rust_1_95_msvc_cargo_release":
        return {"status": "invalid_result", "detail": f"avian_toolchain_id={compiler_runtime.get('toolchain_id', '')}"}
    expected = {
        "avian_version": "0.8.0-dev",
        "bevy_version": "0.19.0",
        "cargo_profile": "release",
        "cargo_features": "3d,f32,parry-f32,parallel,simd"
    }
    for key, value in expected.items():
        if package_metadata.get(key, "") != value:
            return {"status": "invalid_result", "detail": f"avian_package_metadata.{key}={package_metadata.get(key, '')}"}
    for key in ["rust_version", "cargo_version", "cargo_lock_sha256", "runner_manifest_sha256"]:
        value = package_metadata.get(key, "")
        if not isinstance(value, str) or not value.strip():
            return {"status": "invalid_result", "detail": f"avian_package_metadata.{key}"}
    for key in ["cargo_lock_sha256", "runner_manifest_sha256"]:
        if not re.fullmatch(r"[0-9a-f]{64}", package_metadata.get(key, "")):
            return {"status": "invalid_result", "detail": f"avian_package_metadata.{key}={package_metadata.get(key, '')}"}
    if manifest.get("required_sidecars", []) != []:
        return {"status": "invalid_result", "detail": "avian_required_sidecars_present"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_release_artifact_path(path_text, manifest_path):
    status = repo_relative_status(path_text)
    if status != "ok":
        return {"status": "invalid_result", "detail": f"path={path_text} reason={status}"}
    path = repo_path(path_text)
    if not path.is_file():
        return {"status": "tool_missing", "detail": f"missing_file={path_text} manifest={metadata_path_text(manifest_path)}"}
    return {"status": "ok", "detail": path_text}

def validate_release_file_entry(entry, manifest_path):
    for key in ["path", "sha256", "role"]:
        if key not in entry:
            return {"status": "invalid_result", "detail": f"missing_file_key={key} manifest={metadata_path_text(manifest_path)}"}
    path_status = validate_release_artifact_path(entry["path"], manifest_path)
    if path_status["status"] != "ok":
        return path_status
    actual_hash = sha256_file(repo_path(entry["path"]))
    if actual_hash != entry["sha256"]:
        return {"status": "invalid_result", "detail": f"hash_mismatch={entry['path']}"}
    return {"status": "ok", "detail": entry["path"]}

def validate_release_environment(environment, manifest_path):
    if not isinstance(environment, dict):
        return {"status": "invalid_result", "detail": f"environment_type manifest={metadata_path_text(manifest_path)}"}
    for key, value in environment.items():
        if not isinstance(key, str) or not isinstance(value, str):
            return {"status": "invalid_result", "detail": f"environment_entry manifest={metadata_path_text(manifest_path)}"}
        if not re.fullmatch(r"[A-Z0-9_]+", key):
            return {"status": "invalid_result", "detail": f"environment_key={key}"}
    return {"status": "ok", "detail": metadata_path_text(manifest_path)}

def validate_physx_notice(manifest):
    notice_path = repo_path("SOURCES.md")
    if not notice_path.is_file():
        return {"status": "tool_missing", "detail": "SOURCES.md"}
    notice_text = notice_path.read_text(encoding="utf-8")
    engine_id = manifest.get("engine_id", "")
    if engine_id == "physx34" and ("PhysX 3.4" not in notice_text or "BSD-3-Clause" not in notice_text):
        return {"status": "invalid_result", "detail": "missing_physx34_bsd3_notice"}
    if engine_id in {"nvidia_physx34", "nvidia_physx5"} and ("Official NVIDIA PhysX" not in notice_text or "BSD-3-Clause" not in notice_text):
        return {"status": "invalid_result", "detail": "missing_official_nvidia_physx_bsd3_notice"}
    if manifest.get("license_id", "") != "BSD-3-Clause":
        return {"status": "invalid_result", "detail": f"physx_license_id={manifest.get('license_id', '')}"}
    return {"status": "ok", "detail": "SOURCES.md"}

def validate_rapier_notice(manifest):
    notice_path = repo_path("SOURCES.md")
    if not notice_path.is_file():
        return {"status": "tool_missing", "detail": "SOURCES.md"}
    notice_text = notice_path.read_text(encoding="utf-8")
    if "Rapier3D" not in notice_text or "Apache-2.0" not in notice_text:
        return {"status": "invalid_result", "detail": "missing_rapier3d_apache2_notice"}
    if manifest.get("license_id", "") != "Apache-2.0":
        return {"status": "invalid_result", "detail": f"rapier_license_id={manifest.get('license_id', '')}"}
    return {"status": "ok", "detail": "SOURCES.md"}

def validate_avian_notice(manifest):
    notice_path = repo_path("SOURCES.md")
    if not notice_path.is_file():
        return {"status": "tool_missing", "detail": "SOURCES.md"}
    notice_text = notice_path.read_text(encoding="utf-8")
    if "Avian3D" not in notice_text or "MIT OR Apache-2.0" not in notice_text:
        return {"status": "invalid_result", "detail": "missing_avian3d_mit_apache_notice"}
    if manifest.get("license_id", "") != "MIT OR Apache-2.0":
        return {"status": "invalid_result", "detail": f"avian_license_id={manifest.get('license_id', '')}"}
    return {"status": "ok", "detail": "SOURCES.md"}

def load_runtime_records(contracts, engines, release_platform):
    records = {}
    for engine in engines:
        adapter = contracts["engine_sources"][engine["id"]]
        artifact_status = load_release_artifact_manifest(contracts, engine, adapter, release_platform)
        if artifact_status["status"] != "ok":
            return {"status": artifact_status["status"], "detail": artifact_status["detail"], "records": {}}
        records[engine["id"]] = artifact_status
    return {"status": "ok", "detail": release_platform, "records": records}

def release_run_environment(artifact):
    if artifact is None:
        return None
    environment = artifact.get("environment", {})
    if not environment:
        return None
    run_env = os.environ.copy()
    for key, value in environment.items():
        run_env[key] = value
    return run_env

def artifact_run_metadata(engine, artifact, manifest_path):
    compiler_runtime = artifact.get("compiler_runtime", {})
    package_metadata = artifact.get("package_metadata", {})
    metadata = dict(package_metadata)
    metadata["engine_ref"] = artifact.get("source_version", engine["commit"])
    metadata["host_route"] = artifact.get("host_route", "")
    metadata["toolchain_id"] = compiler_runtime.get("toolchain_id", "")
    metadata["compiler_detail"] = compiler_runtime.get("compiler_detail", "")
    metadata["binary_release_platform"] = artifact.get("platform", "")
    metadata["release_artifact_manifest"] = metadata_path_text(manifest_path)
    metadata["runtime_environment"] = compiler_runtime.get("runtime_detail", "")
    return metadata

def visual_artifact_run_metadata(engine, artifact, manifest_path):
    compiler_runtime = artifact.get("compiler_runtime", {})
    package_metadata = artifact.get("package_metadata", {})
    metadata = dict(package_metadata)
    metadata["engine_ref"] = artifact.get("source_version", engine["commit"])
    metadata["host_route"] = artifact.get("host_route", "")
    metadata["visual_toolchain_id"] = compiler_runtime.get("toolchain_id", "")
    metadata["visual_compiler_detail"] = compiler_runtime.get("compiler_detail", "")
    metadata["visual_release_artifact_manifest"] = metadata_path_text(manifest_path)
    metadata["visual_runtime_environment"] = compiler_runtime.get("runtime_detail", "")
    return metadata

def validate_result_artifact_snapshot(contracts, engine_entry, source_dir):
    engine_id = engine_entry.get("id", "")
    artifact_path_text = engine_entry.get("release_artifact_manifest", "")
    artifact_hash = engine_entry.get("artifact_manifest_sha256", "")
    if not artifact_path_text:
        return {"status": "invalid_result", "detail": f"missing_artifact_manifest engine={engine_id} source={source_dir}"}
    if not artifact_hash:
        return {"status": "invalid_result", "detail": f"missing_artifact_manifest_hash engine={engine_id} source={source_dir}"}
    path_status = repo_relative_status(artifact_path_text)
    if path_status != "ok":
        return {"status": "invalid_result", "detail": f"artifact_manifest_path={artifact_path_text} reason={path_status} source={source_dir}"}
    artifact_path = repo_path(artifact_path_text)
    if not artifact_path.is_file():
        return {"status": "invalid_result", "detail": f"missing_artifact_manifest={artifact_path_text} source={source_dir}"}
    actual_hash = sha256_file(artifact_path)
    if actual_hash != artifact_hash:
        return {"status": "invalid_result", "detail": f"artifact_manifest_hash engine={engine_id} source={source_dir}"}
    artifact_manifest = load_json(artifact_path)
    engine = contracts["engine_by_id"][engine_id]
    if artifact_manifest.get("engine_id", "") != engine_id:
        return {"status": "invalid_result", "detail": f"artifact_engine_id={artifact_manifest.get('engine_id', '')} expected={engine_id} source={source_dir}"}
    if artifact_manifest.get("source_version", "") != engine["commit"]:
        return {"status": "invalid_result", "detail": f"artifact_source_version={artifact_manifest.get('source_version', '')} expected={engine['commit']} source={source_dir}"}
    return {"status": "ok", "detail": artifact_path_text}

def validate_result_visual_artifact_snapshot(contracts, manifest, source_dir):
    if manifest.get("benchmark_mode", "") != "visualized_release":
        return {"status": "ok", "detail": "not_visualized_release"}
    renderer_path_text = manifest.get("visual_shared_renderer_artifact_manifest", "")
    renderer_hash = manifest.get("visual_shared_renderer_artifact_manifest_sha256", "")
    renderer_status = validate_result_manifest_hash(renderer_path_text, renderer_hash, source_dir, "shared_visual_renderer")
    if renderer_status["status"] != "ok":
        return renderer_status
    for engine_entry in manifest.get("engines", []):
        engine_id = engine_entry.get("id", "")
        if engine_id not in contracts["engine_by_id"]:
            return {"status": "invalid_result", "detail": f"visual_engine={engine_id} source={source_dir}"}
        artifact_path_text = engine_entry.get("release_artifact_manifest", "")
        artifact_manifest = load_json(repo_path(artifact_path_text))
        adapter = contracts["engine_sources"].get(engine_id, {})
        visual_status = validate_visual_engine_capability(contracts, contracts["engine_by_id"][engine_id], adapter, artifact_manifest, repo_path(artifact_path_text), artifact_manifest.get("platform", ""))
        if visual_status["status"] != "ok":
            return {"status": visual_status["status"], "detail": f"{visual_status['detail']} source={source_dir}"}
    return {"status": "ok", "detail": metadata_path_text(source_dir)}

def validate_result_manifest_hash(path_text, artifact_hash, source_dir, owner):
    if not path_text:
        return {"status": "invalid_result", "detail": f"missing_artifact_manifest owner={owner} source={source_dir}"}
    if not artifact_hash:
        return {"status": "invalid_result", "detail": f"missing_artifact_manifest_hash owner={owner} source={source_dir}"}
    path_status = repo_relative_status(path_text)
    if path_status != "ok":
        return {"status": "invalid_result", "detail": f"artifact_manifest_path={path_text} reason={path_status} owner={owner} source={source_dir}"}
    artifact_path = repo_path(path_text)
    if not artifact_path.is_file():
        return {"status": "invalid_result", "detail": f"missing_artifact_manifest={path_text} owner={owner} source={source_dir}"}
    actual_hash = sha256_file(artifact_path)
    if actual_hash != artifact_hash:
        return {"status": "invalid_result", "detail": f"artifact_manifest_hash owner={owner} source={source_dir}"}
    return {"status": "ok", "detail": path_text}
