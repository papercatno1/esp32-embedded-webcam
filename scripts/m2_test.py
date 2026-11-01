#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
M2 自测：自动熄屏 + 敲击唤醒（敲击这一步需要人手动敲，脚本负责其余部分）

  1. BLE 连上设备，把超时设成 --timeout 秒（默认 6）
  2. 等超时 + 2 秒，读 /status.json，期望 bl=0（已熄屏）
  3. 连续观察 --watch 秒，确认没有误唤醒（bl 一直 0）
  4. 发一条 V（任意指令 = 一次交互），再读，期望 bl=1
  5. 把超时恢复成 60 秒

用法:
  python scripts/m2_test.py
  python scripts/m2_test.py --timeout 5 --watch 20
"""
import argparse
import asyncio
import json
import socket
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

from bleak import BleakClient, BleakScanner  # noqa: E402

DEV_NAME = "ESP32-NOTE"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


def http_status(host="192.168.88.32", path="/status.json", timeout=4):
    s = socket.create_connection((host, 80), timeout=timeout)
    try:
        s.sendall(f"GET {path} HTTP/1.0\r\nHost: x\r\n\r\n".encode())
        buf = b""
        s.settimeout(3)
        while True:
            try:
                d = s.recv(4096)
            except socket.timeout:
                break
            if not d:
                break
            buf += d
    finally:
        s.close()
    body = buf.decode("utf-8", "replace").split("\r\n\r\n")[-1]
    return json.loads(body)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=6, help="临时熄屏超时（秒）")
    ap.add_argument("--watch", type=int, default=15, help="熄屏后观察多久确认不误唤醒（秒）")
    ap.add_argument("--host", default="192.168.88.32")
    ap.add_argument("--restore", type=int, default=60, help="测完恢复的超时（秒）")
    args = ap.parse_args()

    print("== 0. 设备状态 ==")
    st = http_status(args.host)
    print("   ", st)
    if not st.get("imu"):
        print("!! IMU 没起来，后面的敲击唤醒测不了（固件说不可用）")
        return 1

    print(f"\n== 1. 扫描 / 连接，把超时设为 {args.timeout}s ==")
    devs = await BleakScanner.discover(timeout=8, return_adv=True)
    addr = None
    for a, (dev, adv) in devs.items():
        name = dev.name or adv.local_name or ""
        if DEV_NAME.lower() in name.lower():
            addr = a
            print(f"    {name}  {a}  {adv.rssi} dBm")
            break
    if not addr:
        print("!! 没扫到设备")
        return 1

    async with BleakClient(addr, timeout=20) as client:
        await client.start_notify(TX, lambda _c, d: print(f"    [<-] {d.decode('utf-8', 'replace').strip()}"))
        await asyncio.sleep(0.3)
        await client.write_gatt_char(RX, f"O {args.timeout}\n".encode(), response=False)
        await asyncio.sleep(1.0)

        print(f"\n== 2. 等 {args.timeout + 2} 秒后看是否熄屏 ==")
        await asyncio.sleep(args.timeout + 2)
        st = http_status(args.host)
        print("   ", st)
        ok_blank = st.get("bl") == 0
        print("   熄屏:", "OK" if ok_blank else "失败（bl 仍为 1）")

        print(f"\n== 3. 观察 {args.watch} 秒，确认不误唤醒 ==")
        false_wake = 0
        t0 = time.time()
        while time.time() - t0 < args.watch:
            await asyncio.sleep(3)
            st = http_status(args.host)
            print(f"    t={int(time.time() - t0):2d}s  bl={st.get('bl')} idle={st.get('idle')}s")
            if st.get("bl") == 1:
                false_wake += 1
        print("   误唤醒次数:", false_wake)

        print("\n== 4. 发一条 V（等价于手机连上来操作），看屏幕是否恢复 ==")
        await client.write_gatt_char(RX, b"V\n", response=False)
        await asyncio.sleep(1.5)
        st = http_status(args.host)
        print("   ", st)
        ok_wake = st.get("bl") == 1
        print("   点亮:", "OK" if ok_wake else "失败")

        print(f"\n== 5. 超时恢复 {args.restore}s ==")
        await client.write_gatt_char(RX, f"O {args.restore}\n".encode(), response=False)
        await asyncio.sleep(0.8)

    print("\n== 结论 ==")
    print("  自动熄屏 :", "通过" if ok_blank else "未通过")
    print("  无操作不误亮:", "通过" if false_wake == 0 else f"异常 {false_wake} 次")
    print("  指令唤醒 :", "通过" if ok_wake else "未通过")
    print("  敲击唤醒 : 需要人手动敲一下板子或桌面，看屏幕是否亮起（串口会打 [PWR] 敲击 xx g）")
    return 0 if (ok_blank and ok_wake and false_wake == 0) else 1


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
