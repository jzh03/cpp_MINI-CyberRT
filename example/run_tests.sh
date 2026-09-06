#!/usr/bin/env bash
# Run independently built test executables one at a time.  Each executable is
# placed in its own process group so a timeout can reap only that test's child
# processes, without touching unrelated middleware processes.

set -u

usage() {
  echo "usage: $0 --bin-dir DIR --timeout SECONDS TARGET [TARGET ...]" >&2
}

bin_dir=""
timeout_seconds=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin-dir)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      bin_dir="$2"
      shift 2
      ;;
    --timeout)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      timeout_seconds="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --*)
      echo "unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      break
      ;;
  esac
done

[[ -n "$bin_dir" && -n "$timeout_seconds" && $# -gt 0 ]] || {
  usage
  exit 2
}
[[ "$timeout_seconds" =~ ^[1-9][0-9]*$ ]] || {
  echo "timeout must be a positive integer: $timeout_seconds" >&2
  exit 2
}
command -v setsid >/dev/null 2>&1 || {
  echo "test runner requires setsid to clean timed-out test process groups" >&2
  exit 2
}

declare -a passed=()
declare -a failed=()
declare -a timed_out=()
current_pid=""
watchdog_pid=""

terminate_process_group() {
  local pid="$1"
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 0
  if kill -0 -- "-$pid" 2>/dev/null; then
    kill -TERM -- "-$pid" 2>/dev/null || true
    sleep 1
    kill -KILL -- "-$pid" 2>/dev/null || true
  fi
}

cleanup_on_signal() {
  if [[ -n "$current_pid" ]]; then
    terminate_process_group "$current_pid"
    wait "$current_pid" 2>/dev/null || true
  fi
  if [[ -n "$watchdog_pid" ]]; then
    kill "$watchdog_pid" 2>/dev/null || true
    wait "$watchdog_pid" 2>/dev/null || true
  fi
  exit 130
}
trap cleanup_on_signal INT TERM

for target in "$@"; do
  if [[ "$target" == */* ]]; then
    binary="$target"
  else
    binary="$bin_dir/$target"
  fi

  if [[ ! -x "$binary" ]]; then
    echo "[FAIL] $target: missing or non-executable binary: $binary" >&2
    failed+=("$target (missing binary)")
    continue
  fi

  echo "[RUN ] $target (timeout ${timeout_seconds}s)"
  setsid "$binary" &
  current_pid=$!

  # `kill -0` reports success for an exited child which is waiting to be
  # reaped, so polling it falsely turns short successful tests into timeouts.
  # Wait for the child directly and let this watchdog enforce the deadline.
  (
    sleep "$timeout_seconds"
    echo "[TIME] $target exceeded ${timeout_seconds}s; terminating its process group" >&2
    terminate_process_group "$current_pid"
  ) &
  watchdog_pid=$!

  if wait "$current_pid"; then
    status=0
  else
    status=$?
  fi
  current_pid=""

  if kill -0 "$watchdog_pid" 2>/dev/null; then
    kill "$watchdog_pid" 2>/dev/null || true
  fi
  if wait "$watchdog_pid"; then
    did_timeout=1
  else
    did_timeout=0
  fi
  watchdog_pid=""

  if (( did_timeout )); then
    timed_out+=("$target")
  elif (( status == 0 )); then
    echo "[PASS] $target"
    passed+=("$target")
  elif (( status >= 128 )); then
    signal_number=$((status - 128))
    echo "[FAIL] $target exited from signal $signal_number (status $status)" >&2
    failed+=("$target (signal $signal_number)")
  else
    echo "[FAIL] $target exited with status $status" >&2
    failed+=("$target (status $status)")
  fi
done

echo "Test runner summary: passed=${#passed[@]} failed=${#failed[@]} timed_out=${#timed_out[@]}"
if (( ${#passed[@]} )); then
  printf '  passed: %s\n' "${passed[*]}"
fi
if (( ${#failed[@]} )); then
  printf '  failed: %s\n' "${failed[*]}" >&2
fi
if (( ${#timed_out[@]} )); then
  printf '  timed out: %s\n' "${timed_out[*]}" >&2
fi

(( ${#failed[@]} == 0 && ${#timed_out[@]} == 0 ))
