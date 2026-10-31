#!/usr/bin/env python
"""串口监视器：可选先复位板子，再把串口输出同时打到屏幕并存到 logs/。

用法:
    python scripts/serial_monitor.py -p COM16 -b 115200 --reset
    python scripts/serial_monitor.py -p COM16 --duration 10 --log logs/boot.txt
Ctrl+C 退出。
"""
from __future__ import annotations

import argparse
import datetime as _dt
import os
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("缺少 pyserial：.venv\\Scripts\\python.exe -m pip install pyserial")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="COM16")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--reset", action="store_true", help="开始时拉低 EN 复位板子（USB-JTAG 亦可用）")
    ap.add_argument("--duration", type=float, default=0, help="采集秒数，0 表示一直采集")
    ap.add_argument("--log", default="", help="日志文件路径，默认 logs/serial-<时间>.txt")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    log_path = args.log or os.path.join(
        root, "logs", f"serial-{_dt.datetime.now():%Y%m%d-%H%M%S}.txt"
    )
    os.makedirs(os.path.dirname(os.path.abspath(log_path)), exist_ok=True)

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        ser.reset_input_buffer()   # 先清掉上电以来残留的字节，再复位
        if args.reset:
            ser.setDTR(False)
            ser.setRTS(False)
            time.sleep(0.15)
            ser.setRTS(True)      # EN 拉低
            time.sleep(0.15)
            ser.setRTS(False)     # 释放 → 启动
        print(f"[{args.port} @ {args.baud}] 开始采集，日志 -> {log_path}  (Ctrl+C 退出)")

        t0 = time.time()
        total = 0
        reconnects = 0
        with open(log_path, "ab") as fh:
            try:
                while True:
                    try:
                        chunk = ser.read(4096)
                    except (serial.SerialException, OSError):
                        # USB-CDC 设备被复位后会重新枚举，句柄失效：重连继续
                        reconnects += 1
                        if reconnects > 8:
                            print("\n[放弃] 串口反复断开")
                            break
                        print(f"\n[重连 #{reconnects}] 端口重新枚举中…")
                        try:
                            ser.close()
                        except Exception:
                            pass
                        time.sleep(1.0)
                        ser = serial.Serial(args.port, args.baud, timeout=0.2)
                        continue

                    if chunk:
                        total += len(chunk)
                        fh.write(chunk)
                        fh.flush()
                        sys.stdout.write(chunk.decode("utf-8", "replace"))
                        sys.stdout.flush()
                    if args.duration and time.time() - t0 >= args.duration:
                        break
            except KeyboardInterrupt:
                print("\n[中断]")
        try:
            ser.close()
        except Exception:
            pass
    print(f"\n共收到 {total} 字节")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
