import base64
import math
import os
import re
from pathlib import Path
from xml.sax.saxutils import escape

from .common import (
    emit,
    load_json,
    metadata_path_text,
    repo_path,
    write_text_lf,
)
from .options import option_value
from .contracts import load_contracts
from .result_contracts import (
    load_and_validate_source_rows,
    validate_source_manifest_contract,
    write_summary_rows,
)
from .host import (
    host_label,
    host_label_lines,
    host_source_coverage,
    validate_publishable_host_metadata,
)

def command_report(args):
    contracts = load_contracts()
    if not args:
        emit("report_arguments", "invalid_result", "missing_result_dir")
        return 2
    result_dir = repo_path(args[0]) if not Path(args[0]).is_absolute() else Path(args[0])
    report = select_report(contracts, args[1:], result_dir)
    if not result_dir.exists():
        emit("report_arguments", "invalid_result", f"missing_result_dir={result_dir}")
        return 2
    normalized_path = result_dir / "normalized.csv"
    if not normalized_path.is_file():
        emit("report", "invalid_result", f"missing_normalized={normalized_path}")
        return 2
    result_status = validate_result_for_report(contracts, result_dir)
    if result_status["status"] != "ok":
        emit("report", result_status["status"], result_status["detail"])
        return 2
    manifest = result_status["manifest"]
    summary_status = write_summary_rows(
        result_dir / "summary.csv", result_dir.name, result_status["rows"])
    if summary_status["status"] != "ok":
        return 2
    report_status = write_report_artifacts(
        result_dir, contracts, report, manifest, summary_status["rows"])
    return 0 if report_status["status"] == "ok" else 2

def write_report_artifacts(result_dir, contracts, report, manifest, summary_rows):
    host_source_status = validate_publishable_host_metadata(manifest["host"])
    if host_source_status["status"] != "ok":
        emit("report_host_sources", host_source_status["status"], host_source_status["detail"])
        return host_source_status
    emit("report_host_sources", "ok", host_source_coverage(manifest["host"]))
    logo_status = validate_report_logo_assets(report_current_engine_ids(summary_rows, manifest), report_presentation(contracts, report))
    if logo_status["status"] != "ok":
        emit("report", logo_status["status"], logo_status["detail"])
        return logo_status
    write_summary_svg(result_dir / "summary.svg", summary_rows, contracts, report, manifest)
    markdown_path = result_dir / "report.md"
    write_markdown_report(markdown_path, result_dir, summary_rows, contracts, report, manifest)
    emit("report_summary", "ok", metadata_path_text(result_dir / "summary.csv"))
    emit("report_svg", "ok", metadata_path_text(result_dir / "summary.svg"))
    emit("report_markdown", "ok", metadata_path_text(markdown_path))
    return {"status": "ok", "detail": metadata_path_text(markdown_path)}

def command_index(args):
    if args:
        emit("index_arguments", "invalid_result", "unexpected_args=" + ",".join(args))
        return 2
    contracts = load_contracts()
    write_result_indexes(contracts)
    emit("result_indexes", "ok", metadata_path_text(repo_path("results") / "README.md"))
    return 0

def select_report(contracts, args, result_dir):
    report_id = option_value(args, "--report", "")
    if report_id:
        report = contracts["report_by_id"].get(report_id)
        if report is None:
            emit("report_arguments", "invalid_result", f"report={report_id}")
            raise SystemExit(2)
        return report
    for report in contracts["reports_doc"]["reports"]:
        if repo_path(report["result_source"]).resolve() == result_dir.resolve():
            return report
    return contracts["reports_doc"]["reports"][0]

def validate_result_for_report(contracts, result_dir):
    manifest_json = result_dir / "manifest.json"
    if not manifest_json.is_file():
        return {"status": "invalid_result", "detail": f"missing_manifest={manifest_json}"}
    manifest = load_json(manifest_json)
    manifest_status = validate_source_manifest_contract(contracts, manifest, result_dir)
    if manifest_status["status"] != "ok":
        return manifest_status
    host_source_status = validate_publishable_host_metadata(manifest["host"])
    if host_source_status["status"] != "ok":
        return host_source_status
    row_status = load_and_validate_source_rows(contracts, manifest, result_dir / "normalized.csv")
    if row_status["status"] != "ok":
        return row_status
    return {
        "status": "ok",
        "detail": str(manifest_json),
        "manifest": manifest,
        "rows": row_status["rows"],
    }

def engine_display_name(contracts, engine_id):
    engine = contracts["engine_by_id"].get(engine_id)
    if engine:
        return engine["display_name"]
    return engine_id

def markdown_table_value(value):
    return str(value).replace("|", "\\|")

