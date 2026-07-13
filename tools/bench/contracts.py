import os
import re

from .common import (
    ALLOWED_PLACEHOLDERS,
    CASES_PATH,
    DEFAULT_RELEASE_PLATFORM,
    ENGINES_PATH,
    PLACEHOLDER_PATTERN,
    REPORTS_PATH,
    REQUIRED_NORMALIZED_COLUMNS,
    RUNNABLE_ROUTE_STATUSES,
    SPECIAL_ENGINE_SETS,
    SUPPORTED_BUILD_ROUTES,
    SUPPORTED_REPEAT_MODES,
    SUPPORTED_REPORT_STATUSES,
    SUPPORTED_TOOL_REQUIREMENTS,
    SUPPORTED_VISUAL_MODES,
    SUPPORTED_VISUAL_ROUTES,
    SUPPORTED_VISUAL_TRANSPORTS,
    config_path,
    emit,
    engine_ref_path_status,
    load_json,
    local_path,
    metadata_path_text,
    repo_path,
    repo_relative_status,
)

def load_contracts():
    engines_doc = load_json(ENGINES_PATH)
    cases_doc = load_json(CASES_PATH)
    reports_doc = load_json(REPORTS_PATH)
    engine_sources = {}
    case_support_by_engine = {}
    errors = []

    engine_ids = []
    support_statuses = set(engines_doc.get("support_statuses", []))
    result_statuses = set(cases_doc.get("result_statuses", []))
    case_route_statuses = support_statuses.union(result_statuses)
    host_routes = set(cases_doc.get("host_routes", []))
    release_platform_by_id = validate_release_platforms(engines_doc, host_routes, errors)
    engine_report_metadata_by_id = {}
    source_preparation_by_engine_id = {}
    configured_case_ids = [
        case.get("id", "")
        for case in cases_doc.get("cases", [])
        if case.get("id", "")
    ]
    configured_case_id_set = set(configured_case_ids)
    for engine in engines_doc.get("engines", []):
        engine_id = engine.get("id", "")
        if not engine_id:
            errors.append("engine_missing_id")
            continue
        if engine_id in engine_ids:
            errors.append(f"duplicate_engine_id={engine_id}")
        engine_ids.append(engine_id)
        source_kind = engine.get("source", "git")
        built_in_source = source_kind == "built_in"
        if source_kind not in {"git", "built_in", "versioned_source"}:
            errors.append(f"invalid_source_kind engine={engine_id} source={source_kind}")
        required_engine_fields = [
            "display_name",
            "repo_url",
            "commit",
            "source_manifest",
            "support_status"
        ]
        if not built_in_source:
            required_engine_fields.extend(["default_ref_path", "override_env"])
        for key in required_engine_fields:
            if not engine.get(key, ""):
                errors.append(f"missing_engine_field engine={engine_id} key={key}")
        if built_in_source and engine.get("repo_url", "") != "built_in":
            errors.append(f"invalid_built_in_source engine={engine_id}")
        if engine.get("support_status", "") not in support_statuses:
            errors.append(f"invalid_support_status engine={engine_id}")
        for requirement in engine.get("tool_requirements", []):
            if requirement not in SUPPORTED_TOOL_REQUIREMENTS:
                errors.append(f"invalid_tool_requirement engine={engine_id} tool={requirement}")
        commit = engine.get("commit", "")
        if source_kind == "git" and engine.get("support_status") == "supported" and not re.fullmatch(r"[0-9a-f]{40}", commit):
            errors.append(f"invalid_commit engine={engine_id}")
        if engine.get("default_ref_path", ""):
            status = engine_ref_path_status(engine.get("default_ref_path", ""))
            if status != "ok":
                errors.append(f"invalid_path engine={engine_id} key=default_ref_path reason={status}")
        if engine.get("source_manifest", ""):
            status = repo_relative_status(engine.get("source_manifest", ""))
            if status != "ok":
                errors.append(f"invalid_path engine={engine_id} key=source_manifest reason={status}")
        source_manifest_path = repo_path(engine.get("source_manifest", ""))
        if not source_manifest_path.is_file():
            errors.append(f"missing_engine_source engine={engine_id} path={source_manifest_path}")
            continue
        adapter = load_json(source_manifest_path)
        engine_sources[engine_id] = adapter
        if adapter.get("engine_id") != engine_id:
            errors.append(f"engine_source_mismatch engine={engine_id}")
        report_metadata = validate_engine_report_metadata(engine_id, adapter.get("report_metadata"), errors)
        if report_metadata:
            engine_report_metadata_by_id[engine_id] = report_metadata
        source_preparation_manifest = adapter.get("source_preparation_manifest", "")
        if source_preparation_manifest:
            source_preparation_status = validate_source_preparation_reference(engine_id, source_preparation_manifest, errors)
            if source_preparation_status:
                source_preparation_by_engine_id[engine_id] = source_preparation_status
        if adapter.get("route_status", "") not in support_statuses:
            errors.append(f"invalid_engine_source_status engine={engine_id}")
        for host_route in adapter.get("supported_platforms", []):
            if host_route not in host_routes:
                errors.append(f"invalid_engine_source_platform engine={engine_id} platform={host_route}")
        case_support_entries = adapter.get("case_support", [])
        if not isinstance(case_support_entries, list):
            errors.append(f"invalid_case_support engine={engine_id}")
            case_support_entries = []
        seen_case_support = []
        case_support_by_engine[engine_id] = {}
        for entry in case_support_entries:
            case_id = entry.get("case_id", "") if isinstance(entry, dict) else ""
            if not case_id:
                errors.append(f"missing_case_support_id engine={engine_id}")
                continue
            if case_id in seen_case_support:
                errors.append(f"duplicate_case_support engine={engine_id} case={case_id}")
            seen_case_support.append(case_id)
            if case_id not in configured_case_id_set:
                errors.append(f"invalid_case_support_id engine={engine_id} case={case_id}")
            for route_key in ["headless", "visual"]:
                route_status = entry.get(route_key, "") if isinstance(entry, dict) else ""
                if route_status not in case_route_statuses:
                    errors.append(f"invalid_case_support_status engine={engine_id} case={case_id} route={route_key} status={route_status}")
            if isinstance(entry, dict):
                case_support_by_engine[engine_id][case_id] = dict(entry)
        for configured_case_id in configured_case_ids:
            if configured_case_id not in seen_case_support:
                errors.append(f"missing_case_support engine={engine_id} case={configured_case_id}")
        thread_support = adapter.get("thread_support", {})
        if thread_support:
            mode = thread_support.get("mode", "")
            supported_thread_counts = thread_support.get("supported_thread_counts", [])
            if mode not in {"single_thread_only", "explicit_thread_counts", "host_bounded"}:
                errors.append(f"invalid_thread_support_mode engine={engine_id} mode={mode}")
            if mode in {"single_thread_only", "explicit_thread_counts"}:
                if not isinstance(supported_thread_counts, list) or not supported_thread_counts:
                    errors.append(f"invalid_thread_support_counts engine={engine_id}")
                for thread_count in supported_thread_counts:
                    if not isinstance(thread_count, int) or thread_count <= 0:
                        errors.append(f"invalid_thread_support_count engine={engine_id} value={thread_count}")
                if mode == "single_thread_only" and supported_thread_counts != [1]:
                    errors.append(f"invalid_single_thread_support engine={engine_id} values={supported_thread_counts}")
            elif supported_thread_counts:
                errors.append(f"invalid_host_bounded_thread_support_counts engine={engine_id}")
            for key in ["requested_worker_count", "effective_worker_count"]:
                worker_count = thread_support.get(key)
                if mode in {"explicit_thread_counts", "host_bounded"}:
                    if worker_count not in {"thread_count", "thread_count_minus_one"}:
                        errors.append(f"invalid_thread_support_worker_policy engine={engine_id} key={key} value={worker_count}")
                    continue
                if key not in thread_support or not isinstance(worker_count, int):
                    errors.append(f"missing_thread_support_field engine={engine_id} key={key}")
            if thread_support.get("main_thread_participates", "") not in {"yes", "no"}:
                errors.append(f"invalid_thread_support_main_thread engine={engine_id}")
        build_route = adapter.get("build", {}).get("route", "")
        if build_route not in SUPPORTED_BUILD_ROUTES:
            errors.append(f"unsupported_build_route engine={engine_id} route={build_route}")
        if build_route == "physx34_clangcl_sdk_release" and engine_id not in source_preparation_by_engine_id:
            errors.append(f"missing_source_preparation_manifest engine={engine_id}")
        for candidate in adapter.get("candidate_builds", []):
            candidate_route = candidate.get("route", "")
            if candidate_route not in SUPPORTED_BUILD_ROUTES:
                errors.append(f"unsupported_candidate_build_route engine={engine_id} route={candidate_route}")
        build_status = adapter.get("build", {}).get("route_status", adapter.get("route_status", ""))
        if build_status and build_status not in support_statuses and build_status not in result_statuses:
            errors.append(f"invalid_build_status engine={engine_id} status={build_status}")
        run_status = adapter.get("run", {}).get("route_status", adapter.get("route_status", ""))
        if run_status and run_status not in support_statuses and run_status not in result_statuses:
            errors.append(f"invalid_run_status engine={engine_id} status={run_status}")
        repeat_mode = adapter.get("run", {}).get("repeat_mode", "")
        if repeat_mode not in SUPPORTED_REPEAT_MODES:
            errors.append(f"unsupported_repeat_mode engine={engine_id} mode={repeat_mode}")
        visual = adapter.get("visual", {})
        if visual:
            visual_route = visual.get("route", "")
            if visual_route not in SUPPORTED_VISUAL_ROUTES:
                errors.append(f"unsupported_visual_route engine={engine_id} route={visual_route}")
            visual_status = visual.get("route_status", "")
            if visual_status not in support_statuses and visual_status not in result_statuses:
                errors.append(f"invalid_visual_status engine={engine_id} status={visual_status}")
            modes = visual.get("supported_modes", [])
            if not isinstance(modes, list):
                errors.append(f"invalid_visual_modes engine={engine_id}")
            for mode in modes:
                if mode not in SUPPORTED_VISUAL_MODES:
                    errors.append(f"unsupported_visual_mode engine={engine_id} mode={mode}")
            if visual_status in RUNNABLE_ROUTE_STATUSES and not visual.get("artifact_name", ""):
                errors.append(f"missing_visual_artifact_name engine={engine_id}")
            transport = visual.get("transport", "")
            if transport not in SUPPORTED_VISUAL_TRANSPORTS:
                errors.append(f"unsupported_visual_transport engine={engine_id} transport={transport}")
            if visual_route == "shared_visual_renderer_cli" and transport == "route_unavailable":
                errors.append(f"invalid_visual_transport engine={engine_id} transport={transport}")
            if transport == "shared_visual_transport":
                for key in ["producer_mode_arg", "protocol_version", "connection_mode"]:
                    if not visual.get(key, ""):
                        errors.append(f"missing_visual_transport_field engine={engine_id} key={key}")
                if visual.get("connection_mode", "") != "renderer_listens_loopback":
                    errors.append(f"invalid_visual_connection_mode engine={engine_id} mode={visual.get('connection_mode', '')}")
                if visual.get("producer_mode_arg", "") != "--producer-mode=shared-visual":
                    errors.append(f"invalid_visual_producer_mode engine={engine_id} value={visual.get('producer_mode_arg', '')}")
            visual_release = visual.get("release", {})
            if visual_release:
                if not isinstance(visual_release, dict):
                    errors.append(f"invalid_visual_release engine={engine_id}")
                    visual_release = {}
                release_platform = visual_release.get("platform", "")
                if release_platform not in release_platform_by_id:
                    errors.append(f"unsupported_visual_release_platform engine={engine_id} platform={release_platform}")
                release_status = visual_release.get("route_status", "")
                if release_status and release_status not in case_route_statuses:
                    errors.append(f"invalid_visual_release_status engine={engine_id} status={release_status}")
                if release_status and release_status not in RUNNABLE_ROUTE_STATUSES and not visual_release.get("reason", ""):
                    errors.append(f"missing_visual_release_reason engine={engine_id}")
        if engine.get("support_status") == "supported" and not adapter.get("metadata_requirements", []):
            errors.append(f"missing_metadata_requirements engine={engine_id}")

    engine_by_id = {engine["id"]: engine for engine in engines_doc.get("engines", []) if engine.get("id", "")}
    build_settings_rewrite_by_value = validate_build_settings_aliases(engine_report_metadata_by_id, errors)
    validate_source_preparation_adjustment_ids(source_preparation_by_engine_id, errors)

    for set_name, set_engines in engines_doc.get("engine_sets", {}).items():
        for engine_id in set_engines:
            if engine_id not in engine_ids:
                errors.append(f"invalid_engine_set set={set_name} engine={engine_id}")
    errors.extend(unreal_chaos_run_policy_errors(engines_doc, engine_by_id))
    for engine_id in engines_doc.get("engine_sets", {}).get("default_release", []):
        adapter = engine_sources.get(engine_id, {})
        release = adapter.get("release", {})
        if not release.get("artifact_manifest", ""):
            errors.append(f"missing_release_artifact_manifest engine={engine_id}")
        if engine_id not in engine_report_metadata_by_id:
            errors.append(f"missing_report_metadata engine={engine_id}")

    case_ids = []
    case_fixture_contract_by_id = {}
    for case in cases_doc.get("cases", []):
        case_id = case.get("id", "")
        if not case_id:
            errors.append("case_missing_id")
            continue
        if case_id in case_ids:
            errors.append(f"duplicate_case_id={case_id}")
        case_ids.append(case_id)
        if case.get("benchmark_mode", "") not in cases_doc.get("benchmark_modes", []):
            errors.append(f"invalid_benchmark_mode case={case_id}")
        for set_name in case.get("supported_engine_sets", []):
            if set_name not in engines_doc.get("engine_sets", {}) and set_name not in SPECIAL_ENGINE_SETS:
                errors.append(f"invalid_case_engine_set case={case_id} set={set_name}")
        for thread_count in case.get("thread_counts", []):
            if not isinstance(thread_count, int) or thread_count <= 0:
                errors.append(f"invalid_thread_count case={case_id} value={thread_count}")
        for preset_name, repeat_count in case.get("repeat_presets", {}).items():
            if not isinstance(repeat_count, int) or repeat_count <= 0:
                errors.append(f"invalid_repeat_preset case={case_id} preset={preset_name}")
        for counter in case.get("stability_counters", []):
            if counter not in REQUIRED_NORMALIZED_COLUMNS:
                errors.append(f"invalid_stability_counter case={case_id} counter={counter}")
        fixture_contract = validate_case_fixture_contract(case, engine_by_id, errors)
        if fixture_contract:
            case_fixture_contract_by_id[case_id] = fixture_contract

    for engine_id, adapter in engine_sources.items():
        for path_key in ["prepared_path"]:
            status = repo_relative_status(adapter.get(path_key, ""))
            if status != "ok":
                errors.append(f"invalid_engine_source_path engine={engine_id} key={path_key} reason={status}")
        for token in adapter.get("run", {}).get("args", []):
            for placeholder in PLACEHOLDER_PATTERN.findall(token):
                if placeholder not in ALLOWED_PLACEHOLDERS:
                    errors.append(f"unsupported_placeholder engine={engine_id} placeholder={placeholder}")
        release = adapter.get("release", {})
        if release:
            platform_name = release.get("platform", "")
            if platform_name not in release_platform_by_id:
                errors.append(f"unsupported_release_platform engine={engine_id} platform={platform_name}")
            manifest_path = release.get("artifact_manifest", "")
            if repo_relative_status(manifest_path) != "ok":
                errors.append(f"invalid_release_manifest_path engine={engine_id} path={manifest_path}")
        build = adapter.get("build", {})
        for path_key in [
            "source_subdir",
            "build_dir_linux",
            "build_dir_windows",
            "build_dir",
            "obj_dir",
            "package_dir_windows",
            "project",
            "runner_source_dir",
            "runner_manifest",
            "cargo_lock",
            "cargo_target_dir",
            "build_metadata",
            "generated_source_path",
            "sdk_build_dir_windows",
            "sdk_bin_dir_windows",
            "sdk_lib_dir_windows",
            "runner_build_dir_windows"
        ]:
            if path_key in build:
                status = repo_relative_status(build.get(path_key, ""))
                if status != "ok":
                    errors.append(f"invalid_build_path engine={engine_id} key={path_key} reason={status}")
        for shared_dir in build.get("shared_source_dirs", []):
            for path_key in ["source", "target"]:
                value = shared_dir.get(path_key, "")
                if not value:
                    errors.append(f"missing_shared_source_dir_field engine={engine_id} key={path_key}")
                    continue
                status = repo_relative_status(value)
                if status != "ok":
                    errors.append(f"invalid_shared_source_dir engine={engine_id} key={path_key} reason={status}")
        if build.get("route", "") == "cargo_release":
            required_cargo_fields = [
                "runner_source_dir",
                "runner_manifest",
                "cargo_lock",
                "cargo_target_dir",
                "build_metadata",
                "package_dir_windows",
                "target",
                "executable_name_windows",
                "toolchain_id",
                "cargo_profile",
                "public_build_label"
            ]
            for key in required_cargo_fields:
                if not build.get(key, ""):
                    errors.append(f"missing_cargo_build_field engine={engine_id} key={key}")
            if engine_id == "rapier3d" and not build.get("rapier_version", ""):
                errors.append(f"missing_cargo_build_field engine={engine_id} key=rapier_version")
            if engine_id == "avian3d":
                package_metadata = build.get("package_metadata", {})
                if not isinstance(package_metadata, dict):
                    errors.append(f"invalid_cargo_package_metadata engine={engine_id}")
                    package_metadata = {}
                for key in ["avian_version", "bevy_version"]:
                    value = package_metadata.get(key, "")
                    if not isinstance(value, str) or not value.strip():
                        errors.append(f"missing_cargo_package_metadata engine={engine_id} key={key}")
            features = build.get("cargo_features", [])
            if not isinstance(features, list) or "parallel" not in features:
                errors.append(f"invalid_cargo_features engine={engine_id}")
            if build.get("cargo_profile", "") != "release":
                errors.append(f"invalid_cargo_profile engine={engine_id} profile={build.get('cargo_profile', '')}")
        for candidate in adapter.get("candidate_builds", []):
            for path_key in [
                "source_subdir",
                "build_dir",
                "obj_dir",
                "package_dir_windows",
                "project",
                "runner_source_dir"
            ]:
                if path_key in candidate:
                    status = repo_relative_status(candidate.get(path_key, ""))
                    if status != "ok":
                        errors.append(f"invalid_candidate_build_path engine={engine_id} key={path_key} reason={status}")

    report_ids = []
    report_presentation_by_id = {}
    case_ids_set = set(case_ids)
    for report in reports_doc.get("reports", []):
        report_id = report.get("id", "")
        if not report_id:
            errors.append("report_missing_id")
            continue
        if report_id in report_ids:
            errors.append(f"duplicate_report_id={report_id}")
        report_ids.append(report_id)
        if report.get("case_id", "") not in case_ids_set:
            errors.append(f"invalid_report_case report={report_id} case={report.get('case_id', '')}")
        for path_key in ["result_source", "normalized_csv", "summary_csv"]:
            status = repo_relative_status(report.get(path_key, ""))
            if status != "ok":
                errors.append(f"invalid_report_path report={report_id} key={path_key} reason={status}")
        report_status = report.get("report_status", "")
        if report_status not in SUPPORTED_REPORT_STATUSES:
            errors.append(f"invalid_report_status report={report_id} status={report_status}")
        presentation = validate_report_presentation(report, engine_by_id, errors)
        if presentation:
            report_presentation_by_id[report_id] = presentation

    if errors:
        for error in errors:
            emit("manifest_validation", "invalid_result", error)
        raise SystemExit(2)

    return {
        "engines_doc": engines_doc,
        "cases_doc": cases_doc,
        "reports_doc": reports_doc,
        "engine_sources": engine_sources,
        "engine_by_id": engine_by_id,
        "case_support_by_engine": case_support_by_engine,
        "release_platform_by_id": release_platform_by_id,
        "engine_report_metadata_by_id": engine_report_metadata_by_id,
        "build_settings_rewrite_by_value": build_settings_rewrite_by_value,
        "case_fixture_contract_by_id": case_fixture_contract_by_id,
        "report_presentation_by_id": report_presentation_by_id,
        "source_preparation_by_engine_id": source_preparation_by_engine_id,
        "case_by_slug": {case["slug"]: case for case in cases_doc["cases"]},
        "case_by_id": {case["id"]: case for case in cases_doc["cases"]},
        "report_by_id": {report["id"]: report for report in reports_doc["reports"]}
    }

