#!/usr/bin/env bash
#
# mayhem/build.sh — build xapian-core twice (out of tree), then the fuzz harnesses:
#   mayhem/build-fuzz  sanitized + SanCov static library  -> /mayhem/fuzz_* (+ -standalone reproducers)
#   mayhem/build-test  upstream's normal flags, shared    -> apitest/internaltest + KAT probe for test.sh
# Runs offline: autotools come from the image, nothing is downloaded.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
core="$SRC/xapian-core"

# 0) bootstrap the git tree into a configurable one (generated files are all gitignored).
if [ ! -x "$core/configure" ]; then
  ./bootstrap --download-tools=never xapian-core
fi

# 1) Sanitized, SanCov-instrumented static library for the harnesses. configure's own probe
#    programs must not trip LeakSanitizer, hence detect_leaks=0 for this step only.
mkdir -p mayhem/build-fuzz
(
  cd mayhem/build-fuzz
  if [ ! -f Makefile ]; then
    ASAN_OPTIONS=detect_leaks=0 "$core/configure" CC="$CC" CXX="$CXX" \
      CFLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link" \
      CXXFLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link" \
      LDFLAGS="$SANITIZER_FLAGS" \
      --disable-shared --enable-static --disable-documentation --enable-maintainer-mode
  fi
  ASAN_OPTIONS=detect_leaks=0 make -j"$MAYHEM_JOBS"
)
fuzz_lib=$(ls "$SRC"/mayhem/build-fuzz/.libs/libxapian*.a | head -n1)
fuzz_deps=$(sed -n "s/^dependency_libs='\(.*\)'/\1/p" "$SRC"/mayhem/build-fuzz/libxapian*.la | head -n1)
# fuzz_remote_server includes the internal remoteserver.h, which needs the core source dirs and config.h.
inc="-I$SRC/mayhem/build-fuzz/include -I$core/include -I$core/net -I$core/common -I$core -I$SRC/mayhem/build-fuzz"

# 1b) The xapian-omega parsers the omega harnesses fuzz (omindex runs them on arbitrary documents; omega
#     decodes web-client CGI parameters), compiled with the same sanitizer and SanCov flags into a static archive.
#     Omega's own configure is not needed: the two generated headers come from its perl generators, and
#     the core build's config.h serves (HAVE_ICONV is defined as in a real omega build, on glibc).
omega="$SRC/xapian-omega"
od="$SRC/mayhem/build-fuzz/omega"
mkdir -p "$od"
(
  cd "$od"
  perl -I"$core/common" "$omega/gen-htmltags" "$omega" < "$omega/htmlparser.tokens"
  perl -I"$core/common" "$omega/gen-namedents" "$omega"
)
oinc="-I$od -I$omega $inc -DHAVE_ICONV -DICONV_CONST="
omega_objs=""
for f in htmlparser xmlparser atomparser genericxmlparser utf8convert datetime timegm cgiparam; do
  $CXX -O1 -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $oinc \
    -c "$omega/$f.cc" -o "$od/$f.o"
  omega_objs="$omega_objs $od/$f.o"
done
rm -f "$od/libomegaparsers.a"
ar rcs "$od/libomegaparsers.a" $omega_objs

# 2) Harnesses: libFuzzer binary + run-once standalone reproducer, both with the leak hook.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o mayhem/build-fuzz/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o mayhem/build-fuzz/standalone_main.o
for h in fuzz_queryparser fuzz_serialise fuzz_index_snippet fuzz_db_file fuzz_remote_server \
         fuzz_omega_html fuzz_omega_xml fuzz_omega_cgi; do
  hinc="$inc"; hlib=""
  case "$h" in fuzz_omega_*) hinc="$oinc"; hlib="$od/libomegaparsers.a" ;; esac
  $CXX -O1 -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $hinc \
    -c "mayhem/harnesses/$h.cc" -o "mayhem/build-fuzz/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "mayhem/build-fuzz/$h.o" \
    mayhem/build-fuzz/lsan_off.o $hlib "$fuzz_lib" $fuzz_deps -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "mayhem/build-fuzz/$h.o" mayhem/build-fuzz/standalone_main.o \
    mayhem/build-fuzz/lsan_off.o $hlib "$fuzz_lib" $fuzz_deps -o "$SRC/$h-standalone"
done
# Tools used to (re)generate seeds and to inspect findings.
make -j"$MAYHEM_JOBS" -C mayhem/build-fuzz xapian-compact xapian-check xapian-delve simpleindex >/dev/null 2>&1 || true

# 3) Test build with upstream's normal flags (no sanitizer, no -gdwarf-3).
mkdir -p mayhem/build-test
(
  cd mayhem/build-test
  if [ ! -f Makefile ]; then
    env -u CFLAGS -u CXXFLAGS -u LDFLAGS "$core/configure" CC="$CC" CXX="$CXX" \
      CXXFLAGS="-O1 $COVERAGE_FLAGS" CFLAGS="-O1 $COVERAGE_FLAGS" LDFLAGS="$COVERAGE_FLAGS" \
      --enable-shared --disable-static --disable-documentation --enable-maintainer-mode
  fi
  make -j"$MAYHEM_JOBS"
  make -j"$MAYHEM_JOBS" -C tests apitest internaltest
)
$CXX -O1 -std=c++17 -I"$SRC/mayhem/build-test/include" -I"$core/include" mayhem/kat_probe.cc \
  -L"$SRC/mayhem/build-test/.libs" -Wl,-rpath,"$SRC/mayhem/build-test/.libs" -lxapian \
  -o "$SRC/mayhem/build-test/kat_probe"

# Upstream's own tests for the omega parsers the omega harnesses fuzz (htmlparsertest, atomparsertest),
# built against the normal-flags tree; omega's configure is not needed (see step 1b).
ot="$SRC/mayhem/build-test/omega"
mkdir -p "$ot"
tinc="-I$od -I$omega -I$SRC/mayhem/build-test/include -I$core/include -I$core/net -I$core/common -I$core -I$SRC/mayhem/build-test -DHAVE_ICONV -DICONV_CONST="
for f in htmlparser xmlparser atomparser utf8convert datetime timegm; do
  $CXX -O1 -std=c++17 $tinc -c "$omega/$f.cc" -o "$ot/$f.o"
done
# keyword2() is internal to the shared libxapian, so the test build compiles it in.
$CXX -O1 -std=c++17 $tinc -c "$core/common/keyword.cc" -o "$ot/keyword.o"
for t in htmlparsertest atomparsertest; do
  $CXX -O1 -std=c++17 $tinc "$omega/$t.cc" "$ot"/htmlparser.o "$ot"/xmlparser.o "$ot"/atomparser.o \
    "$ot"/utf8convert.o "$ot"/datetime.o "$ot"/timegm.o "$ot"/keyword.o \
    -L"$SRC/mayhem/build-test/.libs" -Wl,-rpath,"$SRC/mayhem/build-test/.libs" -lxapian -o "$ot/$t"
done
