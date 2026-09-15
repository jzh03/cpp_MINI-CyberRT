#!/usr/bin/env python3
"""Run separate SHM endpoints sequentially; retain evidence and aggregate 3 repeats."""
import argparse
import csv
import hashlib
import re
import sys
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def validate(sender, receiver):
    for key in ("mode", "size_bytes", "scope", "slots", "slot_capacity", "segment_bytes",
                "channel_id", "serialized_size", "warmup_ms", "duration_ms", "drain_ms",
                "window_start_ns", "window_end_ns"):
        if sender[key] != receiver[key]:
            raise ValueError(f"endpoint mismatch: {key}")
    if not (sender["pid"] == receiver["peer_pid"] and receiver["pid"] == sender["peer_pid"]
            and sender["pid"] != receiver["pid"]):
        raise ValueError("not separate paired processes")
    for endpoint in (sender, receiver):
        if not endpoint["path_verified"] or endpoint["slots"] != 32:
            raise ValueError("path/layout evidence missing")
        for boundary in ("start", "end"):
            lag = endpoint[f"cpu_{boundary}_ns"] - endpoint[f"window_{boundary}_ns"]
            if not 0 <= lag <= 20_000_000:
                raise ValueError(f"CPU sampling delayed more than 20 ms: {lag}")
    if sender["attempts"] >= 16 * 1024 * 1024 or sender["attempts"] == 0:
        raise ValueError("empty run or sequence accounting limit reached")
    if sender["attempts"] != sender["send_success"] + sender["acquire_fail"] + sender["transmit_fail"]:
        raise ValueError("send accounting mismatch")
    if sender["mode"] == "copy":
        if sender["measured_serializations"] != sender["attempts"] or not receiver["deserializations_all_phases"]:
            raise ValueError("ordinary SHM serialization evidence missing")
    elif (sender["measured_serializations"] != 0 or
          sender["measured_shm_loans"] != sender["attempts"] - sender["acquire_fail"] or
          receiver["shm_callbacks"] != receiver["formal_callbacks"]):
        raise ValueError("SHM-backed Loan evidence missing")
    if receiver["invalid"] or receiver["early"] or receiver["warmup_after_start"]:
        raise ValueError("invalid payload, early formal data, or warmup contaminated formal window")
    if not receiver["window_unique"] or not receiver["warmup_received_excluded"]:
        raise ValueError("no valid window/warmup deliveries")
    total = receiver["window_unique"] + receiver["drain_unique"]
    if (receiver["missing_attempts_after_drain"] != sender["attempts"] - total or
        receiver["missing_success_after_drain"] != sender["send_success"] - total + receiver["received_failed_send"] or
        receiver["window_valid_bytes"] != receiver["window_unique"] * sender["size_bytes"]):
        raise ValueError("receive accounting mismatch")
    if receiver["formal_callbacks"] != (total + receiver["duplicates_window"] +
            receiver["duplicates_drain"] + receiver["early"] + receiver["after_cutoff"]):
        raise ValueError("callback accounting mismatch")


def metrics(sender, receiver):
    return {
        "throughput_mib_s": receiver["window_valid_bytes"] / (1024 ** 2) / (sender["duration_ms"] / 1000),
        "sender_cpu_pct": 100 * sender["cpu_ns"] / (sender["cpu_end_ns"] - sender["cpu_start_ns"]),
        "receiver_cpu_pct": 100 * receiver["cpu_ns"] / (receiver["cpu_end_ns"] - receiver["cpu_start_ns"]),
        "missing_success_pct": 100 * receiver["missing_success_after_drain"] / sender["send_success"],
    }