def validate_release_platforms(engines_doc, host_routes, errors):
    platforms = engines_doc.get("release_platforms", {})
    release_platform_by_id = {}
    if not isinstance(platforms, dict) or not platforms:
        errors.append("missing_release_platforms")
        return release_platform_by_id
    if DEFAULT_RELEASE_PLATFORM not in platforms:
        errors.append(f"missing_default_release_platform={DEFAULT_RELEASE_PLATFORM}")
    for platform_id, entry in platforms.items():
        if not isinstance(platform_id, str) or not platform_id.strip():
            errors.append("invalid_release_platform_id")
            continue
        if not isinstance(entry, dict):
            errors.append(f"invalid_release_platform platform={platform_id}")
            continue
        host_route = entry.get("host_route", "")
        if host_route not in host_routes:
            errors.append(f"invalid_release_platform_host_route platform={platform_id} host_route={host_route}")
            continue
        release_platform_by_id[platform_id] = {"host_route": host_route}
    return release_platform_by_id

def validate_engine_report_metadata(engine_id, metadata, errors):
    if not isinstance(metadata, dict):
        errors.append(f"missing_report_metadata engine={engine_id}")
        return {}
    output = {}
    for key in ["physics_settings", "build_settings"]:
        value = metadata.get(key, "")
        if not isinstance(value, str) or not value.strip():
            errors.append(f"invalid_report_metadata engine={engine_id} key={key}")
            continue
        output[key] = value
    if "physics_settings" not in output or "build_settings" not in output:
        return {}
    return output