def markdown_table(headers, rows, alignments):
    escaped_headers = [markdown_table_value(header) for header in headers]
    escaped_rows = [[markdown_table_value(value) for value in row] for row in rows]
    widths = [len(header) for header in escaped_headers]
    for row in escaped_rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))

    def padded_row(values):
        cells = []
        for index, value in enumerate(values):
            if alignments[index] == "right":
                cells.append(value.rjust(widths[index]))
            else:
                cells.append(value.ljust(widths[index]))
        return "| " + " | ".join(cells) + " |"

    separators = []
    for index, alignment in enumerate(alignments):
        marker_width = max(widths[index], 3)
        if alignment == "right":
            separators.append("-" * (marker_width - 1) + ":")
        else:
            separators.append(":" + "-" * (marker_width - 1))

    return [padded_row(escaped_headers), "| " + " | ".join(separators) + " |"] + [padded_row(row) for row in escaped_rows]

def report_benchmark_modes(rows):
    modes = sorted({row.get("benchmark_mode", "") for row in rows if row.get("benchmark_mode", "")})
    if modes:
        return ", ".join(modes)
    return "unknown"

def report_warmup_steps(rows, case):
    warmup_steps = sorted({row.get("warmup_steps", "") for row in rows if row.get("warmup_steps", "")})
    if warmup_steps:
        return ", ".join(warmup_steps)
    return str(case.get("warmup_steps", 0))

def report_thread_counts(rows):
    return ", ".join(str(item) for item in sorted({int(row["thread_count"]) for row in rows}))

def report_thread_selection_label(rows):
    counts = sorted({int(row["thread_count"]) for row in rows})
    prefix = "thread" if len(counts) == 1 and counts[0] == 1 else "threads"
    return f"{prefix} {', '.join(str(item) for item in counts)}"

def report_repeat_count(rows):
    repeat_counts = sorted({int(row["repeat_count"]) for row in rows if row.get("repeat_count", "")})
    if repeat_counts:
        return ", ".join(str(item) for item in repeat_counts)
    return "unknown"

def report_body_shape_count(rows, case):
    body_counts = sorted({int(row["body_count"]) for row in rows if row.get("body_count", "")})
    shape_counts = sorted({int(row["shape_count"]) for row in rows if row.get("shape_count", "")})
    body_count = body_counts[0] if body_counts else int(case["body_count"])
    shape_count = shape_counts[0] if shape_counts else int(case["shape_count"])
    return f"{body_count:,} bodies, {shape_count:,} shapes"

def report_current_engine_ids(rows, manifest):
    engine_ids = []
    for row in rows:
        engine_id = row.get("engine_id", "")
        if engine_id and engine_id not in engine_ids:
            engine_ids.append(engine_id)
    if engine_ids:
        return engine_ids
    for engine_entry in manifest.get("engines", []):
        engine_id = engine_entry.get("id", "")
        if engine_id and engine_id not in engine_ids:
            engine_ids.append(engine_id)
    return engine_ids

def report_metric_label(report):
    chart_label = report.get("chart_label", "Physics ms/frame")
    chart_note = report.get("chart_note", "lower is better")
    return f"Median {chart_label}; {chart_note}."

def report_run_label(rows, manifest, repeat_label, warmup_label, timestep_label):
    repeat_word = "repeat" if repeat_label == "1" else "repeats"
    thread_label = report_thread_selection_label(rows)
    return f"{report_mode_label(manifest)}; {report_release_source_label(manifest)}; {repeat_label} {repeat_word}; {thread_label}; {manifest['step_count']} measured steps + {warmup_label} warmup; {timestep_label}."

def report_mode_label(manifest):
    mode = manifest.get("benchmark_mode", "")
    if mode == "headless_api":
        return "Headless, no-graphics run"
    if mode == "visualized_release":
        return "Release visual proof run"
    if mode:
        return mode.replace("_", " ")
    return "Benchmark run"

def report_release_source_label(manifest):
    platforms = []
    for engine_entry in manifest.get("engines", []):
        artifact_path = engine_entry.get("release_artifact_manifest", "")
        match = re.search(r"release/([^/]+)/", artifact_path.replace("\\", "/"))
        if match and match.group(1) not in platforms:
            platforms.append(match.group(1))
    if len(platforms) == 1:
        return f"{report_platform_label(platforms[0])} release files"
    host_route = manifest.get("host_route", "")
    if host_route:
        return f"{host_route.title()} route"
    return "release source not recorded"

def report_platform_label(platform_slug):
    return platform_slug.replace("windows", "Windows").replace("-x64", " x64").replace("-", " ")

def report_presentation(contracts, report):
    return contracts.get("report_presentation_by_id", {}).get(report["id"], {})

def ordered_report_engine_ids(engine_ids, presentation):
    configured_order = presentation.get("engine_order", [])
    order_index = {engine_id: index for index, engine_id in enumerate(configured_order)}
    return sorted(engine_ids, key=lambda engine_id: (order_index.get(engine_id, len(configured_order)), engine_id))

