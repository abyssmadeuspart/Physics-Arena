import json
import os
import re
import shutil
import time

from .common import REPO_ROOT, command_path, run_process

HOST_REQUIRED_PATHS = [
    "cpu.model",
    "cpu.logical_threads",
    "memory.total_gb",
    "memory.modules",
    "memory.configured_clock_mhz",
    "os.name",
    "os.version",
    "metadata_sources",
]

PUBLIC_HOST_SOURCE_VALUES = {
    "windows_cim",
    "windows_processor_topology_api",
}
PUBLIC_HOST_SOURCE_PREFIXES = (
    "hardware_report:",
)
NONPUBLIC_HOST_SOURCE_VALUES = {
    "manual_override",
    "local_override",
}

SMBIOS_MEMORY_TYPES = {
    20: "DDR",
    21: "DDR2",
    24: "DDR3",
    26: "DDR4",
    34: "DDR5",
}

def collect_host_metadata():
    windows_status = query_windows_host_metadata()
    if windows_status["status"] != "ok":
        return windows_status

    normalize_status = normalize_windows_host_metadata(windows_status["payload"])
    if normalize_status["status"] != "ok":
        return normalize_status

    host = normalize_status["host"]
    validation_status = validate_host_metadata(host)
    if validation_status["status"] != "ok":
        return validation_status
    validation_status = validate_publishable_host_metadata(host)
    if validation_status["status"] != "ok":
        return validation_status
    return host_result(host)

def host_result(host):
    return {
        "status": "ok",
        "detail": host_label(host),
        "host": host,
        "cpu_slug": cpu_slug(host),
    }

def query_windows_host_metadata():
    powershell = powershell_command()
    if not powershell:
        return {"status": "tool_missing", "detail": "add powershell.exe or pwsh.exe to PATH"}
    script_status = windows_host_probe_script_path(powershell)
    if script_status["status"] != "ok":
        return script_status
    result = run_process([powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script_status["path"]])
    if result["status"] != "ok":
        detail = result["stderr"].decode("utf-8", errors="replace").strip()
        if not detail:
            detail = result["stdout"].decode("utf-8", errors="replace").strip()
        return {"status": "tool_missing", "detail": f"windows_cim_failed={detail}"}
    try:
        payload = json.loads(result["stdout"].decode("utf-8", errors="replace"))
    except json.JSONDecodeError as exc:
        return {"status": "invalid_result", "detail": f"windows_cim_json={exc}"}
    return {"status": "ok", "detail": "windows_cim", "payload": payload}

def powershell_command():
    for command_name in ["powershell.exe", "pwsh.exe"]:
        command = shutil.which(command_name)
        if command:
            return command
    return ""

def windows_host_probe_script_path(powershell):
    script_path = REPO_ROOT / "tools" / "bench" / "host_probe_windows.ps1"
    if os.name == "nt" or not str(powershell).lower().endswith(".exe"):
        return {"status": "ok", "detail": "host_probe_windows", "path": str(script_path)}
    wslpath = command_path("WSLPATH_EXE", ["wslpath"])
    if not wslpath:
        return {"status": "tool_missing", "detail": "set WSLPATH_EXE or add wslpath to PATH for Windows PowerShell interop", "path": ""}
    result = run_process([wslpath, "-w", str(script_path)])
    if result["status"] != "ok":
        detail = result["stderr"].decode("utf-8", errors="replace").strip()
        return {"status": "tool_missing", "detail": f"wslpath_failed={detail}", "path": ""}
    path_text = result["stdout"].decode("utf-8", errors="replace").strip()
    if not path_text:
        return {"status": "tool_missing", "detail": "wslpath_empty", "path": ""}
    return {"status": "ok", "detail": "host_probe_windows", "path": path_text}