def validate_build_settings_aliases(engine_report_metadata_by_id, errors):
    return {}

def validate_case_fixture_contract(case, engine_by_id, errors):
    contract = case.get("fixture_contract", {})
    if not contract:
        return {}
    case_id = case.get("id", "")
    if not isinstance(contract, dict):
        errors.append(f"invalid_fixture_contract case={case_id}")
        return {}
    semantic = contract.get("semantic", "")
    if not isinstance(semantic, str) or not semantic.strip():
        errors.append(f"invalid_fixture_semantic case={case_id}")
    required_columns = contract.get("required_metadata_columns", [])
    if not isinstance(required_columns, list) or not required_columns:
        errors.append(f"invalid_fixture_metadata_columns case={case_id}")
        required_columns = []
    expected_columns = {"fixture_semantic", "fixture_version", "static_box_count"}
    if set(required_columns) != expected_columns:
        errors.append(f"invalid_fixture_metadata_columns case={case_id} columns={','.join(str(item) for item in required_columns)}")
    versions = contract.get("engine_fixture_versions", {})
    if not isinstance(versions, dict) or not versions:
        errors.append(f"invalid_fixture_versions case={case_id}")
        versions = {}
    normalized_versions = {}
    for engine_id, version in versions.items():
        if engine_id not in engine_by_id:
            errors.append(f"unknown_fixture_engine case={case_id} engine={engine_id}")
            continue
        if not isinstance(version, str) or not version.strip():
            errors.append(f"invalid_fixture_version case={case_id} engine={engine_id}")
            continue
        normalized_versions[engine_id] = version
    count_keys = ["static_box_count", "body_count", "shape_count"]
    normalized_counts = {}
    for key in count_keys:
        value = case.get(key)
        if type(value) is not int or value <= 0:
            errors.append(f"invalid_fixture_count case={case_id} key={key} value={value}")
            continue
        normalized_counts[key] = value
    if len(normalized_counts) != len(count_keys):
        return {}
    return {
        "semantic": semantic,
        "required_metadata_columns": list(required_columns),
        "engine_fixture_versions": normalized_versions,
        "static_box_count": normalized_counts["static_box_count"],
        "body_count": normalized_counts["body_count"],
        "shape_count": normalized_counts["shape_count"],
    }

