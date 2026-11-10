#!/bin/bash
#------------------------------------------------------------------------
# tools/run-tests.sh - every suite that needs no plug-in build.
#
#   DSP     tests/DspTests.cpp     - SDK-free, nothing but a compiler
#   Layout  tests/LayoutTests.cpp  - SDK-free
#   Params  tests/ParamsTests.cpp  - header-only SDK files
#                                    (external/vst3sdk, or VST3_SDK_ROOT)
#------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-c++}"
SDK="${VST3_SDK_ROOT:-$ROOT/external/vst3sdk}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
S="$ROOT/source"
fail=0

run () {     # run <label> <output> <flags> <source...>
    local label="$1" out="$2" flags="$3"; shift 3
    printf '\n=== %s ===\n' "$label"
    if ! "$CXX" -std=c++17 $flags -I"$S" -o "$WORK/$out" "$@" 2>"$WORK/build.err"; then
        echo "  BUILD FAILED"; head -20 "$WORK/build.err" | sed 's/^/  /'; fail=1; return
    fi
    "$WORK/$out" || fail=1
}

run "DSP" dsptests "-O2" "$ROOT/tests/DspTests.cpp" "$S/WornVinylDsp.cpp"
run "Layout" layouttests "-O2" "$ROOT/tests/LayoutTests.cpp" "$S/WornVinylDsp.cpp"
if [ -f "$SDK/pluginterfaces/vst/vsttypes.h" ]; then
    run "Params" paramtests "-O2 -I$SDK" "$ROOT/tests/ParamsTests.cpp" "$S/WornVinylParams.cpp" "$S/WornVinylDsp.cpp"
else
    printf '\n=== Params ===\n  SKIPPED: no VST3 SDK at %s (run ./setup-xcode.sh once, or set VST3_SDK_ROOT)\n' "$SDK"
fi

echo
[ $fail -eq 0 ] && echo "ALL SUITES PASSED" || echo "SOMETHING FAILED"
exit $fail