def summarize(rows):
    summary = []
    for size in sorted({r["size_bytes"] for r in rows}):
        for mode in ("copy", "loan"):
            group = [r for r in rows if r["size_bytes"] == size and r["mode"] == mode]
            entry = {"size_bytes": size, "mode": mode, "repeats": len(group)}
            for metric in ("throughput_mib_s", "sender_cpu_pct", "receiver_cpu_pct", "missing_success_pct"):
                values = [r[metric] for r in group]
                entry[metric] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
            summary.append(entry)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, default=ROOT / "example/build/benchmark/bin")
    parser.add_argument("--output-dir", type=Path, required=True, help="fresh directory under project log/")
    parser.add_argument("--sizes", type=int, nargs="+", default=[4096, 65536, 1048576, 4194304])
    parser.add_argument("--warmup-ms", type=int, default=1000)
    parser.add_argument("--duration-ms", type=int, default=5000)
    parser.add_argument("--drain-ms", type=int, default=1000)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--sender-cpu", type=int)
    parser.add_argument("--receiver-cpu", type=int)
    args = parser.parse_args()
    if args.repeats != 3:
        parser.error("exactly 3 repeats required (use shorter durations for smoke runs)")
    if len(set(args.sizes)) != len(args.sizes) or not set(args.sizes) <= {4096, 65536, 1048576, 4194304}:
        parser.error("sizes must be distinct supported payload sizes")
    output = args.output_dir.resolve()
    if ROOT / "log" not in output.parents:
        parser.error("output-dir must be under project log/")
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy(); env["CMW_PATH"] = str(ROOT)
    meta = {"started_local": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "cwd": str(ROOT),
            "argv": sys.argv, "CMW_PATH": env["CMW_PATH"],
            "platform": platform.platform(), "affinity_available": sorted(os.sched_getaffinity(0)),
            "initial_loadavg": os.getloadavg(), "runs": []}
    for command, key in ((["git", "rev-parse", "HEAD"], "head"), (["git", "branch", "--show-current"], "branch"),
                         (["g++", "--version"], "compiler"), (["lscpu"], "lscpu"), (["free", "-h"], "memory"),
                         (["df", "-h", "/dev/shm"], "shm_space"), (["ipcs", "-m"], "ipc_before")):
        meta[key] = subprocess.check_output(command, cwd=ROOT, text=True)
    meta["runtime_env"] = {key: env.get(key) for key in
                           ("CMW_PATH", "LD_LIBRARY_PATH", "ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS")}
    paths = [args.bin_dir.resolve() / ("shm_benchmark_" + role) for role in ("sender", "receiver")]
    paths += [ROOT / "example" / name for name in ("Makefile", "shm_benchmark_common.h",
              "shm_benchmark_sender.cpp", "shm_benchmark_receiver.cpp", "run_shm_benchmark.py")]
    meta["sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    rows = []
    try:
        for size in args.sizes:
            for repeat in range(1, args.repeats + 1):
                # Alternate ordering to reduce systematic order/temperature bias.
                for mode in (("copy", "loan") if repeat % 2 else ("loan", "copy")):
                    tag = f"{size}-{mode}-{repeat}"
                    channel = "shmbench_" + uuid.uuid4().hex
                    # UNIX socket name is bounded independently of output path length.
                    control = ROOT / "log" / (channel + ".sock")
                    common = ["--mode", mode, "--size", str(size), "--channel", channel,
                              "--control", str(control), "--warmup-ms", str(args.warmup_ms),
                              "--duration-ms", str(args.duration_ms), "--drain-ms", str(args.drain_ms)]
                    run = {"tag": tag, "repeat": repeat, "channel": channel, "commands": {}, "exit_codes": {}}
                    meta["runs"].append(run)
                    processes = {}; handles = []
                    try:
                        for role in ("receiver", "sender"):
                            cpu = getattr(args, role + "_cpu")
                            prefix = [] if cpu is None else ["taskset", "-c", str(cpu)]
                            cmd = prefix + [str(args.bin_dir.resolve() / ("shm_benchmark_" + role))] + common + [
                                "--output", str(output / f"{tag}-{role}.json")]
                            run["commands"][role] = cmd
                            handle = (output / f"{tag}-{role}.log").open("w"); handles.append(handle)
                            processes[role] = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=handle, stderr=subprocess.STDOUT)
                        deadline = time.monotonic() + (args.warmup_ms + args.duration_ms + args.drain_ms) / 1000 + 35
                        for role in ("sender", "receiver"):
                            run["exit_codes"][role] = processes[role].wait(timeout=max(.1, deadline - time.monotonic()))
                        if any(run["exit_codes"].values()):
                            raise RuntimeError(f"{tag}: endpoint failure {run['exit_codes']}")
                        sender = json.loads((output / f"{tag}-sender.json").read_text())
                        receiver = json.loads((output / f"{tag}-receiver.json").read_text())
                        run["sender"] = sender; run["receiver"] = receiver
                        validate(sender, receiver)
                        row = {"size_bytes": size, "mode": mode, "repeat": repeat, **metrics(sender, receiver)}
                        row.update({"send_" + k: v for k, v in sender.items() if isinstance(v, (int, float))})
                        row.update({"recv_" + k: v for k, v in receiver.items() if isinstance(v, (int, float))})
                        rows.append(row)
                        run["validation"] = "passed"
                        print(f"{tag}: {row['throughput_mib_s']:.2f} MiB/s, sender/receiver CPU "
                              f"{row['sender_cpu_pct']:.1f}/{row['receiver_cpu_pct']:.1f}%, "
                              f"missing {receiver['missing_success_after_drain']}, drain {receiver['drain_unique']}", flush=True)
                    finally:
                        for role, proc in processes.items():
                            if proc.poll() is None:
                                proc.terminate()
                                try: proc.wait(timeout=3)
                                except subprocess.TimeoutExpired: proc.kill(); proc.wait()
                            run["exit_codes"][role] = proc.returncode
                        for handle in handles: handle.close()
                        control.unlink(missing_ok=True)
                        # Only remove this run's named segment, after both processes exit.
                        segment_match = re.search(r"segment_path=(/dev/shm/cmw_[0-9]+)",
                            (output / f"{tag}-sender.log").read_text())
                        if segment_match:
                            path = Path(segment_match.group(1))
                            run["segment_path"] = str(path)
                            run["segment_left_after_exit"] = path.exists()
                            if path.exists(): path.unlink()
                        for role in ("sender", "receiver"):
                            for log in (ROOT / "log").glob(channel + "_" + role + ".log*"):
                                shutil.move(str(log), str(output / log.name))
    except Exception as exc:
        meta["error"] = str(exc)
        raise
    finally:
        meta["ipc_after"] = subprocess.check_output(["ipcs", "-m"], text=True)
        meta["finished_local"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        (output / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n")
        if rows:
            with (output / "results.csv").open("w") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    summary = summarize(rows)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = ["| Payload | Path | MiB/s median [min, max] | Sender CPU % | Receiver CPU % | Missing success % |",
             "| --- | --- | --- | --- | --- | --- |"]
    for group in summary:
        values = [f"{group[k]['median']:.2f} [{group[k]['min']:.2f}, {group[k]['max']:.2f}]" for k in
                  ("throughput_mib_s", "sender_cpu_pct", "receiver_cpu_pct", "missing_success_pct")]
        lines.append(f"| {group['size_bytes']} | {group['mode']} | " + " | ".join(values) + " |")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
