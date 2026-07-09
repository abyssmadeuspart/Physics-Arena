def parse_positive_int(value):
    try:
        parsed = int(value)
    except ValueError:
        return {"status": "invalid_result", "detail": value, "value": 0}
    if parsed <= 0:
        return {"status": "invalid_result", "detail": value, "value": 0}
    return {"status": "ok", "detail": value, "value": parsed}

def option_value(args, name, default):
    for index, arg in enumerate(args):
        if arg == name and index + 1 < len(args):
            return args[index + 1]
    return default

def parse_value_options(args, allowed_options):
    values = {option: [] for option in allowed_options}
    entries = []
    positionals = []
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in allowed_options:
            if index + 1 >= len(args):
                return {"status": "invalid_result", "detail": f"missing_value={arg}", "values": values, "entries": entries, "positionals": positionals}
            value = args[index + 1]
            values[arg].append(value)
            entries.append({"name": arg, "value": value})
            index += 2
            continue
        if arg.startswith("--"):
            return {"status": "invalid_result", "detail": f"unknown_arg={arg}", "values": values, "entries": entries, "positionals": positionals}
        positionals.append(arg)
        index += 1
    return {"status": "ok", "detail": "", "values": values, "entries": entries, "positionals": positionals}

def single_option_value(options, name, default):
    values = options["values"].get(name, [])
    if len(values) > 1:
        return {"status": "invalid_result", "detail": f"duplicate_option={name}", "value": default}
    if values:
        return {"status": "ok", "detail": values[0], "value": values[0]}
    return {"status": "ok", "detail": "default", "value": default}
