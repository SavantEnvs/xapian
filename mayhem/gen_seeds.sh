#!/usr/bin/env bash
# Regenerates the seeds that are produced by Xapian itself (run after mayhem/build.sh; needs the build-test tree).
set -euo pipefail
cd "${SRC:-/mayhem}"
b=mayhem/build-test
tmp=$(mktemp -d "${TMPDIR:-/tmp}/seedgen.XXXXXX")
clang++ -std=c++17 -Iinclude -I"$b/include" -Ixapian-core/include mayhem/gen_seeds.cc \
  -L"$b/.libs" -Wl,-rpath,"$PWD/$b/.libs" -lxapian -o "$tmp/gen"
"$tmp/gen" "$tmp/out"
cp "$tmp"/out/serialise/* mayhem/fuzz_serialise/testsuite/
for be in glass honey; do
  mayhem/build-fuzz/bin/xapian-compact --single-file --blocksize=2048 --backend="$be" \
    "$tmp/out/dbs/glass" "$tmp/out/$be.db"
  cp "$tmp/out/$be.db" "mayhem/fuzz_db_file/testsuite/seed_$be.db"
done
# fuzz_remote_server: client conversations recorded against a real xapian-progsrv.
clang -shared -fPIC mayhem/rec_read.c -o "$tmp/rec.so" -ldl
clang++ -std=c++17 -Iinclude -I"$b/include" -Ixapian-core/include mayhem/gen_remote_seeds.cc \
  -L"$b/.libs" -Wl,-rpath,"$PWD/$b/.libs" -lxapian -o "$tmp/gen_remote"
mkdir -p "$tmp/remote_out" "$tmp/remote_scratch"
"$tmp/gen_remote" "$tmp/remote_out" "$tmp/remote_scratch" "$tmp/rec.so" "$PWD/$b/bin/xapian-progsrv"
mkdir -p mayhem/fuzz_remote_server/testsuite
cp "$tmp"/remote_out/* mayhem/fuzz_remote_server/testsuite/
rm -rf "$tmp"
