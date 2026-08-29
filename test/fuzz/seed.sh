#!/usr/bin/env sh
# Seed the fuzz corpora from the repo's own Lcl sources.
#
# Every .lcl file up to 8 KB becomes one entry in test/fuzz/seeds/
# (both targets read the same grammar), named seed-<path>. Seeds are
# curated and committed; the fuzzers read them as a read-only extra
# corpus and write their own discoveries to test/fuzz/corpus/<target>.
# Fixed-bug reproducers also live here as regression-<issue>-<what>.
# Re-run after adding tests or syntax; then `run.sh merge`.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p test/fuzz/seeds

n=0
for f in $(find test/conformance test/anaphoric_macros.lcl packages/*/test lib/*/src lib/*/test bench docs \
             -name '*.lcl' 2>/dev/null | sort); do
  [ "$(wc -c < "$f")" -le 8192 ] || continue
  name="seed-$(printf '%s' "$f" | tr '/' '_')"
  cp "$f" "test/fuzz/seeds/$name"
  n=$((n + 1))
done
echo "seeded $n files into test/fuzz/seeds/ ($(ls test/fuzz/seeds | wc -l) entries total)"
