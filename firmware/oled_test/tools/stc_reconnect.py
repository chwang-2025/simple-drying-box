"""Reconnect-aware launcher for STC ISP on boards sharing USB power with CH340."""
import argparse
import subprocess
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def port_present(name: str) -> bool:
    return any(p.device.upper() == name.upper() for p in list_ports.comports())


def wait_for(name: str, present: bool) -> None:
    while port_present(name) != present:
        time.sleep(0.05)


def wait_until_openable(name: str) -> None:
    """A newly enumerated CH340 can be listed before Windows accepts I/O."""
    while True:
        try:
            with serial.Serial(name, 2400, timeout=0.05, write_timeout=0.05):
                pass
            return
        except (serial.SerialException, PermissionError, OSError):
            time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--flash", type=Path)
    args = parser.parse_args()

    project = Path(__file__).resolve().parents[1]
    stcgal = project / ".tools" / "stcgal" / "Scripts" / "stcgal.exe"
    if not stcgal.exists():
        print(f"找不到烧录工具：{stcgal}", flush=True)
        return 2

    print(f"目标串口：{args.port}", flush=True)
    if port_present(args.port):
        print("第1步：现在拔掉开发板USB线。", flush=True)
        wait_for(args.port, False)
    print("已检测到串口断开。", flush=True)
    print("第2步：现在重新插入开发板USB线；程序会自动打开串口。", flush=True)
    wait_for(args.port, True)
    print("已检测到串口，正在等待Windows驱动允许读写……", flush=True)
    wait_until_openable(args.port)
    time.sleep(0.05)
    print("串口已可读写，正在连接STC启动程序……", flush=True)

    # STC8H1Kxx uses the newer STC8G-family ISP framing in stcgal.
    command = [str(stcgal), "-P", "stc8g", "-p", args.port]
    if args.flash:
        image = args.flash.resolve()
        if not image.exists():
            print(f"找不到固件：{image}", flush=True)
            return 2
        command += ["-t", "11059", str(image)]
    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main())