def normalize_windows_host_metadata(payload):
    processor = first_record(payload.get("processor", {}))
    processor_topology_status = str(payload.get("processor_topology_status", "")).strip()
    processor_topology = list_records(payload.get("processor_topology", []))
    memory_records = list_records(payload.get("memory", []))
    operating_system = first_record(payload.get("operating_system", {}))
    computer_system = first_record(payload.get("computer_system", {}))
    baseboard = first_record(payload.get("baseboard", {}))
    bios = first_record(payload.get("bios", {}))

    modules = [normalize_memory_module(record) for record in memory_records]
    total_gb = integer_gb(computer_system.get("TotalPhysicalMemory", 0))
    if total_gb <= 0:
        total_gb = sum(module.get("capacity_gb", 0) for module in modules)

    configured_clock = first_positive_number([module.get("configured_clock_mhz", 0) for module in modules])
    memory_type = first_text([module.get("type", "") for module in modules])

    host = {
        "cpu": {
            "model": clean_cpu_model(processor.get("Name", "")),
            "physical_cores": int_value(processor.get("NumberOfCores", 0)),
            "logical_threads": int_value(processor.get("NumberOfLogicalProcessors", 0)),
            "max_clock_mhz": int_value(processor.get("MaxClockSpeed", 0)),
            "current_clock_mhz": int_value(processor.get("CurrentClockSpeed", 0)),
        },
        "memory": {
            "total_gb": total_gb,
            "type": memory_type,
            "configured_clock_mhz": configured_clock,
            "modules": modules,
        },
        "os": {
            "name": str(operating_system.get("Caption", "")).strip(),
            "version": str(operating_system.get("Version", "")).strip(),
            "build": str(operating_system.get("BuildNumber", "")).strip(),
            "architecture": str(operating_system.get("OSArchitecture", "")).strip(),
        },
        "motherboard": {
            "model": motherboard_model(baseboard),
            "bios_version": str(bios.get("SMBIOSBIOSVersion", "")).strip(),
            "bios_date": wmi_datetime_date(bios.get("ReleaseDate", "")),
        },
        "metadata_sources": {
            "cpu.model": "windows_cim",
            "cpu.physical_cores": "windows_cim",
            "cpu.logical_threads": "windows_cim",
            "cpu.max_clock_mhz": "windows_cim",
            "cpu.current_clock_mhz": "windows_cim",
            "memory.total_gb": "windows_cim",
            "memory.type": "windows_cim",
            "memory.configured_clock_mhz": "windows_cim",
            "memory.modules": "windows_cim",
            "os.name": "windows_cim",
            "os.version": "windows_cim",
            "os.build": "windows_cim",
            "os.architecture": "windows_cim",
            "motherboard.model": "windows_cim",
            "motherboard.bios_version": "windows_cim",
            "motherboard.bios_date": "windows_cim",
        },
    }
    apply_processor_topology(host, processor_topology_status, processor_topology)
    validation_status = validate_host_metadata(host)
    if validation_status["status"] != "ok":
        return validation_status
    return {"status": "ok", "detail": "windows_cim", "host": host}

def apply_processor_topology(host, topology_status, records):
    normalized_records = []
    for record in records:
        efficiency_class = int_value(record.get("efficiency_class", 0))
        logical_processors = int_value(record.get("logical_processors", 0))
        if logical_processors > 0:
            normalized_records.append({
                "efficiency_class": efficiency_class,
                "logical_processors": logical_processors,
            })
    if topology_status != "ok" or not normalized_records:
        host["cpu"]["topology_status"] = "unavailable"
        host["metadata_sources"]["cpu.topology_status"] = "windows_processor_topology_api"
        return host

    physical_cores = len(normalized_records)
    logical_threads = sum(record["logical_processors"] for record in normalized_records)
    host["cpu"]["physical_cores"] = physical_cores
    host["cpu"]["logical_threads"] = logical_threads
    host["metadata_sources"]["cpu.physical_cores"] = "windows_processor_topology_api"
    host["metadata_sources"]["cpu.logical_threads"] = "windows_processor_topology_api"

    class_counts = {}
    for record in normalized_records:
        efficiency_class = record["efficiency_class"]
        class_counts[efficiency_class] = class_counts.get(efficiency_class, 0) + 1
    if len(class_counts) > 1:
        e_class = min(class_counts)
        p_class = max(class_counts)
        host["cpu"]["p_core_count"] = class_counts[p_class]
        host["cpu"]["e_core_count"] = class_counts[e_class]
        host["cpu"]["topology"] = f"{class_counts[p_class]}P+{class_counts[e_class]}E / {logical_threads}T"
        host["metadata_sources"]["cpu.p_core_count"] = "windows_processor_topology_api"
        host["metadata_sources"]["cpu.e_core_count"] = "windows_processor_topology_api"
        host["metadata_sources"]["cpu.topology"] = "windows_processor_topology_api"
        return host

    host["cpu"]["topology_status"] = "unavailable"
    host["metadata_sources"]["cpu.topology_status"] = "windows_processor_topology_api"
    return host

