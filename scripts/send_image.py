"""把一张图压成 1-bit RLE，通过 BLE 发到票据屏（固件 v0.3.0+ 的 I/A/E 指令）。

用法：
  .\\.venv\\Scripts\\python.exe scripts\\send_image.py 图片.png
  .\\.venv\\Scripts\\python.exe scripts\\send_image.py 图片.png --threshold 140 --dither

要点：
- 设备屏幕 320x212（底部 28px 留给页脚），图片会按比例缩放到这个框内
- 位图：每行按字节对齐，MSB 是左边像素，1 = 白、0 = 黑；RLE = [count][value] 成对（value 只能 0x00/0xFF）
- 传 RLE 而不是原始位图：票面截图通常能从 8 KB 压到 2~4 KB
"""
import argparse
import asyncio
import base64
import sys

sys.stdout.reconfigure(encoding="utf-8")

from PIL import Image, ImageOps

IMG_MAX_W = 320
IMG_MAX_H = 212

SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
CCC = "00002902-0000-1000-8000-00805f9b34fb"


def crop_aspect(im, where="center"):
    """按 320:212 的比例裁一块（手机长截图不裁就只剩一条 98px 宽的细条，字全糊）"""
    if where == "none":
        return im
    W, H = im.size
    target = IMG_MAX_W / float(IMG_MAX_H)      # 1.509
    if W / float(H) > target:                  # 太宽：裁左右
        nw = int(round(H * target))
        x0 = (W - nw) // 2
        return im.crop((x0, 0, x0 + nw, H))
    nh = int(round(W / target))                # 太高：裁上下
    if where == "top":
        y0 = 0
    elif where == "bottom":
        y0 = H - nh
    else:
        y0 = (H - nh) // 2
    return im.crop((0, y0, W, y0 + nh))


def to_bitmap(path, threshold=128, dither=False, invert=False, crop="center", preview="", box=""):
    im = Image.open(path).convert("L")
    if box:
        x0, y0, x1, y1 = [int(v) for v in box.split(",")]
        im = im.crop((x0, y0, x1, y1))
    else:
        im = crop_aspect(im, crop)
    im.thumbnail((IMG_MAX_W, IMG_MAX_H), Image.LANCZOS)
    if dither:
        im = im.convert("1")                      # 有序抖动，照片更好看
        bits = im.convert("L")
    else:
        bits = im.point(lambda v: 255 if v >= threshold else 0)
    if invert:
        bits = ImageOps.invert(bits)
    if preview:
        bits.save(preview)
    w, h = bits.size
    stride = (w + 7) // 8
    buf = bytearray(stride * h)
    px = bits.load()
    for y in range(h):
        row = y * stride
        for x in range(w):
            if px[x, y] >= 128:                   # 白 → 位 1
                buf[row + (x >> 3)] |= 0x80 >> (x & 7)
    return w, h, bytes(buf)


def fnv1a(bits):
    h = 2166136261
    for b in bits:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def rle_encode(bits):
    out = bytearray()
    i, n = 0, len(bits)
    while i < n:
        v = bits[i]
        run = 1
        while i + run < n and bits[i + run] == v and run < 255:
            run += 1
        out += bytes((run, v))
        i += run
    return bytes(out)


async def run(args):
    from bleak import BleakClient, BleakScanner

    w, h, bits = to_bitmap(args.image, args.threshold, args.dither, args.invert, args.crop, args.preview, args.box)
    rle = rle_encode(bits)
    b64 = base64.b64encode(rle).decode()
    print(f"图片 {w}x{h}  位图 {len(bits)} B  RLE {len(rle)} B  base64 {len(b64)} 字符")

    if args.dry_run:
        print("（--dry-run：只压缩不发送）")
        return 0

    devs = await BleakScanner.discover(timeout=args.scan, return_adv=True)
    target = None
    for d, adv in devs.values():
        name = d.name or adv.local_name or ""
        if args.name in name:
            target = d
            break
    if not target:
        print(f"没扫到设备（找名字含 {args.name} 的）")
        return 1
    print(f"连接 {target.name} {target.address} ...")

    replies = []

    def on_notify(_, data):
        text = data.decode("utf-8", "replace").strip()
        replies.append(text)
        print("  <-", text)

    async with BleakClient(target) as cli:
        await cli.start_notify(TX, on_notify)
        await asyncio.sleep(0.3)

        async def send(line):
            await cli.write_gatt_char(RX, (line + "\n").encode(), response=False)
            await asyncio.sleep(0.02)

        await send(f"I {w} {h}")
        await asyncio.sleep(0.15)
        # MTU 因 bleak 版本而异：优先 mtu_size，其次 mtu，最后退回保守的 180
        mtu = getattr(cli, "mtu_size", None) or getattr(cli, "mtu", None)
        if not mtu:
            try:
                mtu = await cli.request_mtu(517)
            except Exception:
                mtu = 180
        chunk = ((int(mtu) - 8) // 4) * 4   # 4 的倍数：逐包解 base64 不会留半个字节
        print(f"  MTU={mtu}  每包 {chunk} 字符，共 {(len(b64) + chunk - 1) // chunk} 包")
        for i in range(0, len(b64), chunk):
            await send("A " + b64[i:i + chunk])
            await asyncio.sleep(0.005)
        await send("E")
        await asyncio.sleep(0.6)
        await cli.stop_notify(TX)

    ok = False
    for r in replies:
        if r.startswith("OK E"):
            parts = r.split()
            dev_hash = parts[4] if len(parts) > 4 else None
            want = f"{fnv1a(bits):08X}"
            ok = dev_hash == want
            print(f"  校验和：设备 {dev_hash} / 本地 {want} -> " + ("一致" if ok else "不一致！"))
            if parts[3] and parts[3].isdigit() and int(parts[3]) != len(rle):
                print(f"  注意：设备收到 {parts[3]} 字节，本地 {len(rle)} 字节")
    print("结果：", "成功（屏幕上应该已经显示这张图）" if ok else "失败（看上面的回执）")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--dither", action="store_true", help="抖动（适合照片）")
    ap.add_argument("--invert", action="store_true", help="黑白反相")
    ap.add_argument("--name", default="ESP32-NOTE")
    ap.add_argument("--scan", type=float, default=8.0)
    ap.add_argument("--crop", default="center",
                    choices=["center", "top", "bottom", "none"],
                    help="先把图按 320:212 裁一块再缩放（手机长截图必须裁，否则字小到看不清）")
    ap.add_argument("--box", default="", help="手动裁切 x0,y0,x1,y1（原图像素），给了就不按比例裁")
    ap.add_argument("--preview", default="", help="把处理结果另存成 PNG 便于先看效果")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    raise SystemExit(asyncio.run(run(args)))


if __name__ == "__main__":
    main()