def validate_report_presentation(report, engine_by_id, errors):
    report_id = report.get("id", "")
    presentation = report.get("presentation", {})
    if not isinstance(presentation, dict) or not presentation:
        errors.append(f"missing_report_presentation report={report_id}")
        return {}
    logo_root = presentation.get("logo_root", "")
    if not isinstance(logo_root, str) or repo_relative_status(logo_root) != "ok":
        errors.append(f"invalid_report_logo_root report={report_id}")
        logo_root = ""
    engine_order = validate_report_engine_id_list(report_id, "engine_order", presentation.get("engine_order", []), engine_by_id, errors)
    legend_rows = []
    raw_legend_rows = presentation.get("legend_rows", [])
    if not isinstance(raw_legend_rows, list) or not raw_legend_rows:
        errors.append(f"invalid_report_legend_rows report={report_id}")
    else:
        for row in raw_legend_rows:
            legend_rows.append(validate_report_engine_id_list(report_id, "legend_rows", row, engine_by_id, errors))
    presentation_engine_ids = []
    for engine_id in engine_order:
        if engine_id not in presentation_engine_ids:
            presentation_engine_ids.append(engine_id)
    for row in legend_rows:
        for engine_id in row:
            if engine_id not in presentation_engine_ids:
                presentation_engine_ids.append(engine_id)
    logos = validate_report_logos(report_id, logo_root, presentation.get("logos", {}), engine_by_id, presentation_engine_ids, errors)
    return {
        "logo_root": logo_root,
        "engine_order": engine_order,
        "legend_rows": legend_rows,
        "logos": logos,
    }

