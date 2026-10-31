#!/usr/bin/env python
"""图传端到端自测：状态 / 单帧 / 拍照 / 流与控制面并发。

用法:
    python scripts/verify_cam.py 192.168.88.32
    python scripts/verify_cam.py esp32cam.local
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import time
import urllib.request


def get(url: str, timeout: float = 10) -> tuple[int, bytes]:
    req = urllib.request.Request(url, headers={"User-Agent": "verify_cam/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", default="esp32cam.local")
    ap.add_argument("--out", default="logs/snap.jpg")
    args = ap.parse_args()

    base = f"http://{args.host}"
    ok = True

    print(f"== 目标 {base} ==")

    # 1) 状态
    t0 = time.time()
    code, body = get(base + "/status.json")
    st = json.loads(body)
    print(f"[1] /status.json  {code}  {time.time()-t0:.2f}s")
    for k in ("mode", "ip", "rssi", "res", "fps", "frames", "viewers", "heap", "sd"):
        print(f"      {k:8s} = {st.get(k)}")

    # 2) 单帧
    t0 = time.time()
    code, jpg = get(base + "/capture")
    open(args.out, "wb").write(jpg)
    print(f"[2] /capture      {code}  {len(jpg)/1024:.1f} KB  {time.time()-t0:.2f}s  -> {args.out}")
    if jpg[:2] != b"\xff\xd8":
        print("      ✗ 不是 JPEG")
        ok = False

    # 3) 拍照（没卡时应给出提示）
    code, body = get(base + "/shoot")
    text = body.decode("utf-8", "replace")
    msg = text.split("<p>")[1].split("</p>")[0] if "<p>" in text else text[:80]
    print(f"[3] /shoot        {code}  {msg.strip()}")

    # 4) 关键：流开着的时候，控制面还能不能响应
    print("[4] 并发测试：开一个 MJPEG 连接，同时请求控制接口")
    s = socket.create_connection((args.host, 81), timeout=8)
    s.sendall(b"GET /stream HTTP/1.1\r\nHost: x\r\n\r\n")
    s.settimeout(6)
    buf = b""
    t0 = time.time()
    while time.time() - t0 < 4 and len(buf) < 200_000:
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
    frames = buf.count(b"\xff\xd8")
    print(f"      流已收到 {len(buf)/1024:.0f} KB，JPEG 帧数 ≈ {frames}")

    t0 = time.time()
    try:
        code, body = get(base + "/status.json", timeout=5)
        dt = time.time() - t0
        st2 = json.loads(body)
        print(f"      流进行中 /status.json {code}  用时 {dt:.2f}s  viewers={st2.get('viewers')}  "
              f"fps={st2.get('fps')}")
        if dt > 3:
            print("      ✗ 控制面被拖慢")
            ok = False
    except Exception as e:  # noqa: BLE001
        print(f"      ✗ 流进行时控制面失败：{e}")
        ok = False

    s.close()

    print("\n结果:", "全部通过 ✔" if ok else "有问题 ✗")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
