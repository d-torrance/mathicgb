# Running the item-10 benchmarks on other hardware

Everything below was measured on an i5-6300U (2 physical cores + HT,
`powersave`) with GCC 11.4 on x86-64.  That machine can show *that* `yang1`
fails to scale under TBB but cannot size the regression, and it says nothing
about any compiler or architecture other than its own.  These are the runs
worth repeating elsewhere, in priority order.

## Setup

    git apply bench-variants.patch          # adds MATHICGB_BENCH_VARIANT
    mkdir -p bench/run && cp examples/*.ideal bench/run/

    for V in 0 1 2 4; do
      cmake -S . -B bench/v$V -DCMAKE_BUILD_TYPE=Release -Dwith_tbb=OFF \
        -Denable_tests=OFF -DCMAKE_CXX_FLAGS="-DMATHICGB_BENCH_VARIANT=$V"
      cmake --build bench/v$V -j --target mgb
    done
    cmake -S . -B bench/tbb -DCMAKE_BUILD_TYPE=Release -Dwith_tbb=ON \
      -Denable_tests=OFF && cmake --build bench/tbb -j --target mgb

Variant bits: 1 = index `mEntries` directly instead of via the restrict local,
2 = plain loop instead of the unrolling, 4 = Duff's-device goto.  Variant 0 is
the shipped code.  Confirm before trusting anything: the four `mgb` binaries
must have **distinct** `F4MatrixReducer.cpp.o` and produce **identical**
`.gb` output.

    for V in 0 1 2 4; do md5sum $(find bench/v$V -name 'F4MatrixReducer.cpp.o'); done
    cd bench/run && for V in 0 1 2 4; do rm -f hyclic8-101-trimmed.gb; \
      ../v$V/mgb gb hyclic8-101-trimmed -reducer 26 -outputResult >/dev/null; \
      md5sum hyclic8-101-trimmed.gb; done

Two traps.  `mgb gb` defaults to `-reducer 21`, the *classic* reducer, so every
F4 run needs an explicit `-reducer 26`.  And the project name must come before
the flags -- `mgb gb -reducer 26 foo` fails with "Too few direct options".

## 1. TBB scaling on yang1 -- the one that needs your machine

This is the open question.  On two cores TBB buys nothing on `yang1`; on twelve
it was recorded as a 50% regression.  A thread sweep separates "TBB overhead"
from "this workload has no parallelism":

    cd bench/run
    for T in 1 2 4 8 12 16; do
      printf "threads=%-3s " $T
      /usr/bin/time -p ../tbb/mgb gb yang1 -reducer 26 -threadCount $T 2>&1 >/dev/null | head -3 | tr '\n' ' '
      echo
    done

Then the same sweep on `hyclic8-101-trimmed`, which *does* scale, as a control.

What to look for: if wall time is flat or rising with `-threadCount` on yang1
while `hyclic8` scales, the fault is the join-per-iteration structure in
`reduceToEchelonForm` (`F4MatrixReducer.cpp:439-511`), not TBB itself.

On an M-series chip, note that performance and efficiency cores are not
interchangeable, so report `-threadCount` explicitly rather than relying on the
default of 0, and say how many P-cores the part has.

## 2. Apple clang on arm64 for the addRowMultiple variants

Currently measured on exactly one compiler and one architecture.  The unrolling
result in particular is suspect elsewhere: clang may already unroll this loop,
in which case the hand-unrolling is dead weight rather than an 8-12% win.

    python3 bench.py --reps 15 --cwd bench/run --reducer 26 \
      --input hyclic8-101-trimmed --input hilbertkunz1 \
      --bin "base=bench/v0/mgb"     --bin "mEntries=bench/v1/mgb" \
      --bin "noUnroll=bench/v2/mgb" --bin "duff=bench/v4/mgb"

`bench.py` interleaves round-robin across all cells, which is what makes the
numbers usable on a laptop that throttles -- sequential runs of the same binary
drifted up to 40% here.  Only compare rows within one invocation.

Check whether clang unrolled it anyway before concluding anything:

    clang++ -O2 -S -o - ... F4MatrixReducer.cpp   # or objdump the .o

## 3. The yang1 baseline discrepancy

PR #65 records 14.2-14.4 s serial for `yang1`; the same run takes 11 m 38 s
here, which is 49x and is not machine speed.  It reaches 4750 of 4761 basis
elements in ~32 s and spends ~650 s on a tail that adds no basis elements.

    cd bench/run
    /usr/bin/time -l ../v0/mgb gb yang1 -reducer 26              # full run
    /usr/bin/time -l ../v0/mgb gb yang1 -reducer 26 -breakAfter 4750

If the full run completes in tens of seconds on your machine, the 14.2 s figure
was real and something about this box provokes the tail -- worth knowing which.
If it takes ten minutes there too, the recorded number came from a run that
never entered that stage, and the review's yang1 baseline needs correcting.
`-l` (macOS) reports peak RSS; it was only 155 MB at the 4750 mark here, so
memory pressure is already ruled out on this box.
