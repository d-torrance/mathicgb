# Running the item-10 benchmarks on other hardware

Everything in section 10 of POST-1.4-REVIEW.md was measured on an i5-6300U --
two physical cores plus hyperthreading, `powersave` governor -- with GCC 11.4
on x86-64.  That machine can show *that* `yang1` fails to scale under TBB but
cannot size the regression, and it says nothing about any compiler or
architecture other than its own.  These are the runs worth repeating
elsewhere, in priority order.

## Setup

The variant builds need their own tree, because they are built four times with
different flags and because `bench-variants.patch` should never be committed.
Do not run them in a tree you are working in: an autotools source directory can
hold only one configuration at a time, so an out-of-tree `configure` in an
already-configured checkout fails with "source directory already configured".
Clone instead, and leave your working tree alone.

    git clone --no-hardlinks . /tmp/mgb-bench
    cd /tmp/mgb-bench
    ./autogen.sh
    git apply /path/to/mathicgb/bench-variants.patch

`autogen.sh` runs `autoreconf --install`, so it wants autoconf, automake,
libtool and pkg-config on PATH.  On macOS with Homebrew that is
`brew install autoconf automake libtool pkg-config`, and if `configure` cannot
find memtailor, mathic or TBB, set `PKG_CONFIG_PATH` to the directory holding
their `.pc` files.

Four serial builds, one per variant.  Variant bits: 1 = index `mEntries`
directly instead of via the restrict local, 2 = plain loop instead of the
unrolling, 4 = Duff's-device goto.  Variant 0 is the shipped code.

    for V in 0 1 2 4; do
      mkdir -p b/v$V
      (cd b/v$V && ../../configure --with-tbb=no \
          CXXFLAGS="-O2 -DMATHICGB_BENCH_VARIANT=$V" >/dev/null &&
       make mgb -j)
    done

`make mgb` builds the library and the tool and skips the test suite, so gtest is
not needed.  Note that `CXXFLAGS=` on the `configure` line replaces the default
flags rather than adding to them, which is why `-O2` is written out explicitly.

One TBB build, for section 1 only:

    mkdir -p b/tbb
    (cd b/tbb && ../../configure --with-tbb=yes CXXFLAGS="-O2" >/dev/null &&
     make mgb -j)

Confirm before trusting any number.  The four serial binaries must have
**distinct** object files and produce **identical** output:

    for V in 0 1 2 4; do md5sum b/v$V/src/mathicgb/F4MatrixReducer.o; done

Use the `.o`, not the `.lo` -- libtool's `.lo` is a text stub and is byte
identical across all four variants, so checking it proves nothing.

    mkdir -p run && cp examples/*.ideal run/
    cd run && for V in 0 1 2 4; do rm -f hyclic8-101-trimmed.gb
      ../b/v$V/mgb gb hyclic8-101-trimmed -reducer 26 -outputResult >/dev/null
      md5sum hyclic8-101-trimmed.gb; done; cd ..

Two traps in the tool itself.  `mgb gb` defaults to `-reducer 21`, the
*classic* reducer, so every F4 run needs an explicit `-reducer 26` -- plain
`mgb gb hyclic8-101-trimmed` takes 78 s here and says nothing about F4 or TBB.
And the project name must come before the flags; `mgb gb -reducer 26 foo` fails
with "Too few direct options".

The default build is static, so `b/v0/mgb` is a real executable.  If you
configure `--enable-shared`, `mgb` becomes a libtool wrapper script that
re-execs the binary in `.libs/`, which adds startup cost to every timed run --
time `b/v0/.libs/mgb` in that case, or just leave the default alone.

## 1. TBB scaling on yang1 -- the one that needs your machine

This is the open question.  On two cores TBB buys nothing on `yang1`; on twelve
it was recorded as a 50% regression.  A thread sweep separates "TBB overhead
grows with thread count" from "this workload has no parallelism to find":

    cd run
    for T in 1 2 4 8 12 16; do
      printf "threads=%-3s " $T
      /usr/bin/time -p ../b/tbb/mgb gb yang1 -reducer 26 -threadCount $T \
        2>&1 >/dev/null | tr '\n' ' '
      echo
    done

Then the same sweep on `hyclic8-101-trimmed`, which *does* scale, as a control.

What to look for: if wall time is flat or rising with `-threadCount` on yang1
while `hyclic8` scales, the fault is the join-per-iteration structure in
`reduceToEchelonForm` (`src/mathicgb/F4MatrixReducer.cpp:439-511`), which puts
its `parallel_for` inside a `while` loop with a full join and a global mutex
every iteration -- not TBB itself.

On an M-series chip the performance and efficiency cores are not
interchangeable, so pass `-threadCount` explicitly rather than relying on the
default of 0, and record how many P-cores the part has.

## 2. A second compiler and architecture for the addRowMultiple variants

The results in POST-1.4-REVIEW.md rest on exactly one compiler and one
architecture.  The unrolling result is the suspect one: clang may already
unroll this loop, in which case the hand-unrolling is dead weight on arm64
rather than the 8-12% win it is on GCC/x86-64.

    python3 /path/to/mathicgb/bench.py --reps 15 --cwd run --reducer 26 \
      --input hyclic8-101-trimmed --input hilbertkunz1 \
      --bin "base=$PWD/b/v0/mgb"     --bin "mEntries=$PWD/b/v1/mgb" \
      --bin "noUnroll=$PWD/b/v2/mgb" --bin "duff=$PWD/b/v4/mgb"

The `--bin` paths must be absolute, or relative to `--cwd` rather than to your
shell -- each `mgb` is launched with its working directory set to `--cwd` so
that it finds the `.ideal` files.

`bench.py` runs every (binary, input) cell once per round in round-robin order,
which is what makes the numbers usable on a laptop that throttles: sequential
runs of the same binary drifted up to 40% here.  **Only compare rows within one
invocation.**  It needs nothing but Python 3.

Check whether the compiler unrolled it anyway before concluding anything --
if variant 2's inner loop is already unrolled in the disassembly, a null result
means the compiler did the work, not that unrolling is worthless:

    objdump -d b/v2/src/mathicgb/F4MatrixReducer.o | less   # otool -tv on macOS

`bench.py` reports CPU and wall time only, so it is portable as-is.  If you add
memory reporting, note that `resource.getrusage` gives `ru_maxrss` in bytes on
macOS and in kilobytes on Linux.

## 3. The yang1 baseline discrepancy

PR #65 records 14.2-14.4 s serial for `yang1`; the same run takes 11 m 38 s
here.  That is 49x, and it is not machine speed -- the classic reducer
calibrates this box at 2.2-2.3x slower than the 12-core machine (`yang1` 5.07 s
against 2.24 s, `hyclic8` 78 s against 44.6 s) and `hyclic8` F4 lines up fine.

Bisecting with `-breakAfter` localizes it: `yang1` reaches 4750 of its 4761
basis elements in 32.5 s and then spends roughly 650 s on a tail that adds no
basis elements at all.

    cd run
    /usr/bin/time -l ../b/v0/mgb gb yang1 -reducer 26
    /usr/bin/time -l ../b/v0/mgb gb yang1 -reducer 26 -breakAfter 4750

If the full run finishes in tens of seconds on your machine, the 14.2 s figure
was real and something about this box provokes the tail -- worth knowing what.
If it takes ten minutes there too, the recorded number came from a run that
never entered that stage, and the review's `yang1` baseline needs correcting.

`-l` is the macOS spelling for resource usage; on Linux it is `-v`.  Peak RSS
was only 155 MB at the 4750 mark here, so memory pressure is already ruled out
on this box.
