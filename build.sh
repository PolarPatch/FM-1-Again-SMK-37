#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# FM-1 Again SMK-37: the firmware (app/) on fm1-base (base/, its smk37-inhouse branch). Arguments go to
# base/tools/build.py: --release X.Y.Z, --host. MODEL=pro|mke picks the SMK-37 model (fm1.toml: pro).
#   ./build.sh                          app/build/again-smk37.fwsc
#   MODEL=mke ./build.sh --release 1.0.7   app/build/again-smk37-mke-1.0.7.fwsc
set -e
cd "$(dirname "$0")"
[ -n "$MODEL" ] && export FM1B_MODEL="$MODEL"
exec "${PYTHON:-python3}" base/tools/build.py app "$@"
