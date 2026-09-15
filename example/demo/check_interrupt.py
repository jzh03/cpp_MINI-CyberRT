#!/usr/bin/env python3
"""Local Ctrl+C acceptance check; Python standard library only, no dependencies."""
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

demo = Path(__file__).resolve().parent
out = Path(tempfile.mkdtemp(prefix="interrupt-", dir=demo / "runs"))
command = [str(demo / "run_demo.sh"), "B", "--no-build"]
(out / "command.txt").write_text(repr(command) + "\nSIGINT to its own process group after CONTIGUOUS_10\n")
with (out / "console.log").open("w") as console:
    process = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        deadline = time.monotonic() + 30
        while True:
            text = (out / "console.log").read_text()
            if "[CHECK] CONTIGUOUS_10" in text:
                break
            if process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("demo did not establish communication before interrupt")
            time.sleep(0.1)
        os.killpg(process.pid, signal.SIGINT)  # Terminal Ctrl+C semantics.
        result = process.wait(timeout=15)
        text = (out / "console.log").read_text()
        assert result == 130, (result, text)
        assert "cleanup complete; child processes waited" in text, text
        assert "cleanup timeout" not in text, text
        run = Path(re.search(r"run_dir=(\S+)", text).group(1))
        children = re.findall(r"^name=\S+ pid=(\d+)$", (run / "commands.txt").read_text(), re.M)
        assert len(children) == 2, children
        for child in children:
            assert not Path("/proc", child).exists(), f"child still exists: {child}"
        summary = f"PASS Ctrl+C exit=130 children={children} gone; evidence={run}\n"
        (out / "result.txt").write_text(summary)
        print(summary + f"console={out / 'console.log'}")
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                raise RuntimeError("interrupt acceptance watchdog forced its own group to stop")
