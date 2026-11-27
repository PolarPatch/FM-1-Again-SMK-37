#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PolarPatch
"""FM-1 Again's site (GitHub Pages): fm1-base's (base/tools/make_site.py: the installer, the manual, the emulator,
the companion, backup, the firmware) with this firmware's own pages over it (app/site: the front page with the
emulator and its demos, the cheat sheet, the editor), then, with --publish, pushed to the repository's gh-pages.

    /usr/bin/python3 app/tools/make_again_site.py 1.0 [--publish]

Builds the release package (base/tools/build.py app --release VERSION) and the emulator (--web: needs Emscripten,
source ~/emsdk/emsdk_env.sh first) unless they are there already; app/build/site is the result."""
import argparse, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP, BASE = ROOT / "app", ROOT / "base"
PY = sys.executable


def run(*a, **k):
    print("+", " ".join(str(x) for x in a))
    subprocess.run([str(x) for x in a], check=True, **k)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version")
    ap.add_argument("--publish", action="store_true", help="push app/build/site to gh-pages")
    ap.add_argument("--rebuild", action="store_true", help="build the package and the emulator again")
    a = ap.parse_args()
    v, out = a.version, APP / "build" / "site"
    pkg = APP / "build" / f"again-{v}.fwsc"
    if a.rebuild or not pkg.exists():
        run(PY, BASE / "tools/build.py", APP, "--release", v)
    if a.rebuild or not (APP / "build/emu/app.wasm").exists():
        run(PY, BASE / "tools/build.py", APP, "--web", "--release", v)
    run(PY, BASE / "tools/make_site.py", APP, pkg, v, out)
    site = APP / "site"
    for f in site.rglob("*"):                           # this firmware's pages over fm1-base's front page
        if f.is_dir() or f.name == "landing.md":
            continue
        dst = out / f.relative_to(site)
        dst.parent.mkdir(parents=True, exist_ok=True)
        if f.suffix in (".html", ".js"):
            dst.write_text(f.read_text(encoding="utf-8").replace("{{VERSION}}", v), encoding="utf-8")
        else:
            shutil.copy(f, dst)
    shutil.copy(APP / "assets/fonts/texgyreheros-bold.otf", out / "heros-bold.otf")
    shutil.copy(APP / "assets/fonts/GUST-FONT-LICENSE.txt", out / "GUST-FONT-LICENSE.txt")
    shutil.copy(APP / "assets/polarpatch-logo.png", out / "polarpatch-logo.png")
    shutil.copy(ROOT / "LICENSE", out / "LICENSE")
    (out / ".nojekyll").touch()
    print(f"site: {out}")
    if a.publish:
        remote = subprocess.run(["git", "-C", ROOT, "remote", "get-url", "origin"], capture_output=True, text=True,
                                check=True).stdout.strip()
        sha = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                             check=True).stdout.strip()
        shutil.rmtree(out / ".git", ignore_errors=True)
        run("git", "init", "-q", "-b", "gh-pages", cwd=out)
        run("git", "add", "-A", cwd=out)
        run("git", "commit", "-q", "-m", f"Site for FM-1 Again {v} (from {sha})", cwd=out)
        run("git", "push", "-q", "-f", remote, "gh-pages", cwd=out)
        shutil.rmtree(out / ".git")
        print("pushed the site to gh-pages")


if __name__ == "__main__":
    main()
