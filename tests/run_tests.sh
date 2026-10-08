#!/usr/bin/env bash
# Cascade test runner.
#
# Every tests/<category>/<name>.casc has a sibling <name>.expected holding
# the compiler's full output followed by its exit status. A test passes
# when the actual output matches exactly AND the program failed (or
# succeeded) at the stage its category says it should:
#
#   valid/     compiles and runs           -> exit 0
#   lexical/   lexical error               -> exit 1, "lexical error"
#   syntax/    syntax error                -> exit 1, "syntax error"
#   semantic/  semantic error              -> exit 1, "semantic error"
#   runtime/   compiles, fails while running -> exit 3, "runtime error"
#
# usage: tests/run_tests.sh [--update]
#   --update   (re)write the .expected files from the current output —
#              only after checking by hand that the new output is right.

set -u
cd "$(dirname "$0")/.."
BIN=./cascade
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not built (run make)"; exit 2
fi

pass=0; fail=0; failed=()

run_one() {
    local src=$1
    local out
    out=$("$BIN" "$src" 2>&1); local code=$?
    printf '%s\n[exit %d]\n' "$out" "$code"
}

stage_ok() { # category, output, exit code
    case $1 in
        valid)    [ "$3" -eq 0 ] ;;
        lexical)  [ "$3" -eq 1 ] && grep -q "lexical error" <<<"$2" ;;
        syntax)   [ "$3" -eq 1 ] && grep -q "syntax error" <<<"$2" ;;
        semantic) [ "$3" -eq 1 ] && grep -q "semantic error" <<<"$2" ;;
        runtime)  [ "$3" -eq 3 ] && grep -q "runtime error" <<<"$2" ;;
        *)        return 1 ;;
    esac
}

# The grammar itself must stay SLR(1): --tables exits non-zero on conflicts.
if "$BIN" --tables >/dev/null 2>&1; then
    echo "PASS  grammar is SLR(1) (0 conflicts)"; pass=$((pass+1))
else
    echo "FAIL  grammar has SLR conflicts (run ./cascade --tables)"; fail=$((fail+1)); failed+=("grammar")
fi

for src in tests/*/*.casc; do
    category=$(basename "$(dirname "$src")")
    expected="${src%.casc}.expected"
    actual=$(run_one "$src")
    code=$(tail -n1 <<<"$actual" | sed 's/\[exit \([0-9]*\)\]/\1/')

    if [ $UPDATE -eq 1 ]; then
        printf '%s\n' "$actual" > "$expected"
    fi

    if ! stage_ok "$category" "$actual" "$code"; then
        echo "FAIL  $src  (wrong stage/exit status $code for category '$category')"
        fail=$((fail+1)); failed+=("$src")
    elif [ ! -f "$expected" ]; then
        echo "FAIL  $src  (no .expected file; run with --update after checking output)"
        fail=$((fail+1)); failed+=("$src")
    elif [ "$actual" != "$(cat "$expected")" ]; then
        echo "FAIL  $src  (output differs from $expected)"
        diff <(printf '%s\n' "$actual") "$expected" | sed 's/^/        /' | head -20
        fail=$((fail+1)); failed+=("$src")
    else
        echo "PASS  $src"; pass=$((pass+1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
