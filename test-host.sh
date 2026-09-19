#!/bin/sh
set -eu
cd "$(dirname "$0")"
game=${1:-鬼作}
test_save=$(mktemp -d)
trap 'rm -f "$test_save/kisaku-runtime.ini" "$test_save/kisaku-flag-0-100.dat" "$test_save/flags-test.dat"; rmdir "$test_save"' EXIT
build/akb-test
build/vm-memory-test
build/bowling-test
build/flags-test "$test_save/flags-test.dat"
build/kisaku-bootstrap-test "$game" "$test_save"
build/kisaku-probe "$game"
build/kisaku-akb-probe "$game/layer.arc"
python3 tools/audit.py "$game" --output reports/inventory.json
