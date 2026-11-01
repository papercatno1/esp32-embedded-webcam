#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
BLE(NUS) 端到端自测：扫描 -> 连接 -> 订阅 TX -> 发指令 -> 收回执

用法:
  python scripts/ble_test.py                # 全流程自测
  python scripts/ble_test.py --scan 8       # 只扫 8 秒
  python scripts/ble_test.py --cmd "V"      # 只发一条指令
"""
import argparse
import asyncio
import sys

sys.stdout.reconfigure(encoding="utf-8")

from bleak import BleakClient, BleakScanner  # noqa: E402

DEV_NAME = "ESP32-NOTE"
SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # 手机 -> 设备
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # 设备 -> 手机


def on_notify(_char, data: bytearray):
    print(f"[<-] {data.decode('utf-8', 'replace')}")


async def find_device(timeout: float):
    print(f"扫描 {timeout:.0f} 秒 ...")
    # 注意：用 detect_callback 逐包过滤会漏掉"首个广播包还没带名字"的设备，
    # 统一先全量发现再按名字筛。
    devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
    hits = []
    for addr, (dev, adv) in devs.items():
        name = dev.name or adv.local_name or ""
        if DEV_NAME.lower() in name.lower():
            print(f"  {name}  {addr}  {adv.rssi} dBm")
            hits.append(addr)
    return hits


async def connect_and_send(address, cmds, hold: float):
    print(f"连接 {address} ...")
    async with BleakClient(address, timeout=20) as client:
        print(f"已连接, MTU={client.mtu_size}")
        await client.start_notify(TX, on_notify)
        await asyncio.sleep(0.3)
        for c in cmds:
            payload = (c + "\n").encode("utf-8")
            print(f"[->] {c}   ({len(payload)} 字节)")
            await client.write_gatt_char(RX, payload, response=False)
            await asyncio.sleep(0.6)
        await asyncio.sleep(hold)
        await client.stop_notify(TX)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", type=float, default=6.0)
    ap.add_argument("--cmd", default=None)
    ap.add_argument("--hold", type=float, default=2.0)
    ap.add_argument("--address", default=None)
    args = ap.parse_args()

    addrs = [args.address] if args.address else await find_device(args.scan)
    if not addrs:
        print("没扫到设备：确认板子已上电、没被手机占用连接")
        return 1

    if args.cmd:
        cmds = [args.cmd]
    else:
        cmds = [
            "V",
            "C",
            "T 高铁票 G106|2026-11-02 08:25 开|济南西 → 北京南|二等座 12车 12A号|票价 314.5 元|请提前 30 分钟检票",
            "L",
            "N",
        ]
    await connect_and_send(addrs[0], cmds, args.hold)
    print("完成")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