def report_engine_legend_rows(engine_ids, presentation):
    ordered_engine_ids = ordered_report_engine_ids(engine_ids, presentation)
    configured_rows = presentation.get("legend_rows", [])
    rows = []
    listed_engine_ids = []
    for configured_row in configured_rows:
        row = [engine_id for engine_id in configured_row if engine_id in ordered_engine_ids]
        if row:
            rows.append(row)
            listed_engine_ids.extend(row)
    remaining_engine_ids = [engine_id for engine_id in ordered_engine_ids if engine_id not in listed_engine_ids]
    for index in range(0, len(remaining_engine_ids), 4):
        rows.append(remaining_engine_ids[index:index + 4])
    if rows:
        return rows
    return [["unknown_engine"]]

def report_text_meta_height(lines):
    return max(30, 12 + 18 * len(lines))

def report_engine_meta_height(legend_rows):
    return max(30, 12 + 22 * len(legend_rows))

def report_physics_settings_label(rows, contracts, manifest):
    return "; ".join(report_segment_text(line) for line in report_physics_settings_lines(rows, contracts, manifest))

def report_build_settings_evidence_rows(rows, contracts, manifest):
    evidence_rows = []
    for line in report_build_settings_lines(rows, contracts, manifest):
        label = "Build settings" if not evidence_rows else ""
        evidence_rows.append([label, report_segment_text(line)])
    return evidence_rows

def report_physics_settings_lines(rows, contracts, manifest):
    engine_ids = report_current_engine_ids(rows, manifest)
    settings_by_engine = report_settings_by_engine(rows, engine_ids, "physics_settings")
    public_labels = []
    for engine_id in engine_ids:
        label = report_physics_public_label(settings_by_engine.get(engine_id, ""))
        if label and label not in public_labels:
            public_labels.append(label)
    if len(public_labels) == 1:
        return report_physics_segments(public_labels[0])
    lines = []
    for engine_id in engine_ids:
        setting = settings_by_engine.get(engine_id, "")
        lines.append([
            ("meta-key", report_short_engine_name(engine_id)),
            ("", f": {report_sentence(report_physics_public_label(setting))}"),
        ])
    if lines:
        return lines
    return [[("", "not recorded.")]]

def report_build_settings_lines(rows, contracts, manifest):
    engine_ids = report_current_engine_ids(rows, manifest)
    settings_by_engine = report_settings_by_engine(rows, engine_ids, "build_settings")
    lines = []
    for engine_id in engine_ids:
        setting = settings_by_engine.get(engine_id, "")
        lines.append(report_build_public_segments(engine_id, setting))
    if lines:
        return lines
    return [[("", "not recorded.")]]

def report_settings_by_engine(rows, engine_ids, column):
    settings_by_engine = {engine_id: "" for engine_id in engine_ids}
    for row in rows:
        engine_id = row.get("engine_id", "")
        if engine_id in settings_by_engine and not settings_by_engine[engine_id]:
            settings_by_engine[engine_id] = row.get(column, "").strip()
    return settings_by_engine

def report_physics_public_label(setting):
    text = str(setting).strip()
    lower = text.lower()
    if "solve" in lower and "4,1" in lower and "sleep=disabled" in lower:
        return "Physics solver iterations: 4; rigid-body sleeping: disabled"
    if "solver_iterations=4,1" in lower and "sleep_threshold=0" in lower:
        return "Physics solver iterations: 4; rigid-body sleeping: disabled"
    if "solver=4 velocity,1 position" in lower and "sleep=disabled" in lower:
        return "Physics solver iterations: 4; rigid-body sleeping: disabled"
    if "substeps=4" in lower and "sleep=disabled" in lower:
        return "Physics solver iterations: 4; rigid-body sleeping: disabled"
    if "solver_iterations=4" in lower and "sleep=disabled" in lower:
        return "Physics solver iterations: 4; rigid-body sleeping: disabled"
    if "physicsstep default solver" in lower and "synchronize_collision_world=on" in lower:
        return "Physics solver iterations: 4; rigid-body sleeping: disabled"
    return text or "not recorded"

def report_physics_segments(label):
    text = report_sentence(label)
    if text == "Physics solver iterations: 4; rigid-body sleeping: disabled.":
        return [[
            ("meta-key", "Physics solver iterations"),
            ("", ": 4; "),
            ("meta-key", "rigid-body sleeping"),
            ("", ": disabled."),
        ]]
    return [[("", text)]]

def report_build_public_segments(engine_id, setting):
    label = report_build_public_label(engine_id, setting)
    return [
        ("meta-key", f"{report_short_engine_name(engine_id)}:"),
        ("", f" {report_sentence(label)}"),
    ]

def report_build_public_label(engine_id, setting):
    text = report_normalize_space(setting)
    if not text:
        return "not recorded"
    text = report_build_dotnet_label(text)
    text = report_build_unity_label(text)
    text = report_build_without_engine_alias(engine_id, text)
    text = report_build_route_label(text)
    return text

def report_normalize_space(value):
    return re.sub(r"\s+", " ", str(value).strip())

def report_build_dotnet_label(text):
    match = re.search(r"(\.NET\s+\d+)\s+Release;\s+[^;]*ReleaseNoProfiling", text)
    if match:
        return f"{match.group(1)} ReleaseNoProfiling"
    return text

