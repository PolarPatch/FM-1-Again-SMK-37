#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# FM-1 Again SMK-37's tests in the host simulator: every app/tests/*.txt but saver.txt (no screensaver film on the
# SMK-37); a FAIL in any fails the run.
set -e
cd "$(dirname "$0")/.."
"${PYTHON:-python3}" base/tools/build.py app --host
fail=0
for t in app/tests/*.txt; do
    [ "$(basename "$t")" = saver.txt ] && continue
    out=$(app/build/host/again-smk37_host "$t" app/build/host 2>&1) || { echo "$t: crashed"; echo "$out" | tail -5; fail=1; continue; }
    if echo "$out" | grep -q FAIL; then echo "$t:"; echo "$out" | grep FAIL; fail=1; else echo "$t: ok"; fi
done
exit $fail
