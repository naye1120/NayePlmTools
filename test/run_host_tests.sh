#!/bin/bash
# [CN-FORK] Host-side unit tests (WSL clang, no PS5 SDK needed).
#   bash test/run_host_tests.sh
set -u
cd "$(dirname "$0")/.."

fail=0
# run_one <name> <source> [extra cflags...]
run_one() {
    name="$1"; src="$2"; shift 2
    echo "=== $name ==="
    if ! clang -Wall -Wextra -O0 -Isrc/psaccounts "$@" "$src" "test/${name}_test.c" -o "/tmp/${name}_test"; then
        echo "  BUILD FAILED"; fail=1; return
    fi
    "/tmp/${name}_test" || fail=1
}

run_one slot_resolve   src/psaccounts/npsignin.c
run_one offact_genid   src/psaccounts/offact.c
# safety.c really creates SAFETY_DIR, so point it at a temp dir on the host.
run_one safety_origin  src/psaccounts/safety.c -DSAFETY_DIR='"/tmp/safety_origin_test"'

echo "==== overall: $([ $fail -eq 0 ] && echo PASS || echo FAIL) ===="
exit $fail
