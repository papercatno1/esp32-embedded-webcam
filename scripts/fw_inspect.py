#!/usr/bin/env python
"""固件离线分析：分区表解析 + app 描述符 + 关键字串提取。

用法:
    python scripts/fw_inspect.py --partitions dump/partitions.bin
    python scripts/fw_inspect.py --app dump/app0.bin --grep "OV5640,LVGL,SDMMC"
    python scripts/fw_inspect.py --app dump/app0.bin            # 打印 app 描述符 + 常用关键词
"""
from __future__ import annotations

import argparse
import re
import struct

DEFAULT_KEYWORDS = (
    "ESP32-S3-LCD,waveshare,LVGL,ST7789,GC9,ILI9,OV5640,OV2640,"
    "SDMMC,camera,psram,app_wifi,bsp_"
)


def dump_partitions(path: str) -> None:
    data = open(path, "rb").read()
    print(f"=== 分区表: {path} ===")
    for i in range(0, len(data), 32):
        e = data[i : i + 32]
        if e[0:2] != b"\xaa\x50":
            break
        t, st = e[2], e[3]
        off, sz = struct.unpack("<II", e[4:12])
        label = e[12:28].split(b"\x00")[0].decode("ascii", "replace")
        print(
            f"  {label:10s} type=0x{t:02x} sub=0x{st:02x} "
            f"offset=0x{off:06x} size=0x{sz:06x} ({sz / 1024:.0f}KB)"
        )


def app_desc(app: bytes) -> None:
    print("=== app 描述符 ===")
    if struct.unpack("<I", app[0x20:0x24])[0] != 0xABCD5432:
        print("  未找到 esp_app_desc_t 魔数 (0xABCD5432)，偏移 0x10000 可能不是 app 分区")
        return
    def s(a: int, b: int) -> str:
        return app[a:b].split(b"\x00")[0].decode("ascii", "replace")
    print(f"  项目名    : {s(0x50, 0x70)}")
    print(f"  版本      : {s(0x30, 0x50)}")
    print(f"  编译时间  : {s(0x80, 0x90)} {s(0x70, 0x80)}")
    print(f"  IDF 版本  : {s(0x90, 0xB0)}")


def grep(app: bytes, keywords: list[str], limit: int = 40) -> None:
    strs = [b.decode("ascii", "replace") for b in re.findall(rb"[\x20-\x7e]{4,}", app)]
    seen: set[str] = set()
    hits = 0
    for text in strs:
        for kw in keywords:
            if kw and kw.lower() in text.lower() and text not in seen:
                seen.add(text)
                print(" ", text[:140])
                hits += 1
                break
        if hits >= limit:
            break
    print(f"  --- 命中 {hits} 条 / 全部 {len(strs)} 条可打印串 ---")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--partitions", default="")
    ap.add_argument("--app", default="")
    ap.add_argument("--grep", default=DEFAULT_KEYWORDS)
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()

    if args.partitions:
        dump_partitions(args.partitions)
    if args.app:
        app = open(args.app, "rb").read()
        app_desc(app)
        if args.grep.strip():
            print(f"=== 关键字符串 (grep: {args.grep}) ===")
            grep(app, [k for k in args.grep.split(",") if k], args.limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