def report_build_unity_label(text):
    lower = text.lower()
    if "il2cpp" in lower and "burst on" in lower and "safety off" in lower:
        return "IL2CPP Release; Burst enabled; safety checks disabled"
    return text

def report_build_without_engine_alias(engine_id, text):
    cleaned = text
    for alias in report_build_engine_aliases(engine_id):
        cleaned = re.sub(rf"(^|;\s*){re.escape(alias)}\s+", r"\1", cleaned)
    return report_normalize_build_separators(cleaned)

def report_build_engine_aliases(engine_id):
    aliases = {
        "bepuphysics2": ["BepuPhysics", "BEPUphysics v2", "BEPU"],
        "box3d": ["Box3D"],
        "joltphysics": ["Jolt Physics", "Jolt"],
        "nvidia_physx34": ["NVIDIA PhysX 3.4"],
        "nvidia_physx5": ["NVIDIA PhysX 5.6"],
        "physx34": ["Vite PhysX 3.4"],
        "rapier3d": ["Rapier"],
        "unity_physics": ["Unity DOTS Physics", "Unity Physics", "Unity"],
    }
    return aliases.get(engine_id, [])

def report_normalize_build_separators(text):
    return re.sub(r"\s*;\s*", "; ", text).strip(" ;")

def report_build_route_label(text):
    cleaned = re.sub(r"\bRelease runner\b", "Release", text)
    cleaned = re.sub(r"\bDistribution runner\b", "Distribution", cleaned)
    return report_normalize_build_separators(cleaned)

def report_short_engine_name(engine_id):
    names = {
        "bepuphysics2": "BEPU",
        "box3d": "Box3D",
        "joltphysics": "Jolt Physics",
        "nvidia_physx34": "NVIDIA PhysX 3.4",
        "nvidia_physx5": "NVIDIA PhysX 5.6",
        "physx34": "Vite PhysX 3.4",
        "rapier3d": "Rapier",
        "unity_physics": "Unity",
        "unreal_chaos": "Unreal Engine Chaos",
    }
    return names.get(engine_id, engine_id)

def report_sentence(value):
    text = str(value).strip()
    if not text:
        return "not recorded."
    if text.endswith((".", "!", "?")):
        return text
    return text + "."

def report_segment_text(segments):
    return "".join(value for class_name, value in segments)

def append_meta_text(parts, x, y, segments):
    text_parts = [f'<text x="{x}" y="{y}" class="meta-value">']
    for class_name, value in segments:
        if class_name:
            text_parts.append(f'<tspan class="{class_name}">{escape(value)}</tspan>')
        else:
            text_parts.append(f'<tspan>{escape(value)}</tspan>')
    text_parts.append('</text>')
    parts.append("".join(text_parts))

def report_logo_dir(presentation):
    return repo_path(presentation.get("logo_root", ""))

def engine_logo_source_path(engine_id, presentation):
    logo = presentation.get("logos", {}).get(engine_id)
    if logo is None:
        return None
    return report_logo_dir(presentation) / logo["file"]

def validate_report_logo_assets(engine_ids, presentation):
    missing = []
    for engine_id in engine_ids:
        logo = presentation.get("logos", {}).get(engine_id)
        if logo is None:
            missing.append(f"{engine_id}:missing_logo_metadata")
            continue
        source = report_logo_dir(presentation) / logo["file"]
        if not source.is_file():
            missing.append(f"{engine_id}:{logo['file']}")
    if missing:
        return {
            "status": "invalid_result",
            "detail": f"missing_logo_dir={metadata_path_text(report_logo_dir(presentation))} missing={','.join(missing)}",
        }
    return {"status": "ok", "detail": metadata_path_text(report_logo_dir(presentation))}

def engine_logo_data_uri(engine_id, presentation):
    logo = presentation.get("logos", {}).get(engine_id)
    source = engine_logo_source_path(engine_id, presentation)
    if logo is None or source is None or not source.is_file():
        return ""
    encoded = base64.b64encode(source.read_bytes()).decode("ascii")
    return f"data:{logo['media_type']};base64,{encoded}"

def engine_logo_symbol_frame(engine_id, presentation):
    logo = presentation.get("logos", {}).get(engine_id, {})
    return (
        logo.get("view_box", "0 0 100 100"),
        logo.get("source_width", 100),
        logo.get("source_height", 100),
    )

def engine_logo_use_size(engine_id, size, presentation):
    logo = presentation.get("logos", {}).get(engine_id, {})
    return (size * logo.get("use_width_ratio", 1), size)

def append_engine_logo(parts, logo_sources, engine_id, x, center_y, size, color, presentation):
    logo_source = logo_sources.get(engine_id, "")
    if logo_source:
        width, height = engine_logo_use_size(engine_id, size, presentation)
        y = center_y - height / 2
        parts.append(f'<use href="#engine-logo-{engine_id}" x="{x:.1f}" y="{y:.1f}" width="{width:.1f}" height="{height:.1f}"/>')
        return width
    return 0

