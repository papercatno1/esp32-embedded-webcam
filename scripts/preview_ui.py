#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
离线预览：直接读 data/cjk16.bin + cjk24.bin，按 ui.cpp 的版式把一张票据画到
320x240 的 PNG 上，用来确认字号/行距/折行有没有溢出（不用连板子）。

用法:
  python scripts/preview_ui.py
  python scripts/preview_ui.py --out docs/img/票据屏-预览.png
"""
import argparse
import os
import struct
import sys

from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "firmware", "notepad", "data")

# 与 ui.cpp 保持一致
W, H = 320, 240
TITLE_H, BODY_Y, BODY_LINES, BODY_LH = 34, 44, 6, 28
FOOT_LINE, FOOT_Y = 212, 216

C_BG = (0, 0, 0)


def c565(v):
    """RGB565 -> RGB888（和屏上看到的颜色一致）"""
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


C_BAR = c565(0x18C3)
C_TITLE = c565(0xFFFF)
C_TEXT = c565(0xEF7D)
C_DIM = c565(0x8410)
C_ACCENT = c565(0x07E0)


class Font:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.raw = f.read()
        assert self.raw[:4] == b"CJKF", f"{path} 不是字库"
        self.gw, self.gh, self.count = struct.unpack_from("<HHI", self.raw, 4)
        self.row_bytes = (self.gw + 7) // 8
        self.rec = 8 + self.row_bytes * self.gh
        self.base = 12
        # 建索引：codepoint -> 记录偏移
        self.index = {}
        for i in range(self.count):
            off = self.base + i * self.rec
            cp = struct.unpack_from("<I", self.raw, off)[0]
            self.index[cp] = off

    def glyph(self, ch):
        off = self.index.get(ord(ch))
        if off is None:
            return None, self.gw
        adv = self.raw[off + 4]
        bmp = self.raw[off + 8: off + 8 + self.row_bytes * self.gh]
        return bmp, (adv or self.gw)

    def draw(self, img, x, y, text, color, maxx=-1):
        cx = x
        for ch in text:
            bmp, adv = self.glyph(ch)
            if maxx > 0 and cx + adv > maxx + 1:
                break
            if bmp is None:
                cx += adv
                continue
            for row in range(self.gh):
                line = bmp[row * self.row_bytes:(row + 1) * self.row_bytes]
                for col in range(self.gw):
                    if line[col >> 3] & (0x80 >> (col & 7)):
                        px = cx + col
                        py = y + row
                        if 0 <= px < W and 0 <= py < H:
                            img.putpixel((px, py), color)
            cx += adv
        return cx - x

    def width(self, text):
        return sum(self.glyph(c)[1] for c in text)


def wrap(lines, font, max_px, max_lines):
    out = []
    for line in lines:
        p = line
        while p:
            w, cut = 0, 0
            for i, ch in enumerate(p):
                a = font.glyph(ch)[1]
                if w + a > max_px:
                    break
                w += a
                cut = i + 1
            if cut == 0:
                cut = 1
            out.append(p[:cut])
            p = p[cut:]
            if len(out) >= max_lines:
                return out
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", default="高铁票 G106")
    ap.add_argument("--lines", nargs="*", default=[
        "2026-11-02 08:25 开",
        "济南西 → 北京南",
        "二等座 12车 12A号",
        "票价 314.5 元",
        "请提前 30 分钟检票",
    ])
    ap.add_argument("--page", default="1/3")
    ap.add_argument("--link", default="已连", help="已连 / 广播")
    ap.add_argument("--ot", default="60s 熄屏", help="常亮 / 60s 熄屏")
    ap.add_argument("--pgtext", default="2/3", help="页脚右侧的页码文字")
    ap.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "."), "ui_preview.png"))
    args = ap.parse_args()

    big = Font(os.path.join(DATA, "cjk24.bin"))
    small = Font(os.path.join(DATA, "cjk16.bin"))

    img = Image.new("RGB", (W, H), C_BG)
    img.paste(Image.new("RGB", (W, TITLE_H), C_BAR), (0, 0))
    big.draw(img, 6, 5, args.title, C_TITLE, 246)
    small.draw(img, W - 6 - small.width(args.page), 9, args.page, C_ACCENT)

    body = wrap(args.lines, big, 306, BODY_LINES)
    y = BODY_Y
    for ln in body:
        big.draw(img, 8, y, ln, C_TEXT, 312)
        y += BODY_LH

    for x in range(W):
        img.putpixel((x, FOOT_LINE), C_BAR)
    # 页脚三段：左连接态、中熄屏设置、右页码（与 ui.cpp 的 ui_footer_refresh() 一致）
    left = "蓝牙 " + args.link
    left_color = C_ACCENT if args.link.startswith("已连") else C_DIM
    small.draw(img, 6, FOOT_Y, left, left_color, -1)
    small.draw(img, (W - small.width(args.ot)) // 2, FOOT_Y, args.ot, C_DIM, -1)
    small.draw(img, 314 - small.width(args.pgtext), FOOT_Y, args.pgtext, C_DIM, -1)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    img.save(args.out)
    scale = 2
    img.resize((W * scale, H * scale), Image.NEAREST).save(
        os.path.splitext(args.out)[0] + "-2x.png")
    print(f"输出: {args.out}（以及 -2x.png 放大版）")
    print(f"正文行数: {len(body)}/{BODY_LINES}，末行底部 y={BODY_Y + (len(body) - 1) * BODY_LH + big.gh}")


if __name__ == "__main__":
    main()