def validate_report_engine_id_list(report_id, key, values, engine_by_id, errors):
    if not isinstance(values, list) or not values:
        errors.append(f"invalid_report_{key} report={report_id}")
        return []
    output = []
    for engine_id in values:
        if engine_id not in engine_by_id:
            errors.append(f"unknown_report_engine report={report_id} key={key} engine={engine_id}")
            continue
        if engine_id in output:
            errors.append(f"duplicate_report_engine report={report_id} key={key} engine={engine_id}")
            continue
        output.append(engine_id)
    return output

def validate_report_logos(report_id, logo_root, logos, engine_by_id, presentation_engine_ids, errors):
    if not isinstance(logos, dict) or not logos:
        errors.append(f"invalid_report_logos report={report_id}")
        return {}
    presentation_engine_id_set = set(presentation_engine_ids)
    for engine_id in presentation_engine_ids:
        if engine_id not in logos:
            errors.append(f"missing_report_logo_entry report={report_id} engine={engine_id}")
    output = {}
    for engine_id, logo in logos.items():
        if engine_id not in engine_by_id:
            errors.append(f"unknown_report_logo_engine report={report_id} engine={engine_id}")
            continue
        if engine_id not in presentation_engine_id_set:
            errors.append(f"unused_report_logo_entry report={report_id} engine={engine_id}")
            continue
        if not isinstance(logo, dict):
            errors.append(f"invalid_report_logo report={report_id} engine={engine_id}")
            continue
        file_name = logo.get("file", "")
        media_type = logo.get("media_type", "")
        view_box = logo.get("view_box", "")
        if not isinstance(file_name, str) or repo_relative_status(file_name) != "ok":
            errors.append(f"invalid_report_logo_file report={report_id} engine={engine_id}")
            continue
        if "/" in file_name.replace("\\", "/"):
            errors.append(f"invalid_report_logo_file report={report_id} engine={engine_id}")
            continue
        if logo_root:
            logo_path = repo_path(logo_root) / file_name
            if not logo_path.is_file():
                errors.append(f"missing_report_logo report={report_id} engine={engine_id} path={metadata_path_text(logo_path)}")
        if not isinstance(media_type, str) or "/" not in media_type:
            errors.append(f"invalid_report_logo_media_type report={report_id} engine={engine_id}")
        if not isinstance(view_box, str) or not view_box.strip():
            errors.append(f"invalid_report_logo_view_box report={report_id} engine={engine_id}")
        source_width = logo.get("source_width", 0)
        source_height = logo.get("source_height", 0)
        use_width_ratio = logo.get("use_width_ratio", 0)
        for key, value in [
            ("source_width", source_width),
            ("source_height", source_height),
            ("use_width_ratio", use_width_ratio),
        ]:
            if not isinstance(value, (int, float)) or value <= 0:
                errors.append(f"invalid_report_logo_{key} report={report_id} engine={engine_id}")
        output[engine_id] = {
            "file": file_name,
            "media_type": media_type,
            "view_box": view_box,
            "source_width": source_width,
            "source_height": source_height,
            "use_width_ratio": use_width_ratio,
        }
    return output

