#!/usr/bin/env bash
set -Eeuo pipefail
DEMO_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$DEMO_DIR/../.." && pwd)
original_args=("$@")
scenario=all
seconds=30
payload=1048576
build=1
usage() {
  echo "Usage: $0 [all|A|B|C|D|E] [--seconds 3..60] [--payload 1048576|4194304] [--no-build]"
}
while (($#)); do
  case "$1" in
    all|A|B|C|D|E) scenario=$1; shift ;;
    --seconds|--payload)
      (($# >= 2)) || { usage; exit 2; }
      if [[ $1 == --seconds ]]; then seconds=$2; else payload=$2; fi
      shift 2 ;;
    --no-build) build=0; shift ;;
    --help) usage; exit 0 ;;
    *) usage; exit 2 ;;
  esac
done
[[ $seconds =~ ^[0-9]+$ && ${#seconds} -le 2 ]] && ((seconds >= 3 && seconds <= 60)) || { usage; exit 2; }
[[ $payload == 1048576 || $payload == 4194304 ]] || { usage; exit 2; }
# A private IPC namespace avoids touching the host's global ConditionNotifier.
# Keep the network namespace unchanged: the two processes use real local DDS.
if [[ ${CMW_DEMO_IPC_ISOLATED:-0} != 1 ]]; then
  echo '[STAGE] entering private user/IPC namespace; host IPC objects remain untouched'
  exec unshare --user --map-root-user --ipc \
    env CMW_DEMO_IPC_ISOLATED=1 bash "$DEMO_DIR/run_demo.sh" "${original_args[@]}"
fi
mkdir -p "$DEMO_DIR/runs"
RUN_DIR=$(mktemp -d "$DEMO_DIR/runs/$(date +%Y%m%d-%H%M%S)-XXXXXX")
token=$(basename "$RUN_DIR" | tr '-' '_')
export CMW_PATH="$ROOT" CMW_DEMO_TRACE=1
BIN="$DEMO_DIR/build/bin/demo_transport"
declare -A pids=() files=() channels=() lines=() active=()
build_pid=
echo "[STAGE] run_dir=$RUN_DIR"
printf 'cwd=%q\nCMW_PATH=%q\nCMW_DEMO_TRACE=1\nscenario=%q seconds=%q payload=%q\n' \
  "$ROOT" "$ROOT" "$scenario" "$seconds" "$payload" > "$RUN_DIR/commands.txt"
printf 'ipc_namespace=%s\n' "$(readlink /proc/self/ns/ipc)" >> "$RUN_DIR/commands.txt"

show() {
  local name=$1 count
  count=$(wc -l < "${files[$name]}")
  if ((count > ${lines[$name]})); then
    sed -n "$((lines[$name] + 1)),${count}p" "${files[$name]}" |
      { grep -E '^\[(STAGE|PUB|SUB|DISCOVERY|ROUTE|CHECK|RESULT)\]' || [[ $? == 1 ]]; }
    lines[$name]=$count
  fi
}
show_all() {
  local name
  for name in "${!pids[@]}"; do
    show "$name"
    if grep -q '^\[RESULT\] FAIL' "${files[$name]}"; then fail "$name reported FAIL"; fi
  done
}
archive() {
  local name=$1 source="$ROOT/log/demo_${channels[$1]}_${pids[$1]}.log"
  if [[ -f $source ]]; then
    mv -n -- "$source" "$RUN_DIR/${name}.runtime.log"
    printf 'logger_archive %q -> %q\n' "$source" "$RUN_DIR/${name}.runtime.log" >> "$RUN_DIR/commands.txt"
  fi
}
cleanup() {
  local rc=$? name pid deadline
  trap - EXIT INT TERM
  if [[ -n $build_pid ]]; then
    # timeout owns a process group and forwards TERM to make and its compilers.
    kill -TERM "$build_pid" 2>/dev/null || true
    local build_rc=0
    wait "$build_pid" || build_rc=$?
    printf 'build_cleanup exit=%s\n' "$build_rc" >> "$RUN_DIR/commands.txt"
    if ((rc == 0 && build_rc != 0)); then rc=$build_rc; fi
  fi
  for name in "${!active[@]}"; do
    pid=${pids[$name]}
    if kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid" 2>/dev/null || true; fi
  done
  deadline=$((SECONDS + 10))
  for name in "${!active[@]}"; do
    pid=${pids[$name]}
    while kill -0 "$pid" 2>/dev/null && ((SECONDS < deadline)); do sleep 0.1; done
    if kill -0 "$pid" 2>/dev/null; then
      echo "[RESULT] FAIL cleanup timeout name=$name pid=$pid; terminating this child only"
      kill -KILL "$pid" 2>/dev/null || true
      rc=1
    fi
    local child_rc=0
    wait "$pid" || child_rc=$?
    printf 'cleanup name=%s pid=%s exit=%s\n' "$name" "$pid" "$child_rc" >> "$RUN_DIR/commands.txt"
    # Interrupted children may fail their minimum-message contract; preserve it.
    if ((rc == 0 && child_rc != 0)); then rc=$child_rc; fi
  done
  for name in "${!pids[@]}"; do show "$name"; archive "$name"; done
  # No unlink/ipcrm: normal runtime reference/Lease cleanup owns channel segments.
  # Report exact residual names from our own READY records, never delete strangers.
  local id
  for name in "${!pids[@]}"; do
    id=$(sed -n 's/.*channel_id=\([0-9]*\).*/\1/p' "${files[$name]}" | head -1)
    if [[ -n $id && -e /dev/shm/cmw_$id ]]; then
      echo "[CHECK] retained channel segment /dev/shm/cmw_$id (not deleted)"
      printf 'retained_segment=/dev/shm/cmw_%s\n' "$id" >> "$RUN_DIR/commands.txt"
    fi
  done
  echo "[STAGE] cleanup complete; child processes waited; logs=$RUN_DIR exit=$rc"
  exit "$rc"
}
trap cleanup EXIT
trap 'echo "[STAGE] Ctrl+C: requesting orderly child shutdown"; exit 130' INT
trap 'echo "[STAGE] SIGTERM: requesting orderly child shutdown"; exit 143' TERM
fail() { echo "[RESULT] FAIL $* logs=$RUN_DIR"; exit 1; }

if ((build)); then
  echo "[STAGE] build isolated demo"
  printf 'timeout --signal=TERM --kill-after=5s 300s make -C %q -f demo/Makefile -j2 demo-transport > %q 2>&1\n' "$ROOT/example" "$RUN_DIR/build.log" >> "$RUN_DIR/commands.txt"
  timeout --signal=TERM --kill-after=5s 300s \
    make -C "$ROOT/example" -f demo/Makefile -j2 demo-transport > "$RUN_DIR/build.log" 2>&1 &
  build_pid=$!
  build_rc=0
  wait "$build_pid" || build_rc=$?
  build_pid=
  printf 'build exit=%s\n' "$build_rc" >> "$RUN_DIR/commands.txt"
  ((build_rc == 0)) || fail "build failed or exceeded 300s (exit=$build_rc)"
fi
[[ -x $BIN ]] || fail "missing binary; run without --no-build"

launch() {
  local name=$1 role=$2 stage=$3 duration=$4 channel=$5
  local bytes=1024 hz=10
  if [[ $stage == C ]]; then bytes=$payload; hz=5; fi
  files[$name]="$RUN_DIR/$name.log"; channels[$name]=$channel; lines[$name]=0
  local args=("$BIN" --role "$role" --scenario "$stage" --channel "$channel"
              --payload "$bytes" --hz "$hz" --seconds "$duration")
  printf '%q ' "${args[@]}" >> "$RUN_DIR/commands.txt"
  printf '> %q 2>&1 &\n' "${files[$name]}" >> "$RUN_DIR/commands.txt"
  "${args[@]}" > "${files[$name]}" 2>&1 &
  pids[$name]=$!; active[$name]=1
  printf 'name=%s pid=%s\n' "$name" "${pids[$name]}" >> "$RUN_DIR/commands.txt"
}
event() {
  local name=$1 pattern=$2 timeout=$3 deadline=$((SECONDS + $3))
  while :; do
    show_all
    if grep -Eq "$pattern" "${files[$name]}"; then return; fi
    if ! kill -0 "${pids[$name]}" 2>/dev/null; then fail "$name exited before event: $pattern"; fi
    ((SECONDS < deadline)) || fail "$name timeout ${timeout}s waiting for $pattern"
    sleep 0.1
  done
}
finish() {
  local name=$1 deadline=$((SECONDS + $2)) rc=0
  while kill -0 "${pids[$name]}" 2>/dev/null; do
    show_all
    ((SECONDS < deadline)) || fail "$name exit timeout"
    sleep 0.1
  done
  wait "${pids[$name]}" || rc=$?
  unset 'active[$name]'
  show "$name"; archive "$name"
  printf 'wait name=%s pid=%s exit=%s\n' "$name" "${pids[$name]}" "$rc" >> "$RUN_DIR/commands.txt"
  ((rc == 0)) || fail "$name exit=$rc"
  grep -q '^\[RESULT\] PASS' "${files[$name]}" || fail "$name missing process PASS"
}
stop_pub() {
  local name=$1
  kill -0 "${pids[$name]}" 2>/dev/null || fail "$name did not remain running"
  echo "[STAGE] requesting orderly publisher shutdown pid=${pids[$name]}"
  printf 'kill -TERM %s\n' "${pids[$name]}" >> "$RUN_DIR/commands.txt"
  kill -TERM "${pids[$name]}"
  finish "$name" 10
}
run_stage() {
  local stage=$1 channel="demo_${token}_$1" route=SHM
  echo "[STAGE] BEGIN $stage channel=$channel"
  if [[ $stage == A ]]; then
    launch A intra A "$seconds" "$channel"
    event A '\[DISCOVERY\] MATCHED' 20
    event A 'backend=INTRA state=ENABLED' 5
    event A '\[CHECK\] CONTIGUOUS_10' 20
    finish A "$((seconds + 20))"
  else
    [[ $stage != E ]] || route=RTPS
    launch "${stage}_pub" pub "$stage" "$((seconds * 2 + 100))" "$channel"
    launch "${stage}_sub1" sub "$stage" "$seconds" "$channel"
    if [[ $stage != E ]]; then event "${stage}_pub" '\[DISCOVERY\] MATCHED' 20; fi
    event "${stage}_pub" "backend=$route state=ENABLED" 20
    event "${stage}_sub1" '\[CHECK\] FIRST_VALID' 20
    event "${stage}_sub1" '\[CHECK\] CONTIGUOUS_10' 10
    if [[ $stage == C ]]; then event C_pub 'LOAN_SEND shm_backed=1' 5; fi
    finish "${stage}_sub1" "$((seconds + 20))"
    if [[ $stage != E ]]; then
      event "${stage}_pub" '\[DISCOVERY\] OFFLINE subscribers=0' 15
      event "${stage}_pub" 'backend=SHM state=DISABLED' 5
    fi
    if [[ $stage == D ]]; then
      echo '[STAGE] D: subscriber exited normally; real OFFLINE and SHM DISABLED observed'
      local last first enabled_count
      last=$(sed -n 's/^\[SUB\].* last=\([0-9]*\).*/\1/p' "${files[D_sub1]}" | tail -1)
      echo '[STAGE] D: restarting subscriber; publisher PID and sequence continue; no offline replay claim'
      launch D_sub2 sub D "$seconds" "$channel"
      event D_sub2 '\[CHECK\] CONTIGUOUS_10' 20
      enabled_count=$(grep -c 'backend=SHM state=ENABLED' "${files[D_pub]}")
      ((enabled_count >= 2)) || fail 'D no second backend enable'
      first=$(sed -n 's/^\[CHECK\] FIRST_VALID seq=\([0-9]*\).*/\1/p' "${files[D_sub2]}")
      ((first > last)) || fail "D recovery sequence did not advance: $first <= $last"
      echo "[CHECK] RECOVERED same_publisher_pid=${pids[D_pub]} previous_last=$last new_first=$first contiguous=10"
      finish D_sub2 "$((seconds + 20))"
    fi
    stop_pub "${stage}_pub"
  fi
  echo "[RESULT] PASS scene=$stage (backend event + validated reception + process exit checks)"
}
if [[ $scenario == all ]]; then
  for stage in A B C D E; do run_stage "$stage"; done
else run_stage "$scenario"
fi
echo "[RESULT] PASS requested=$scenario logs=$RUN_DIR"