def normalize_memory_module(record):
    smbios_type = int_value(record.get("SMBIOSMemoryType", 0))
    return {
        "capacity_gb": integer_gb(record.get("Capacity", 0)),
        "configured_clock_mhz": int_value(record.get("ConfiguredClockSpeed", 0)),
        "speed_mhz": int_value(record.get("Speed", 0)),
        "type": SMBIOS_MEMORY_TYPES.get(smbios_type, f"smbios-{smbios_type}" if smbios_type else ""),
        "manufacturer": str(record.get("Manufacturer", "")).strip(),
        "part_number": str(record.get("PartNumber", "")).strip(),
        "slot": str(record.get("DeviceLocator", "")).strip(),
    }

def validate_host_metadata(host):
    if not isinstance(host, dict):
        return {"status": "invalid_result", "detail": "host_metadata_not_object"}
    for field_path in HOST_REQUIRED_PATHS:
        value = host_path_value(host, field_path)
        if value in ["", None, []]:
            return {"status": "invalid_result", "detail": f"missing_host_field={field_path}"}
        if isinstance(value, int) and value <= 0:
            return {"status": "invalid_result", "detail": f"invalid_host_field={field_path}"}
    return {"status": "ok", "detail": "host_metadata"}

def validate_publishable_host_metadata(host):
    source_status = validate_host_source_values(host)
    if source_status["status"] != "ok":
        return source_status
    sources = host.get("metadata_sources", {})
    for field_path in displayed_host_field_paths(host):
        value = host_path_value(host, field_path)
        if value in ["", None, []]:
            continue
        source = sources.get(field_path, "")
        if not source:
            return {"status": "invalid_result", "detail": f"missing_host_source={field_path}"}
        if not publishable_host_source(source):
            return {"status": "invalid_result", "detail": f"nonpublishable_host_source={field_path}:{source}"}
    return {"status": "ok", "detail": host_source_coverage(host)}

def validate_host_source_values(host):
    sources = host.get("metadata_sources", {})
    if not isinstance(sources, dict):
        return {"status": "invalid_result", "detail": "metadata_sources_not_object"}
    for field_path, source in sources.items():
        source_text = str(source).strip()
        if source_text in NONPUBLIC_HOST_SOURCE_VALUES:
            return {"status": "invalid_result", "detail": f"nonpublishable_host_source={field_path}:{source_text}"}
        if not publishable_host_source(source_text):
            return {"status": "invalid_result", "detail": f"unsupported_host_source={field_path}:{source_text}"}
    return {"status": "ok", "detail": "host_sources"}

def publishable_host_source(source):
    source_text = str(source).strip()
    if source_text in PUBLIC_HOST_SOURCE_VALUES:
        return True
    return any(source_text.startswith(prefix) for prefix in PUBLIC_HOST_SOURCE_PREFIXES)

def displayed_host_field_paths(host):
    paths = [
        "cpu.model",
        "cpu.logical_threads",
        "memory.total_gb",
        "memory.type",
        "memory.configured_clock_mhz",
        "os.name",
        "os.version",
        "os.architecture",
        "motherboard.model",
        "motherboard.bios_version",
    ]
    optional_paths = [
        "cpu.topology",
        "cpu.p_core_count",
        "cpu.e_core_count",
        "cpu.p_core_observed_mhz",
        "cpu.e_core_observed_mhz",
        "cpu.max_clock_mhz",
        "memory.channel_mode",
        "memory.observed_clock_mhz",
        "memory.effective_mt_s",
        "memory.timings",
        "memory.command_rate",
    ]
    return paths + [
        field_path for field_path in optional_paths
        if host_path_value(host, field_path) not in ["", None, []]
    ]

