#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
生成中文点阵字库（二进制 blob），供 ESP32 端按 Unicode 码点二分查找。

格式（小端）:
  magic   : 4B  "CJKF"
  glyph_w : u16 = SIZE
  glyph_h : u16 = SIZE
  count   : u32
  records : count x REC  { u32 codepoint, u8 advance, u8 pad[3], u8 bitmap[ROW*H] }
            ROW  = (SIZE + 7) // 8   每行字节数
            bitmap 按行存放，每行 ROW 字节，高位在左（超出宽度的低位补 0）
            REC  = 8 + ROW * SIZE

  SIZE=16 -> ROW=2, REC=40,  单字 40 B
  SIZE=24 -> ROW=3, REC=80,  单字 80 B

字表 = ASCII 0x20-0x7E + GB2312 全字符集（符号区 0xA1A1-0xA9FE 与汉字区 0xB0A1-0xF7FE）

用法:
  python scripts/gen_cjk_font.py                 # 16x16 宋体 -> cjk16.bin
  python scripts/gen_cjk_font.py --size 24       # 24x24 宋体 -> cjk24.bin
  python scripts/gen_cjk_font.py --size 24 --font simhei
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


def render(ch, font, size, row_bytes):
    """渲染单字 -> (bitmap, advance)"""
    # 用 2 倍画布居中，留出字体下沉/上升超出 em 框的部分
    img = Image.new("L", (size * 2, size * 2), 0)
    d = ImageDraw.Draw(img)
    d.text((size // 2, size // 2), ch, fill=255, font=font)
    adv = int(round(font.getlength(ch)))
    adv = max(1, min(size, adv))

    bmp = bytearray()
    for row in range(size):
        rowbits = bytearray(row_bytes)
        for col in range(size):
            v = img.getpixel((size // 2 + col, size // 2 + row))
            if v >= THRESHOLD:
                rowbits[col >> 3] |= (1 << (7 - (col & 7)))
        bmp += rowbits
    return bytes(bmp), adv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="simsun", choices=list(FONTS))
    ap.add_argument("--size", type=int, default=16, help="字形像素高度，常用 16 / 24 / 32")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    size = args.size
    if size < 8 or size > 48:
        sys.exit("--size 建议 8..48")
    row_bytes = (size + 7) // 8

    path = FONTS[args.font]
    if not os.path.exists(path):
        sys.exit(f"字体不存在: {path}")
    out = args.out or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "firmware", "notepad", "data", f"cjk{size}.bin")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    font = ImageFont.truetype(path, size)
    cps = build_charset()
    recs = []
    for cp in cps:
        bmp, adv = render(chr(cp), font, size, row_bytes)
        recs.append(struct.pack("<IB3x", cp, adv) + bmp)

    with open(out, "wb") as f:
        f.write(b"CJKF")
        f.write(struct.pack("<HHI", size, size, len(recs)))
        f.write(b"".join(recs))

    total = os.path.getsize(out)
    print(f"字体: {os.path.basename(path)}  字形: {size}x{size}  每行 {row_bytes} B  单字 {8 + row_bytes * size} B")
    print(f"字符数: {len(recs)}  (ASCII 95 + GB2312 {len(recs) - 95})")
    print(f"输出: {out}  {total:,} B ({total / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
