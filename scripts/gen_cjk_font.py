#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
生成 16x16 中文点阵字库（二进制 blob），供 ESP32 端按 Unicode 码点二分查找。

格式（小端）:
  magic   : 4B  "CJKF"
  glyph_w : u16 = 16
  glyph_h : u16 = 16
  count   : u32
  records : count x 40B  { u32 codepoint, u8 advance, u8 pad[3], u8 bitmap[32] }
            bitmap 按行存放，每行 16 bit（2 字节，高位在左）

字表 = ASCII 0x20-0x7E + GB2312 全字符集（含符号区 0xA1A1-0xA9FE 与汉字区 0xB0A1-0xF7FE）
用法:
  python scripts/gen_cjk_font.py                # 默认宋体 16px
  python scripts/gen_cjk_font.py --font simhei  # 换字体
"""
import argparse
import os
import struct
import sys
from PIL import Image, ImageDraw, ImageFont

FONTS = {
    "simsun": r"C:\Windows\Fonts\simsun.ttc",
    "simhei": r"C:\Windows\Fonts\simhei.ttf",
    "msyh": r"C:\Windows\Fonts\msyh.ttc",
    "deng": r"C:\Windows\Fonts\Deng.ttf",
}

SIZE = 16          # 字形像素高度
THRESHOLD = 110    # 二值化阈值


def gb2312_charset():
    """GB2312 全部可解码字符（符号区 + 汉字区）。"""
    out = []
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            out.append(ch)
    return out


def build_charset():
    chars = [chr(c) for c in range(0x20, 0x7F)]        # ASCII
    chars += gb2312_charset()                          # 中文与常用符号
    seen, uniq = set(), []
    for ch in chars:
        cp = ord(ch)
        if cp in seen:
            continue
        seen.add(cp)
        uniq.append(cp)
    return sorted(uniq)


def render(ch, font):
    """渲染单字 -> (32 字节点阵, advance)"""
    img = Image.new("L", (SIZE * 2, SIZE * 2), 0)
    d = ImageDraw.Draw(img)
    d.text((SIZE // 2, SIZE // 2), ch, fill=255, font=font)
    adv = int(round(font.getlength(ch)))
    adv = max(1, min(SIZE, adv))
    bmp = bytearray()
    for row in range(SIZE):
        bits = 0
        for col in range(SIZE):
            v = img.getpixel((SIZE // 2 + col, SIZE // 2 + row))
            if v >= THRESHOLD:
                bits |= (1 << (SIZE - 1 - col))
        bmp += struct.pack("<H", bits)
    return bytes(bmp), adv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="simsun", choices=list(FONTS))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    path = FONTS[args.font]
    if not os.path.exists(path):
        sys.exit(f"字体不存在: {path}")
    out = args.out or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "firmware", "notepad", "data", "cjk16.bin")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    font = ImageFont.truetype(path, SIZE)
    cps = build_charset()
    recs = []
    for cp in cps:
        bmp, adv = render(chr(cp), font)
        recs.append(struct.pack("<IB3x", cp, adv) + bmp)

    with open(out, "wb") as f:
        f.write(b"CJKF")
        f.write(struct.pack("<HHI", SIZE, SIZE, len(recs)))
        f.write(b"".join(recs))

    total = os.path.getsize(out)
    print(f"字体: {os.path.basename(path)}  字形: {SIZE}x{SIZE}")
    print(f"字符数: {len(recs)}  (ASCII 95 + GB2312 {len(recs) - 95})")
    print(f"输出: {out}  {total:,} B ({total / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