def host_source_coverage(host):
    sources = host.get("metadata_sources", {})
    counts = {}
    missing = 0
    for field_path in displayed_host_field_paths(host):
        value = host_path_value(host, field_path)
        if value in ["", None, []]:
            continue
        source = str(sources.get(field_path, "")).strip()
        if not source:
            missing += 1
            continue
        source_key = "hardware_report" if source.startswith("hardware_report:") else source
        counts[source_key] = counts.get(source_key, 0) + 1
    labels = [f"{key}={counts[key]}" for key in sorted(counts)]
    if missing:
        labels.append(f"missing={missing}")
    return ",".join(labels) if labels else "none"

def host_path_value(host, field_path):
    value = host
    for part in field_path.split("."):
        if not isinstance(value, dict) or part not in value:
            return ""
        value = value[part]
    return value

def first_record(value):
    if isinstance(value, list):
        if value:
            return value[0]
        return {}
    if isinstance(value, dict):
        return value
    return {}

def list_records(value):
    if isinstance(value, list):
        return [record for record in value if isinstance(record, dict)]
    if isinstance(value, dict):
        return [value]
    return []

def int_value(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0

def integer_gb(value):
    integer = int_value(value)
    if integer <= 0:
        return 0
    return round(integer / 1024 / 1024 / 1024)

def first_positive_number(values):
    for value in values:
        integer = int_value(value)
        if integer > 0:
            return integer
    return 0

def first_text(values):
    for value in values:
        text = str(value).strip()
        if text:
            return text
    return ""

def motherboard_model(baseboard):
    manufacturer = str(baseboard.get("Manufacturer", "")).strip()
    product = str(baseboard.get("Product", "")).strip()
    if manufacturer and product:
        if product.lower().startswith(manufacturer.lower()):
            return product
        return f"{manufacturer} {product}"
    return manufacturer or product

def wmi_datetime_date(value):
    text = str(value).strip()
    if len(text) >= 8 and text[:8].isdigit():
        return f"{text[0:4]}-{text[4:6]}-{text[6:8]}"
    match = re.fullmatch(r"/Date\(([0-9]+)\)/", text)
    if match:
        seconds = int(match.group(1)) / 1000
        return time.strftime("%Y-%m-%d", time.gmtime(seconds))
    return text

def clean_cpu_model(value):
    text = str(value).strip()
    text = re.sub(r"\s+", " ", text)
    text = text.replace("13th Gen ", "")
    text = text.replace("(R)", "")
    text = text.replace("(TM)", "")
    text = text.replace(" CPU", "")
    return re.sub(r"\s+", " ", text).strip()

def slug_text(value):
    text = str(value).lower()
    text = text.replace("(r)", "").replace("(tm)", "")
    text = re.sub(r"\b\d+(st|nd|rd|th)\s+gen\b", "", text)
    text = re.sub(r"\bcpu\b", "", text)
    text = re.sub(r"[^a-z0-9]+", "-", text)
    return text.strip("-")

def cpu_slug(host):
    slug = slug_text(host.get("cpu", {}).get("model", ""))
    if slug:
        return slug
    return "unknown-cpu"

def run_state_slug(host, repeat_count, thread_selection_token="", run_date=None):
    tokens = [run_date or time.strftime("%Y-%m-%d_%H%M")]
    memory_token = memory_slug_token(host.get("memory", {}))
    if memory_token:
        tokens.append(memory_token)
    if thread_selection_token:
        tokens.append(thread_selection_token)
    tokens.append(f"r{repeat_count}")
    return "_".join(tokens)

def memory_slug_token(memory):
    memory_type = slug_text(memory.get("type", ""))
    clock = int_value(memory.get("configured_clock_mhz", 0))
    parts = []
    if memory_type:
        parts.append(memory_type)
    if clock > 0:
        parts.append(str(clock))
    return "-".join(parts)

def host_label(host):
    lines = host_label_lines(host)
    return "; ".join(lines) if lines else "not recorded"

def host_label_lines(host):
    cpu = host.get("cpu", {})
    memory = host.get("memory", {})
    os_info = host.get("os", {})
    motherboard = host.get("motherboard", {})
    lines = []
    cpu_label = cpu_summary_label(cpu)
    clock_label = core_clock_label(cpu)
    if cpu_label:
        if clock_label:
            lines.append(f"{cpu_label}; {clock_label}")
        else:
            lines.append(cpu_label)
    memory_label = memory_summary_label(memory)
    if memory_label:
        lines.append(memory_label)
    motherboard_label = motherboard_summary_label(motherboard)
    os_label = os_summary_label(os_info)
    board_os_parts = []
    if motherboard_label:
        board_os_parts.append(motherboard_label)
    if os_label:
        board_os_parts.append(os_label)
    if board_os_parts:
        lines.append("; ".join(board_os_parts))
    return lines

def cpu_summary_label(cpu):
    model = cpu.get("model", "")
    topology = cpu.get("topology", "")
    if not topology:
        p_core_count = int_value(cpu.get("p_core_count", 0))
        e_core_count = int_value(cpu.get("e_core_count", 0))
        logical_threads = int_value(cpu.get("logical_threads", 0))
        topology_parts = []
        if p_core_count > 0 and e_core_count > 0:
            topology_parts.append(f"{p_core_count}P+{e_core_count}E")
        if logical_threads > 0:
            topology_parts.append(f"{logical_threads}T")
        topology = " / ".join(topology_parts)
    if model and topology:
        return f"{model} ({topology})"
    return model or topology

def core_clock_label(cpu):
    p_core = ghz_range_label(cpu.get("p_core_observed_mhz", ""))
    e_core = ghz_range_label(cpu.get("e_core_observed_mhz", ""))
    labels = []
    if p_core:
        labels.append(f"P-cores {p_core}")
    if e_core:
        labels.append(f"E-cores {e_core}")
    if labels:
        return ", ".join(labels)
    max_clock = int_value(cpu.get("max_clock_mhz", 0))
    if max_clock > 0:
        return f"max {max_clock / 1000:.2f} GHz"
    return ""

def ghz_range_label(value):
    text = str(value).strip()
    if not text:
        return ""
    numbers = clock_values_mhz(text)
    if not numbers:
        return text
    labels = [f"{number / 1000:.2f} GHz" for number in numbers]
    return "-".join(label.replace(" GHz", "") for label in labels[:-1]) + (f"-{labels[-1]}" if len(labels) > 1 else labels[0])

def clock_values_mhz(value):
    numbers = []
    for item in re.findall(r"[0-9]+(?:\.[0-9]+)?", str(value)):
        number = float(item)
        if number < 100:
            number = number * 1000
        numbers.append(round(number))
    return numbers

def memory_summary_label(memory):
    parts = []
    total = int_value(memory.get("total_gb", 0))
    if total > 0:
        parts.append(f"{total} GB")
    memory_type = memory.get("type", "")
    if memory_type:
        parts.append(memory_type)
    channel = memory.get("channel_mode", "")
    if channel:
        parts.append(channel)
    effective_mt_s = int_value(memory.get("effective_mt_s", 0))
    if effective_mt_s > 0:
        parts.append(f"{effective_mt_s} MT/s")
    else:
        clock = int_value(memory.get("observed_clock_mhz", 0))
        if clock <= 0:
            clock = int_value(memory.get("configured_clock_mhz", 0))
        if clock > 0:
            parts.append(f"{clock} MHz")
    timings = memory.get("timings", "")
    if timings:
        parts.append(str(timings))
    command_rate = memory.get("command_rate", "")
    if command_rate:
        parts.append(f"CR {command_rate}" if not str(command_rate).upper().startswith("CR") else str(command_rate))
    return " ".join(parts)

def motherboard_summary_label(motherboard):
    model = motherboard.get("model", "")
    bios_version = motherboard.get("bios_version", "")
    if model and bios_version:
        return f"{model} BIOS {bios_version}"
    return model or bios_version

def os_summary_label(os_info):
    parts = []
    name = os_info.get("name", "")
    if name:
        parts.append(name)
    architecture = os_info.get("architecture", "")
    if architecture:
        parts.append(architecture)
    version = os_info.get("version", "")
    if version:
        parts.append(str(version))
    return " ".join(parts)