def report_axis_max(raw_max_value):
    target = max(1.0, raw_max_value * 1.15)
    return max(5, math.ceil(target / 5.0) * 5)

def write_markdown_report(path, result_dir, rows, contracts, report, manifest):
    case = contracts["case_by_id"][manifest["case_id"]]
    evidence_rows = [
        ["Selected run id", f"`{manifest['run_id']}`"],
        ["Result folder", f"`{metadata_path_text(result_dir)}/`"],
        ["Case id", f"`{case['id']}`"],
        ["Benchmark mode", f"`{report_benchmark_modes(rows)}`"],
        ["Thread counts", f"`{report_thread_counts(rows)}`"],
        ["Step count", f"`{manifest['step_count']}`"],
        ["Warmup steps", f"`{report_warmup_steps(rows, case)}`"],
        ["Host", host_label(manifest["host"])],
        ["Physics settings", report_physics_settings_label(rows, contracts, manifest)],
        *report_build_settings_evidence_rows(rows, contracts, manifest),
        ["Data source", f"`{report['summary_csv']}` generated from `{report['normalized_csv']}`"],
        ["Chart", "`summary.svg`"],
        ["Run route", manifest.get("host_route", "not recorded")],
    ]

    lines = [
        f"# {case['display_name']} Benchmark Report",
        "",
        "## Evidence",
        "",
    ]
    lines.extend(markdown_table(["Field", "Value"], evidence_rows, ["left", "left"]))
    lines.extend([
        "",
        "## Result Summary",
        "",
        "Main metric: lower median Physics ms/frame is better. Rows are grouped by thread count and sorted fastest to slowest within each thread group.",
        ""
    ])
    summary_rows = []
    previous_thread_count = ""
    for row in sorted_report_rows(rows, report, report_presentation(contracts, report)):
        if previous_thread_count and row["thread_count"] != previous_thread_count:
            summary_rows.append([""] * 9)
        previous_thread_count = row["thread_count"]
        summary_rows.append(
            [
                engine_display_name(contracts, row["engine_id"]),
                row["thread_count"],
                f"{float(row['median_ms_per_step']):.3f}",
                f"{float(row['mean_ms_per_step']):.3f}",
                f"{float(row['min_ms_per_step']):.3f}",
                f"{float(row['mean_steps_per_second']):.3f}",
                row["repeat_count"],
                f"{int(row['body_count']):,}",
                f"{int(row['shape_count']):,}",
            ]
        )
    lines.extend(markdown_table(
        [
            "Engine",
            "Threads",
            "Physics ms/frame median",
            "Physics ms/frame mean",
            "Fastest repeat ms/frame",
            "Steps/s mean",
            "Repeats",
            "Bodies",
            "Shapes",
        ],
        summary_rows,
        ["left", "right", "right", "right", "right", "right", "right", "right", "right"]
    ))
    lines.extend([
        "",
        "## Route Notes",
        "",
        "The table above is generated from `summary.csv`; `summary.csv` is generated from `normalized.csv`. Regenerate the report with `./bench.sh report <result-dir>` after a new benchmark run.",
        ""
    ])
    while lines and lines[-1] == "":
        lines.pop()
    write_text_lf(path, "\n".join(lines) + "\n")