def validate_source_preparation_reference(engine_id, manifest_text, errors):
    status = repo_relative_status(manifest_text)
    if status != "ok":
        errors.append(f"invalid_source_preparation_path engine={engine_id} reason={status}")
        return {}
    manifest_path = repo_path(manifest_text)
    if not manifest_path.is_file():
        errors.append(f"missing_source_preparation_manifest engine={engine_id} path={manifest_text}")
        return {}
    document = load_json(manifest_path)
    return validate_source_preparation_manifest(engine_id, document, manifest_path, errors)

def validate_source_preparation_manifest(engine_id, document, manifest_path, errors):
    if not isinstance(document, dict):
        errors.append(f"invalid_source_preparation_manifest engine={engine_id}")
        return {}
    if document.get("schema_version") != 1:
        errors.append(f"invalid_source_preparation_schema engine={engine_id}")
    if document.get("engine_id", "") != engine_id:
        errors.append(f"source_preparation_engine_mismatch engine={engine_id} actual={document.get('engine_id', '')}")
    output = {"manifest_path": metadata_path_text(manifest_path)}
    for key in [
        "expected_physx_version",
        "required_nvtoolsext_include",
        "generated_adjustment_manifest",
        "adjustment_id",
        "source_scope",
        "reason",
    ]:
        value = document.get(key, "")
        if not isinstance(value, str) or not value.strip():
            errors.append(f"invalid_source_preparation_field engine={engine_id} key={key}")
            continue
        output[key] = value
    if repo_relative_status(document.get("required_nvtoolsext_include", "")) != "ok":
        errors.append(f"invalid_source_preparation_path engine={engine_id} key=required_nvtoolsext_include")
    if repo_relative_status(document.get("generated_adjustment_manifest", "")) != "ok":
        errors.append(f"invalid_source_preparation_path engine={engine_id} key=generated_adjustment_manifest")
    output["required_source_dirs"] = validate_source_preparation_paths(engine_id, "required_source_dirs", document.get("required_source_dirs", []), errors)
    output["resource_file_relative_paths"] = validate_source_preparation_paths(engine_id, "resource_file_relative_paths", document.get("resource_file_relative_paths", []), errors)
    output["map_link_flag_relative_paths"] = validate_source_preparation_paths(engine_id, "map_link_flag_relative_paths", document.get("map_link_flag_relative_paths", []), errors)
    output["artifact_prefixes"] = validate_nonempty_string_list(engine_id, "artifact_prefixes", document.get("artifact_prefixes", []), errors)
    output["text_adjustments"] = validate_source_preparation_adjustments(engine_id, document.get("text_adjustments", []), errors)
    output["engine_id"] = engine_id
    return output

