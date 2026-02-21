#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Translate wiki/en/ to wiki/zh/ using LM Studio API."""

import json
import os
import sys
import urllib.request
import urllib.error

# Configuration
BASE_URL = "http://172.23.96.1:12878"
API_ENDPOINT = f"{BASE_URL}/v1/chat/completions"
MODEL = "qwen/qwen3-vl-8b"
SRC_DIR = "wiki/en"
DST_DIR = "wiki/zh"

SYSTEM_PROMPT = """You are a technical translator. Translate the following Markdown content from English to Simplified Chinese (zh-CN).

Rules:
1. Keep all Markdown formatting (headers, lists, tables, links, code blocks).
2. Preserve code blocks exactly - do NOT translate code or technical identifiers.
3. Preserve file paths, class names, method names, variable names.
4. Translate explanatory text, descriptions, comments in prose.
5. Use standard technical terminology in Chinese where appropriate.
6. Output only the translated content, no preamble."""


def translate(text: str) -> str:
    """Call LM Studio API to translate text."""
    payload = {
        "model": MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": text},
        ],
        "temperature": 0.3,
        "max_tokens": 16384,
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        API_ENDPOINT,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            result = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        print(f"  [ERROR] HTTP {e.code}: {e.read().decode('utf-8', errors='replace')}")
        raise
    except urllib.error.URLError as e:
        print(f"  [ERROR] URL error: {e.reason}")
        raise

    choices = result.get("choices", [])
    if not choices:
        raise RuntimeError("No response from model")
    content = choices[0].get("message", {}).get("content", "").strip()
    return content


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src_base = os.path.join(root, SRC_DIR)
    dst_base = os.path.join(root, DST_DIR)

    if not os.path.isdir(src_base):
        print(f"Source directory not found: {src_base}")
        sys.exit(1)

    files = []
    for dirpath, _, filenames in os.walk(src_base):
        for f in filenames:
            if f.endswith(".md"):
                rel = os.path.relpath(os.path.join(dirpath, f), src_base)
                files.append(rel)

    print(f"Found {len(files)} markdown files. Translating with LM Studio ({MODEL})...")

    for i, rel in enumerate(sorted(files), 1):
        src_path = os.path.join(src_base, rel)
        dst_path = os.path.join(dst_base, rel)
        print(f"[{i}/{len(files)}] {rel}")

        with open(src_path, "r", encoding="utf-8") as f:
            content = f.read()

        if not content.strip():
            print("  [SKIP] Empty file")
            os.makedirs(os.path.dirname(dst_path), exist_ok=True)
            with open(dst_path, "w", encoding="utf-8") as f:
                f.write("")
            continue

        try:
            translated = translate(content)
        except Exception as e:
            print(f"  [FAIL] {e}")
            continue

        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        with open(dst_path, "w", encoding="utf-8") as f:
            f.write(translated)
        print("  [OK]")

    print("Done.")


if __name__ == "__main__":
    main()