def write_summary_svg(path, rows, contracts, report, manifest):
    presentation = report_presentation(contracts, report)
    sorted_rows = sorted_report_rows(rows, report, presentation)
    case = contracts["case_by_id"][manifest["case_id"]]
    width = 1280
    outer_x = 24
    outer_y = 24
    outer_width = 1232
    content_left = 48
    group_x = 32
    group_width = 1216
    row_step = 28
    group_gap = 18
    thread_counts = sorted({int(row["thread_count"]) for row in sorted_rows})
    group_heights = {}
    for thread_count in thread_counts:
        group_rows = [row for row in sorted_rows if int(row["thread_count"]) == thread_count]
        group_heights[thread_count] = 52 + row_step * len(group_rows)
    panel_columns = 1 if len(thread_counts) <= 1 else 2
    panel_rows = math.ceil(len(thread_counts) / panel_columns) if thread_counts else 0
    panel_width = (group_width - group_gap * (panel_columns - 1)) / panel_columns
    panel_height = max(group_heights.values()) if group_heights else 52
    chart_height = panel_rows * panel_height + group_gap * max(0, panel_rows - 1)
    values = [float(row["median_ms_per_step"]) for row in rows]
    raw_max_value = max(values) if values else 1.0
    axis_max = report_axis_max(raw_max_value)
    axis_ticks = [axis_max * index / 5 for index in range(6)]
    colors = {
        "avian3d": "#5869a9",
        "bepuphysics2": "#18875a",
        "box3d": "#2f68b7",
        "joltphysics": "#b65337",
        "nvidia_physx34": "#425f9c",
        "nvidia_physx5": "#253f6d",
        "physx34": "#7058b8",
        "rapier3d": "#7a321c",
        "unity_physics": "#4c7f91"
    }
    engine_ids = report_current_engine_ids(rows, manifest)
    legend_rows = report_engine_legend_rows(engine_ids, presentation)
    logo_sources = {engine_id: engine_logo_data_uri(engine_id, presentation) for engine_id in engine_ids}
    repeat_label = report_repeat_count(rows)
    warmup_label = report_warmup_steps(rows, case)
    timestep_label = f"{int(case['timestep_hz'])} Hz" if "timestep_hz" in case else "configured timestep"
    run_label = report_run_label(
        rows, manifest, repeat_label, warmup_label, timestep_label)
    host_lines = [[("", line)] for line in host_label_lines(manifest["host"])]
    physics_lines = report_physics_settings_lines(rows, contracts, manifest)
    build_lines = report_build_settings_lines(rows, contracts, manifest)
    meta_rows = [
        ("Metric", [[("", report_metric_label(report))]], "text", 30),
        ("Case", [[("", f"{case['display_name']}; {report_body_shape_count(rows, case)}.")]], "text", 30),
        ("Run", [[("", run_label)]], "text", 30),
        ("Host", host_lines, "text", report_text_meta_height(host_lines)),
        ("Engines", [], "engines", report_engine_meta_height(legend_rows)),
        ("Physics", physics_lines, "text", report_text_meta_height(physics_lines)),
        ("Builds", build_lines, "text", report_text_meta_height(build_lines)),
    ]
    meta_block_y = 76
    meta_total_height = sum(row[3] for row in meta_rows)
    group_top = meta_block_y + meta_total_height + 28
    height = max(520, group_top + chart_height + 36)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<defs>',
        '<style><![CDATA[',
        '  text{font-family:Inter,Segoe UI,Arial,sans-serif;fill:#0f172a;dominant-baseline:middle}',
        '  .tab{font-variant-numeric:tabular-nums;font-feature-settings:"tnum" 1}',
        '  .title{font-size:22px;font-weight:760}',
        '  .subtitle{font-size:13px;fill:#475569}',
        '  .meta-label{font-size:13px;font-weight:760;fill:#0f172a}',
        '  .meta-value{font-size:13.5px;fill:#26364d}',
        '  .meta-key{font-weight:650;fill:#102033}',
        '  .legend{font-size:13px;font-weight:560}',
        '  .small{font-size:12px;fill:#475569}',
        '  .column{font-size:12px;fill:#334155;font-weight:620}',
        '  .group{font-size:15px;font-weight:760}',
        '  .row{font-size:13px}',
        '  .value{font-size:13px;font-weight:600}',
        '  .fast{}',
        ']]></style>',
    ]
    for engine_id, logo_source in logo_sources.items():
        if logo_source:
            view_box, source_width, source_height = engine_logo_symbol_frame(engine_id, presentation)
            parts.append(f'<symbol id="engine-logo-{engine_id}" viewBox="{view_box}">')
            parts.append(f'<image href="{logo_source}" x="0" y="0" width="{source_width}" height="{source_height}" preserveAspectRatio="xMidYMid meet"/>')
            parts.append('</symbol>')
    parts.extend([
        '</defs>',
        '<rect width="100%" height="100%" fill="#f6f8fb"/>',
        f'<rect x="{outer_x}" y="{outer_y}" width="{outer_width}" height="{height - 48}" rx="14" fill="#ffffff" stroke="#d9e0ea"/>',
        f'<text x="{content_left}" y="58" class="title">{escape(case["display_name"])}</text>',
    ])
    meta_block_x = 40
    meta_label_x = content_left
    meta_value_x = 150
    meta_divider_x = 126
    meta_y = meta_block_y
    parts.append(f'<rect x="{meta_block_x}" y="{meta_block_y}" width="1192" height="{meta_total_height}" rx="9" fill="#fbfdff" stroke="#e2e8f0"/>')
    for row_index, (label, lines, row_kind, row_height) in enumerate(meta_rows):
        row_center = meta_y + row_height / 2
        parts.append(f'<text x="{meta_label_x}" y="{row_center:.1f}" class="meta-label">{escape(label)}</text>')
        if row_kind == "engines":
            engine_row_y = row_center - (len(legend_rows) - 1) * 11
            for engine_row in legend_rows:
                engine_x = meta_value_x
                for engine_id in engine_row:
                    color = colors.get(engine_id, "#555555")
                    engine_label = escape(engine_display_name(contracts, engine_id))
                    logo_width = append_engine_logo(parts, logo_sources, engine_id, engine_x, engine_row_y, 18, color, presentation)
                    parts.append(f'<text x="{engine_x + logo_width + 9:.1f}" y="{engine_row_y:.1f}" class="meta-value">{engine_label}</text>')
                    engine_x += max(112, logo_width + len(engine_label) * 6.9 + 31)
                engine_row_y += 22
        else:
            line_y = row_center - (len(lines) - 1) * 9
            for segments in lines:
                append_meta_text(parts, meta_value_x, f"{line_y:.1f}", segments)
                line_y += 18
        meta_y += row_height
        if row_index < len(meta_rows) - 1:
            parts.append(f'<line x1="{meta_block_x}" y1="{meta_y:.1f}" x2="{meta_block_x + 1192}" y2="{meta_y:.1f}" stroke="#edf2f7" stroke-width="1"/>')
    parts.append(f'<line x1="{meta_divider_x}" y1="{meta_block_y + 4}" x2="{meta_divider_x}" y2="{meta_block_y + meta_total_height - 5}" stroke="#d9e2ee" stroke-width="1"/>')

    for group_index, thread_count in enumerate(thread_counts):
        panel_column = group_index % panel_columns
        panel_row = group_index // panel_columns
        panel_x = group_x + panel_column * (panel_width + group_gap)
        panel_y = group_top + panel_row * (panel_height + group_gap)
        row_x = panel_x + (32 if panel_columns == 1 else 24)
        engine_text_x = panel_x + (74 if panel_columns == 1 else 66)
        plot_left = panel_x + (298 if panel_columns == 1 else 238)
        plot_width = 650 if panel_columns == 1 else max(160.0, panel_width - 354)
        value_x = panel_x + (1156 if panel_columns == 1 else panel_width - 24)
        group_rows = [row for row in sorted_rows if int(row["thread_count"]) == thread_count]
        fill = "#fbfdff" if group_index % 2 == 0 else "#ffffff"
        thread_label = "thread" if thread_count == 1 else "threads"
        axis_label_y = panel_y + 24
        axis_y = panel_y + 44
        grid_top = axis_y
        grid_bottom = panel_y + panel_height - 14
        parts.append(f'<rect x="{panel_x:.1f}" y="{panel_y:.1f}" width="{panel_width:.1f}" height="{panel_height}" rx="10" fill="{fill}" stroke="#e2e8f0"/>')
        parts.append(f'<text x="{row_x:.1f}" y="{panel_y + 24:.1f}" class="group">{thread_count} {thread_label}</text>')
        for tick in axis_ticks:
            tick_x = plot_left + (tick / axis_max) * plot_width
            parts.append(f'<text x="{tick_x:.1f}" y="{axis_label_y:.1f}" class="small tab" text-anchor="middle">{tick:.0f}</text>')
        parts.append(f'<text x="{value_x:.1f}" y="{axis_label_y:.1f}" class="small" text-anchor="end">ms/frame</text>')
        parts.append(f'<line x1="{plot_left:.1f}" y1="{axis_y:.1f}" x2="{plot_left + plot_width:.1f}" y2="{axis_y:.1f}" stroke="#cfd8e3" stroke-width="1"/>')
        for tick in axis_ticks:
            tick_x = plot_left + (tick / axis_max) * plot_width
            parts.append(f'<line x1="{tick_x:.1f}" y1="{axis_y - 6:.1f}" x2="{tick_x:.1f}" y2="{axis_y + 6:.1f}" stroke="#cfd8e3" stroke-width="1"/>')
            parts.append(f'<line x1="{tick_x:.1f}" y1="{grid_top:.1f}" x2="{tick_x:.1f}" y2="{grid_bottom:.1f}" stroke="#e8edf4" stroke-width="1"/>')
        row_y = panel_y + 62
        for row_index, row in enumerate(group_rows):
            value = float(row["median_ms_per_step"])
            bar_width = max(3.0, (value / axis_max) * plot_width)
            color = colors.get(row["engine_id"], "#555555")
            label = escape(engine_display_name(contracts, row["engine_id"]))
            value_label = escape(f"{value:.2f} ms")
            if row_index == 0 and len(group_rows) > 1:
                parts.append(f'<rect x="{panel_x + 16:.1f}" y="{row_y - 13:.1f}" width="{panel_width - 32:.1f}" height="26" rx="6" fill="#fff7ed" stroke="#fed7aa" stroke-width="1"/>')
                row_class = "row fast"
                value_class = "value tab fast"
            else:
                row_class = "row"
                value_class = "value tab"
            append_engine_logo(parts, logo_sources, row["engine_id"], row_x, row_y, 20, color, presentation)
            parts.append(f'<text x="{engine_text_x:.1f}" y="{row_y:.1f}" class="{row_class}">{label}</text>')
            parts.append(f'<rect x="{plot_left:.1f}" y="{row_y - 7:.1f}" width="{plot_width:.1f}" height="14" rx="4" fill="#edf2f7"/>')
            parts.append(f'<rect x="{plot_left:.1f}" y="{row_y - 7:.1f}" width="{bar_width:.1f}" height="14" rx="4" fill="{color}"/>')
            parts.append(f'<text x="{value_x:.1f}" y="{row_y:.1f}" class="{value_class}" text-anchor="end">{value_label}</text>')
            row_y += row_step

    parts.append("</svg>")
    write_text_lf(path, "\n".join(parts) + "\n")