def validate_source_preparation_adjustment_ids(source_preparation_by_engine_id, errors):
    seen_adjustment_ids = {}
    for engine_id, preparation in source_preparation_by_engine_id.items():
        adjustment_id = preparation.get("adjustment_id", "")
        if not adjustment_id:
            errors.append(f"empty_source_preparation_adjustment_id engine={engine_id}")
            continue
        owner = seen_adjustment_ids.get(adjustment_id)
        if owner is not None:
            errors.append(f"duplicate_source_preparation_adjustment_id id={adjustment_id} engine={engine_id} owner={owner}")
            continue
        seen_adjustment_ids[adjustment_id] = engine_id

def validate_source_preparation_paths(engine_id, key, values, errors):
    output = validate_nonempty_string_list(engine_id, key, values, errors)
    for value in output:
        status = repo_relative_status(value)
        if status != "ok":
            errors.append(f"invalid_source_preparation_path engine={engine_id} key={key} path={value} reason={status}")
    return output

def validate_nonempty_string_list(engine_id, key, values, errors):
    if not isinstance(values, list) or not values:
        errors.append(f"invalid_source_preparation_list engine={engine_id} key={key}")
        return []
    output = []
    for value in values:
        if not isinstance(value, str) or not value.strip():
            errors.append(f"invalid_source_preparation_list_item engine={engine_id} key={key}")
            continue
        output.append(value)
    return output

