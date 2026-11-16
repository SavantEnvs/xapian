#!/usr/bin/env bash
#
# mayhem/test.sh — run xapian-core's own test suites plus a known-answer probe, all pre-built by
# mayhem/build.sh (normal flags, shared libxapian). Reports CTRF.
#   1. kat_probe: stemming, query parsing, BM25 ranking, snippets, serialisation; compared line by
#      line against mayhem/kat_expected.txt (each line is one test).
#   2. apitest on the inmemory, glass and honey backends, and internaltest (each run is one test
#      per its own pass count; a run with no summary line, a failure, or a non-zero exit counts as failed).
#   3. htmlparsertest and atomparsertest from xapian-omega (one test each; they exit non-zero on a mismatch).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

bt="$SRC/mayhem/build-test"
if [ ! -x "$bt/kat_probe" ] || [ ! -x "$bt/tests/apitest" ] || [ ! -x "$bt/tests/internaltest" ]; then
  echo "test runners missing: mayhem/build.sh must build them" >&2
  emit_ctrf "xapian" 0 1 0
  exit 1
fi

passed=0; failed=0; skipped=0

# 1) Known-answer probe.
actual=$(mktemp); trap 'rm -f "$actual"' EXIT
"$bt/kat_probe" > "$actual" 2>&1
expected="$SRC/mayhem/kat_expected.txt"
n=$(wc -l < "$expected")
for i in $(seq 1 "$n"); do
  if [ "$(sed -n "${i}p" "$actual")" = "$(sed -n "${i}p" "$expected")" ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
    echo "KAT mismatch at line $i: expected [$(sed -n "${i}p" "$expected")] got [$(sed -n "${i}p" "$actual")]" >&2
  fi
done
if [ "$(wc -l < "$actual")" -ne "$n" ]; then
  failed=$((failed + 1)); echo "KAT probe printed $(wc -l < "$actual") lines, expected $n" >&2
fi

# 2) Upstream's own suites.
run_suite() {
  local label="$1"; shift
  local out rc p s f
  out=$(cd "$bt/tests" && "$@" 2>&1); rc=$?
  echo "$out" | tail -n 3
  p=$(echo "$out" | sed -n 's/.*All \([0-9][0-9]*\) tests\? passed.*/\1/p' | tail -n1)
  s=$(echo "$out" | sed -n 's/.*passed, \([0-9][0-9]*\) skipped.*/\1/p' | tail -n1)
  # internaltest prints no summary line: count its "... ok" lines when nothing else was reported.
  if [ -z "$p" ] && [ "$rc" -eq 0 ] && ! echo "$out" | grep -q 'Running test: .*\.\.\. \(FAIL\|.*[Ff]ailed\)'; then
    p=$(echo "$out" | grep -c '^Running test: .*\.\.\. ok$')
  fi
  if [ "$rc" -eq 0 ] && [ -n "$p" ] && [ "$p" -gt 0 ]; then
    passed=$((passed + p)); skipped=$((skipped + ${s:-0}))
  else
    f=$(echo "$out" | sed -n 's/.*[^0-9]\([0-9][0-9]*\) tests\? failed.*/\1/p' | tail -n1)
    failed=$((failed + ${f:-1})); echo "suite $label failed (rc=$rc)" >&2
  fi
}
for b in inmemory glass honey; do run_suite "apitest-$b" ./runtest ./apitest -b "$b"; done
run_suite internaltest ./runtest ./internaltest

# 3) Upstream's own tests of the omega parsers (known HTML/Atom documents -> expected dump, title, keywords...).
for t in htmlparsertest atomparsertest; do
  if [ ! -x "$bt/omega/$t" ]; then
    echo "test runner missing: $bt/omega/$t (mayhem/build.sh must build it)" >&2; failed=$((failed + 1)); continue
  fi
  if (cd "$SRC/xapian-omega" && "$bt/omega/$t"); then passed=$((passed + 1)); else failed=$((failed + 1)); echo "$t failed" >&2; fi
done

emit_ctrf "xapian-kat+apitest+internaltest+omegaparsers" "$passed" "$failed" "$skipped"
