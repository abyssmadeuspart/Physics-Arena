#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
default_case_slug="box-container-pile-10k"

print_help() {
  cat <<'EOF'
Physics Arena

Usage:
  ./bench.sh
  ./bench.sh help
  ./bench.sh run <case-slug> [--engine <engine-id> ...] [--engine-set <set>] [--thread-count <count> ...] [--max-thread-count <count>] [--repeats <count>]
  ./bench.sh visual-run <case-slug> [--engine <engine-id> ...] [--engine-set <set>] [--thread-count <count> ...] [--max-thread-count <count>] [--repeats <count>]
  ./bench.sh report results/<case>/<cpu>/<run>
  ./bench.sh index

Options:
  --engine <id>             Add one engine. Repeat for multiple engines.
  --engines <a,b,c>         Add several engines as a comma-separated list.
  --engine-set <set>        Use a configured engine set.
  --thread-count <count>    Run one exact thread count. Repeat for more.
  --threads <a,b,c>         Run exact thread counts from a comma-separated list.
  --max-thread-count <n>    Run baseline checkpoints up to n, plus higher host checkpoints.
  --repeats <count>         Repeats per engine and thread count.

Defaults:
  run uses --engine-set default_release.
  visual-run uses --engine-set default_visual_release.
  thread selection uses baseline checkpoints and adds the host maximum above the baseline.
  box-container-pile-10k uses 3 repeats unless --repeats is set.

Current default engines:
  box3d joltphysics bepuphysics2 rapier3d avian3d unity_physics physx34 nvidia_physx34 nvidia_physx5

Unreal Engine Chaos is opt-in because it is much slower than the default comparison route.

Examples:
  ./bench.sh run box-container-pile-10k --engine bepuphysics2 --engine joltphysics --engine physx34 --engine nvidia_physx34 --engine nvidia_physx5 --max-thread-count 4 --repeats 3
  ./bench.sh visual-run box-container-pile-10k --engine unity_physics --thread-count 1 --repeats 1
  ./bench.sh index
  ./bench.sh run box-container-pile-10k --engine unreal_chaos --threads 1,3,4,5,6,8,16,24 --repeats 1
EOF
}

public_error() {
  printf 'bench_component,status,detail\n'
  printf 'public_command,invalid_result,%s\n' "$1"
  exit 2
}

python_command=()
if [[ -n "${PYTHON_EXE:-}" ]]; then
  python_command=("$PYTHON_EXE")
fi

if [[ "${#python_command[@]}" -eq 0 ]]; then
  if command -v python3 >/dev/null 2>&1; then
    python_command=("$(command -v python3)")
  elif command -v python >/dev/null 2>&1; then
    python_command=("$(command -v python)")
  elif command -v py >/dev/null 2>&1; then
    python_command=("$(command -v py)" "-3")
  else
    printf 'bench_component,status,detail\n'
    printf 'tool_python,tool_missing,set PYTHON_EXE or add python3/python/py to PATH\n'
    exit 2
  fi
fi

python_script_path() {
  local script_path="$1"
  case "${python_command[0]}" in
    *.exe|*.EXE)
      if command -v wslpath >/dev/null 2>&1; then
        wslpath -w "$script_path"
        return
      fi
      ;;
  esac
  printf '%s\n' "$script_path"
}

bench_py="$(python_script_path "$repo_root/tools/bench.py")"

if [[ "$#" -gt 0 ]]; then
  command_name="$1"
  shift
  case "$command_name" in
    help|-h|--help)
      print_help
      exit 0
      ;;
    run|visual-run)
      if [[ "$#" -lt 1 ]]; then
        public_error "${command_name}_case=<case-slug> required"
      fi
      case_slug="$1"
      shift
      run_args=()
      while [[ "$#" -gt 0 ]]; do
        case "$1" in
          --engine|--engine-set|--engines|--thread-count|--threads|--max-thread-count|--repeats)
            if [[ "$#" -lt 2 ]]; then
              public_error "missing_value=$1"
            fi
            run_args+=("$1" "$2")
            shift 2
            ;;
          *)
            public_error "${command_name}_arg=$1 allowed=--engine/--engine-set/--thread-count/--engines/--threads/--max-thread-count/--repeats"
            ;;
        esac
      done
      exec "${python_command[@]}" "$bench_py" "$command_name" "$case_slug" "${run_args[@]}"
      ;;
    report)
      if [[ "$#" -ne 1 ]]; then
        public_error "report_path=results/<run-id> required"
      fi
      case "$1" in
        results/*)
          exec "${python_command[@]}" "$bench_py" report "$1"
          ;;
        *)
          public_error "report_path=results/<run-id> required"
          ;;
      esac
      ;;
    index)
      if [[ "$#" -ne 0 ]]; then
        public_error "index_args=none"
      fi
      exec "${python_command[@]}" "$bench_py" index
      ;;
    *)
      printf 'bench_component,status,detail\n'
      printf 'public_command,invalid_result,command=%s allowed=help/run/visual-run/report/index\n' "$command_name"
      exit 2
      ;;
  esac
fi

run_args=(run "$default_case_slug" --engine-set default_release)

set +e
run_output="$("${python_command[@]}" "$bench_py" "${run_args[@]}" 2>&1)"
run_status=$?
set -e
printf '%s\n' "$run_output"
if [[ "$run_status" -ne 0 ]]; then
  exit "$run_status"
fi

result_path=""
while IFS=, read -r component status detail; do
  if [[ "$component" == "run_result" && "$status" == "ok" ]]; then
    result_path="$detail"
  fi
done <<< "$run_output"

if [[ -z "$result_path" ]]; then
  public_error "missing_run_result"
fi

printf 'bench_default,ok,%s\n' "$result_path"
