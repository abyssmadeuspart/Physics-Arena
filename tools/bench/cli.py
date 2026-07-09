import platform
import sys

from .common import emit
from .reports import command_index, command_report
from .runner import command_run
from .visual import command_visual_run

def command_help():
    emit("bench_component", "status", "detail")
    usage_lines = [
        "./bench.sh",
        "./bench.sh help",
        "./bench.sh run <case> [--engine <id> ...] [--engine-set <set>] [--thread-count <n> ...] [--max-thread-count <n>] --repeats <count>",
        "./bench.sh visual-run <case> [--engine <id> ...] [--engine-set <set>] [--thread-count <n> ...] [--max-thread-count <n>] --repeats <count>",
        "./bench.sh report results/<case>/<cpu>/<run>",
        "./bench.sh index",
    ]
    for usage_line in usage_lines:
        emit("usage_public", "ok", usage_line)
    emit("usage_note", "ok", "--max-thread-count uses baseline checkpoints plus higher host checkpoints")
    return 0

def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if sys.version_info < (3, 10):
        emit("tool_python", "tool_missing", f"Python 3.10+ required; actual={platform.python_version()}")
        return 2
    if not argv:
        return command_help()
    command = argv[0]
    args = argv[1:]
    commands = {
        "run": command_run,
        "visual-run": command_visual_run,
        "report": command_report,
        "index": command_index,
        "help": lambda command_args: command_help()
    }
    if command not in commands:
        emit("bench_component", "invalid_result", f"unknown_command={command}")
        return 2
    return commands[command](args)
