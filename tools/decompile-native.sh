#!/bin/sh
# Static analysis only. The EXE is input data and is never launched.
set -eu
cd "$(dirname "$0")/.."
: "${GHIDRA_HOME:?Set GHIDRA_HOME to the installed Ghidra directory}"
mkdir -p local/ghidra-project local/decompiled
"$GHIDRA_HOME/support/analyzeHeadless" "$PWD/local/ghidra-project" Kisaku \
  -import "$PWD/鬼作/AI6WIN.exe" -overwrite -max-cpu 4 \
  -scriptPath "$PWD/tools/ghidra" -postScript ExportNative.java "$PWD/local/decompiled" \
  -log "$PWD/local/ghidra-analysis.log" -scriptlog "$PWD/local/ghidra-export.log"