def validate_source_preparation_adjustments(engine_id, adjustments, errors):
    if not isinstance(adjustments, list) or not adjustments:
        errors.append(f"invalid_source_preparation_adjustments engine={engine_id}")
        return []
    output = []
    seen_entries = set()
    for adjustment in adjustments:
        if not isinstance(adjustment, dict):
            errors.append(f"invalid_source_preparation_adjustment engine={engine_id}")
            continue
        relative_path = adjustment.get("relative_path", "")
        original = adjustment.get("original", "")
        patched = adjustment.get("patched", "")
        if not isinstance(relative_path, str) or not relative_path.strip():
            errors.append(f"missing_text_adjustment_field engine={engine_id} key=relative_path")
            continue
        status = repo_relative_status(relative_path)
        if status != "ok":
            errors.append(f"invalid_text_adjustment_path engine={engine_id} path={relative_path} reason={status}")
            continue
        if not isinstance(original, str) or not original:
            errors.append(f"empty_text_adjustment_original engine={engine_id} path={relative_path}")
            continue
        if not isinstance(patched, str) or not patched:
            errors.append(f"empty_text_adjustment_patched engine={engine_id} path={relative_path}")
            continue
        entry_key = (relative_path, original)
        if entry_key in seen_entries:
            errors.append(f"duplicate_text_adjustment engine={engine_id} path={relative_path}")
            continue
        seen_entries.add(entry_key)
        output.append({
            "relative_path": relative_path,
            "original": original,
            "patched": patched,
        })
    return output

def unreal_chaos_run_policy_errors(engines_doc, engine_by_id):
    errors = []
    engine = engine_by_id.get("unreal_chaos")
    if engine is None:
        return errors
    if engine.get("support_status", "") != "experimental":
        errors.append(f"invalid_unreal_chaos_support_status status={engine.get('support_status', '')} expected=experimental")
    for set_name in ["core", "all", "working", "default_release", "default_visual_release"]:
        if "unreal_chaos" in engines_doc.get("engine_sets", {}).get(set_name, []):
            errors.append(f"unreal_chaos_not_explicit_only set={set_name}")
    all_selection = [
        item.get("id", "")
        for item in engines_doc.get("engines", [])
        if item.get("support_status", "") == "supported"
    ]
    if "unreal_chaos" in all_selection:
        errors.append("unreal_chaos_not_explicit_only set=special_all")
    return errors

def selected_engines(contracts, engine_set, engine_id, engine_ids=None):
    engines_doc = contracts["engines_doc"]
    engine_by_id = contracts["engine_by_id"]
    if engine_ids is None:
        engine_ids = []
    if engine_ids:
        engines = []
        seen_ids = set()
        for selected_id in engine_ids:
            if selected_id not in engine_by_id:
                emit("engine_selection", "invalid_result", f"engine={selected_id}")
                raise SystemExit(2)
            if selected_id in seen_ids:
                emit("engine_selection", "invalid_result", f"duplicate_engine={selected_id}")
                raise SystemExit(2)
            seen_ids.add(selected_id)
            engines.append(engine_by_id[selected_id])
        return engines
    if engine_id:
        if engine_id not in engine_by_id:
            emit("engine_selection", "invalid_result", f"engine={engine_id}")
            raise SystemExit(2)
        return [engine_by_id[engine_id]]
    if engine_set == "all":
        return [engine for engine in engines_doc["engines"] if engine.get("support_status") == "supported"]
    if engine_set in engines_doc.get("engine_sets", {}):
        return [engine_by_id[item] for item in engines_doc["engine_sets"][engine_set]]
    emit("engine_selection", "invalid_result", f"engine_set={engine_set}")
    raise SystemExit(2)

def route_case_support(contracts, engine_id, case_id, route_key):
    engine_cases = contracts.get("case_support_by_engine", {}).get(engine_id, {})
    entry = engine_cases.get(case_id)
    if entry is None:
        return {
            "status": "invalid_result",
            "detail": f"engine={engine_id} case={case_id} route={route_key} missing_case_support",
        }
    return {
        "status": entry.get(route_key, "unsupported"),
        "detail": f"engine={engine_id} case={case_id} route={route_key}",
    }

def validate_case_route_support(contracts, engines, case_id, route_key):
    blocked = []
    for engine in engines:
        support = route_case_support(contracts, engine["id"], case_id, route_key)
        if support["status"] not in RUNNABLE_ROUTE_STATUSES:
            blocked.append(support)
    if blocked:
        details = [f"{item['detail']} status={item['status']}" for item in blocked]
        return {
            "status": blocked[0]["status"],
            "detail": ";".join(details),
        }
    return {"status": "ok", "detail": f"case={case_id} route={route_key}"}

def engine_ref_path(engine):
    override = os.environ.get(engine.get("override_env", ""), "")
    if override:
        return local_path(override).resolve()
    return config_path(engine["default_ref_path"]).resolve()
