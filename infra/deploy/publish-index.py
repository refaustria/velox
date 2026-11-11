#!/usr/bin/env python3
"""Regenerate downloads/latest.json and downloads/index.html from the version directories (called by publish-download.sh).

Usage: publish-index.py <downloads dir> <index template>
Layout: <downloads>/<ver>/Velox-<ver>-{Linux.tar.zst,macOS.zip,Windows.zip} + SHA256SUMS. The newest version (sort -V order)
is "latest"; at most two versions are listed. Output is written atomically (temp file + rename).
"""
import html
import json
import os
import re
import sys
from datetime import datetime, timezone

PLATFORMS = (("linux", "Linux", "-Linux.tar.zst"), ("macos", "macOS", "-macOS.zip"), ("windows", "Windows", "-Windows.zip"))


def version_key(v):
    return [(0, int(p)) if p.isdigit() else (1, p) for p in re.split(r"[.\-+]", v)]


def read_sums(path):
    sums = {}
    try:
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                parts = line.strip().split(None, 1)
                if len(parts) == 2:
                    sums[parts[1].lstrip("*")] = parts[0]
    except FileNotFoundError:
        pass
    return sums


def scan(dl, ver):
    sums = read_sums(os.path.join(dl, ver, "SHA256SUMS"))
    out = {}
    for key, label, suffix in PLATFORMS:
        name = "Velox-%s%s" % (ver, suffix)
        p = os.path.join(dl, ver, name)
        if os.path.isfile(p) and name in sums:
            out[key] = {"label": label, "name": name, "url": "/downloads/%s/%s" % (ver, name),
                        "sha256": sums[name], "size": os.path.getsize(p)}
    return out


def atomic_write(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(text)
    os.chmod(tmp, 0o644)
    os.replace(tmp, path)


def human(n):
    return "%.1f MB" % (n / 1048576) if n >= 1048576 else "%d KB" % round(n / 1024)


def main():
    dl, template = sys.argv[1], sys.argv[2]
    versions = sorted((d for d in os.listdir(dl) if os.path.isdir(os.path.join(dl, d))), key=version_key, reverse=True)[:2]
    if not versions:
        raise SystemExit("no version directories under " + dl)
    latest = versions[0]
    files = scan(dl, latest)
    atomic_write(os.path.join(dl, "latest.json"),
                 json.dumps({"version": latest, "files": {k: {"url": v["url"], "sha256": v["sha256"], "size": v["size"]}
                                                          for k, v in files.items()}}, indent=2) + "\n")
    rows = []
    for ver in versions:
        for key, v in scan(dl, ver).items():
            rows.append('<tr><td>%s</td><td>%s</td><td><a href="%s">%s</a></td><td>%s</td><td><code>%s</code></td></tr>' % (
                html.escape(ver), v["label"], html.escape(v["url"]), html.escape(v["name"]), human(v["size"]), v["sha256"]))
    with open(template, encoding="utf-8") as fh:
        page = fh.read()
    page = (page.replace("@@LATEST_VERSION@@", html.escape(latest))
                .replace("@@GENERATED_AT@@", datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"))
                .replace("@@ROWS@@", "\n".join(rows)))
    atomic_write(os.path.join(dl, "index.html"), page)


if __name__ == "__main__":
    main()
