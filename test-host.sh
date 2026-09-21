#!/bin/sh
set -eu
cd "$(dirname "$0")"
game=${1:-鬼作}
test_save=$(mktemp -d)
roundtrip_save=$(mktemp -d)
letter_save=$(mktemp -d)
gate_save=$(mktemp -d)
stage_save=$(mktemp -d)
trap 'rm -f "$test_save/kisaku-runtime.ini" "$test_save/kisaku-flag-0-100.dat" "$test_save/flags-test.dat"; rmdir "$test_save" 2>/dev/null || :; rm -rf "$roundtrip_save" "$letter_save" "$gate_save" "$stage_save"' EXIT
build/akb-test
build/vm-memory-test
build/param-change-test
build/bowling-test
build/flags-test "$test_save/flags-test.dat"
build/kisaku-bootstrap-test "$game" "$test_save"
build/save-runtime-test "$game" "$roundtrip_save"
build/letter-save-test "$game" "$letter_save"
build/transition-cadence-test "$game" "$test_save"
build/vm-capacity-gate-test "$game" "$gate_save"
build/scene-completion-stage-test "$game" "$stage_save"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/message-panel-test "$game" "$test_save"
build/kisaku-probe "$game"
build/kisaku-akb-probe "$game/layer.arc"
python3 tools/audit.py "$game" --output reports/inventory.json
