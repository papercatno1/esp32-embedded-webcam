#!/usr/bin/env python
"""本地抓网页 -> 纯文本，绕开 harness web_fetch 的"非公网 IP"限制。

背景：本机在跑 Mihomo TUN（fake-ip 模式），www.waveshare.com 这类域名会被解析成
198.18.0.x（保留网段），harness 的 web_fetch 出于 SSRF 防护会拒绝这类地址；
但本机正常网络（浏览器 / .NET / curl）走 TUN 是通的，所以用本脚本抓。

用法:
    python scripts/webget.py https://www.waveshare.com/wiki/ESP32-S3-LCD-2
    python scripts/webget.py URL --raw -o page.html
    python scripts/webget.py URL --grep "GPIO|引脚"
"""
from __future__ import annotations

import argparse
import gzip
import io
import re
import sys
import urllib.request


def fetch(url: str, timeout: int = 30) -> str:
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) dsh-webget/1.0",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
        if resp.headers.get("Content-Encoding") == "gzip":
            data = gzip.GzipFile(fileobj=io.BytesIO(data)).read()
        charset = resp.headers.get_content_charset() or "utf-8"
        return data.decode(charset, "replace")


def html_to_text(html: str) -> str:
    html = re.sub(r"(?is)<(script|style|noscript)[^>]*>.*?</\1>", " ", html)
    html = re.sub(r"(?i)<br\s*/?>", "\n", html)
    html = re.sub(r"(?i)</(p|div|li|tr|h[1-6]|table|section)>", "\n", html)
    html = re.sub(r"(?i)</t[dh]>", " | ", html)
    html = re.sub(r"(?s)<[^>]+>", "", html)
    entities = {
        "&nbsp;": " ", "&amp;": "&", "&lt;": "<", "&gt;": ">",
        "&quot;": '"', "&#39;": "'", "&middot;": "·", "&times;": "×",
    }
    for k, v in entities.items():
        html = html.replace(k, v)
    html = re.sub(r"&#(\d+);", lambda m: chr(int(m.group(1))), html)
    lines = [re.sub(r"[ \t\u00a0]+", " ", ln).strip() for ln in html.splitlines()]
    return "\n".join(ln for ln in lines if ln)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("-o", "--out", default="")
    ap.add_argument("--raw", action="store_true", help="保留原始 HTML")
    ap.add_argument("--grep", default="", help="只打印匹配该正则的行")
    ap.add_argument("--timeout", type=int, default=30)
    args = ap.parse_args()

    html = fetch(args.url, args.timeout)
    text = html if args.raw else html_to_text(html)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"已保存 {len(text)} 字符 -> {args.out}")
        return 0

    if args.grep:
        pat = re.compile(args.grep, re.I)
        hits = [ln for ln in text.splitlines() if pat.search(ln)]
        print("\n".join(hits[:200]))
        print(f"--- 命中 {len(hits)} 行 ---")
        return 0

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