def write_result_indexes(contracts):
    records = discover_index_records(contracts)
    results_root = repo_path("results")
    write_results_root_index(results_root / "README.md", results_root, records)
    for case_slug in sorted({record["case_slug"] for record in records}):
        case_records = [record for record in records if record["case_slug"] == case_slug]
        case_dir = results_root / case_slug
        write_case_index(case_dir / "README.md", case_dir, case_records)
        for cpu_slug in sorted({record["cpu_slug"] for record in case_records}):
            cpu_records = [record for record in case_records if record["cpu_slug"] == cpu_slug]
            cpu_dir = case_dir / cpu_slug
            write_cpu_index(cpu_dir / "README.md", cpu_dir, cpu_records)

def discover_index_records(contracts):
    records = []
    results_root = repo_path("results")
    for manifest_path in sorted(results_root.glob("*/*/*/manifest.json")):
        result_dir = manifest_path.parent
        manifest = load_json(manifest_path)
        manifest_status = validate_source_manifest_contract(contracts, manifest, result_dir)
        if manifest_status["status"] != "ok":
            continue
        host_source_status = validate_publishable_host_metadata(manifest["host"])
        if host_source_status["status"] != "ok":
            continue
        case = contracts["case_by_id"][manifest["case_id"]]
        records.append({
            "case_slug": case["slug"],
            "case_name": case["display_name"],
            "cpu_slug": result_dir.parent.name,
            "cpu_name": manifest["host"]["cpu"]["model"],
            "run_slug": result_dir.name,
            "result_dir": result_dir,
            "manifest": manifest,
            "summary_svg": result_dir / "summary.svg",
            "normalized_csv": result_dir / "normalized.csv",
            "summary_csv": result_dir / "summary.csv",
            "report_md": result_dir / "report.md",
        })
    return records

