#!/usr/bin/env sh
# Build, run, merge and replay the libFuzzer targets. Usage:
#
#   test/fuzz/run.sh build                 # CC=clang cmake into build-fuzz/
#   test/fuzz/run.sh compile [seconds]     # fuzz the scanner/compiler (default 600 s)
#   test/fuzz/run.sh eval [seconds]        # fuzz compile+eval, hermetic core
#   test/fuzz/run.sh merge                 # minimize both corpora in place
#   test/fuzz/run.sh replay <artifact>...  # re-run artifacts alone, fresh process
#
# Findings land in test/fuzz/artifacts/<target>/ (crash-*, timeout-*,
# oom-*, slow-unit-*). Replay a finding alone before believing it: a
# timeout that finishes in a few seconds in a fresh process is load
# from the parallel workers, not a bug (see README.md).
set -eu
cd "$(dirname "$0")/../.."

BUILD=build-fuzz
JOBS=${FUZZ_JOBS:-$(nproc 2>/dev/null || echo 4)}
COMMON="-dict=test/fuzz/lcl.dict -max_len=4096 -timeout=30 -rss_limit_mb=2048 -print_final_stats=1"

cmd=${1:-}
shift || true

case "$cmd" in
  build)
    CC=clang cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DLCL_BUILD_FUZZERS=ON \
      -DLCL_BUILD_SHARED=OFF -DLCL_BUILD_CLI=OFF -DLCL_BUILD_TESTS=OFF
    cmake --build "$BUILD" --parallel
    ;;
  compile|eval)
    secs=${1:-600}
    mkdir -p "test/fuzz/artifacts/$cmd" "test/fuzz/corpus/$cmd"
    # first dir receives new inputs; seeds/ is read-only extra corpus
    exec "$BUILD/fuzz-$cmd" $COMMON \
      -artifact_prefix="test/fuzz/artifacts/$cmd/" \
      -max_total_time="$secs" -jobs="$JOBS" -workers="$JOBS" \
      "test/fuzz/corpus/$cmd" test/fuzz/seeds
    ;;
  merge)
    for t in compile eval; do
      mkdir -p "test/fuzz/corpus/$t"
      tmp="test/fuzz/corpus/$t.merged"
      rm -rf "$tmp"; mkdir -p "$tmp"
      # minimize corpus/<t> against the seeds: entries whose coverage
      # the seeds already provide are dropped
      "$BUILD/fuzz-$t" -merge=1 -dict=test/fuzz/lcl.dict -max_len=4096 -timeout=30 \
        "$tmp" test/fuzz/seeds "test/fuzz/corpus/$t" 2>&1 | tail -1
      # libFuzzer names entries by content SHA-1, so a merged entry
      # that is byte-identical to a seed is recognisable by name
      seedsums=" $(sha1sum test/fuzz/seeds/* | cut -d' ' -f1 | tr '\n' ' ') "
      rm -rf "test/fuzz/corpus/$t"; mkdir -p "test/fuzz/corpus/$t"
      for f in "$tmp"/*; do
        [ -e "$f" ] || continue
        case "$seedsums" in *" $(basename "$f") "*) continue ;; esac
        cp "$f" "test/fuzz/corpus/$t/"
      done
      rm -rf "$tmp"
      echo "$t: $(ls "test/fuzz/corpus/$t" | wc -l) entries"
    done
    ;;
  replay)
    for a in "$@"; do
      case "$a" in
        */compile/*) t=compile ;;
        *) t=eval ;;
      esac
      echo "== $a"
      "$BUILD/fuzz-$t" -timeout=30 -rss_limit_mb=2048 "$a" 2>&1 | tail -3
    done
    ;;
  *)
    sed -n '2,13p' "$0"; exit 2 ;;
esac