def write_results_root_index(path, base_dir, records):
    lines = [
        "# Benchmark Results",
        "",
        "Results are grouped by benchmark case, CPU model, and run state. Each run folder contains the manifest, normalized CSV, summary CSV, and SVG chart for that run.",
        "",
    ]
    if records:
        lines.extend(markdown_table(
            ["Case", "CPU", "Run", "Chart", "Report"],
            [
                [
                    record["case_name"],
                    record["cpu_name"],
                    record["run_slug"],
                    markdown_link("summary.svg", record["summary_svg"], base_dir),
                    markdown_link("report", record["report_md"], base_dir),
                ]
                for record in sorted(records, key=index_record_key)
            ],
            ["left", "left", "left", "left", "left"]
        ))
    else:
        lines.append("No organized benchmark results are available yet.")
    lines.append("")
    write_text_lf(path, "\n".join(lines) + "\n")

def write_case_index(path, base_dir, records):
    case_name = records[0]["case_name"] if records else path.parent.name
    lines = [
        f"# {case_name}",
        "",
        "Runs for this benchmark case, grouped by CPU model.",
        "",
    ]
    if records:
        lines.extend(markdown_table(
            ["CPU", "Run", "Chart", "Report"],
            [
                [
                    record["cpu_name"],
                    record["run_slug"],
                    markdown_link("summary.svg", record["summary_svg"], base_dir),
                    markdown_link("report", record["report_md"], base_dir),
                ]
                for record in sorted(records, key=index_record_key)
            ],
            ["left", "left", "left", "left"]
        ))
    else:
        lines.append("No runs are available for this case yet.")
    lines.append("")
    write_text_lf(path, "\n".join(lines) + "\n")

def write_cpu_index(path, base_dir, records):
    cpu_name = records[0]["cpu_name"] if records else path.parent.name
    lines = [
        f"# {cpu_name}",
        "",
        "Runs for this CPU model.",
        "",
    ]
    if records:
        lines.extend(markdown_table(
            ["Run", "Host", "Repeats", "Threads", "Chart", "Report"],
            [
                [
                    record["run_slug"],
                    host_label(record["manifest"]["host"]),
                    record["manifest"]["repeat_count"],
                    ", ".join(str(item) for item in record["manifest"]["thread_counts"]),
                    markdown_link("summary.svg", record["summary_svg"], base_dir),
                    markdown_link("report", record["report_md"], base_dir),
                ]
                for record in sorted(records, key=index_record_key)
            ],
            ["left", "left", "right", "left", "left", "left"]
        ))
    else:
        lines.append("No runs are available for this CPU yet.")
    lines.append("")
    write_text_lf(path, "\n".join(lines) + "\n")

def markdown_link(label, target, base_dir):
    relative = os.path.relpath(target, base_dir).replace("\\", "/")
    return f"[{label}]({relative})"

def index_record_key(record):
    return (record["case_slug"], record["cpu_slug"], record["run_slug"])

def sorted_report_rows(rows, report, presentation):
    engine_order = presentation.get("engine_order", [])
    engine_order_index = {engine_id: index for index, engine_id in enumerate(engine_order)}
    return sorted(rows, key=lambda row: (
        int(row["thread_count"]),
        float(row["median_ms_per_step"]),
        engine_order_index.get(row["engine_id"], len(engine_order)),
        row["engine_id"]
    ))
