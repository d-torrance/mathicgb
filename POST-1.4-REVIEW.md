# Review of changes since v1.4

Scope: the 19 commits between `v1.4` (eae9200) and `71cfdba`, contributed as
PRs #60-#68.  Reviewed 2026-08-29.

Baseline verified before reviewing: 244 tests in 27 suites pass in both a
Release and a Debug cmake build against system memtailor/mathic 1.0, TBB
2021.11.0, GCC 13.3.

The branch itself holds up.  The UBSan fixes are correct, the `constexpr`
conversion is complete (no `static const` class member with an in-class
initializer remains), and the modulus work closes the polynomial-input and
streaming-interface paths.  What follows is what it did not reach.

## What belongs on this branch

**No source changes.**  `release-todo` carries this document and the tooling
that supports it -- `bench.py`, `BENCH-RUNBOOK.md`, `bench-variants.patch` --
and nothing under `src/`.  Every fix goes on its own branch off `master` and
into its own PR, the way items 1 through 9 did.

This is written down because it was got wrong once.  Item 10's measurements
came with a rewrite of three comments in `F4MatrixReducer.cpp`, committed here
rather than on a branch of its own.  The eventual PR (#77) took a smaller and
different approach, so the branch was left carrying a competing version of a
file it had no business touching, which would have conflicted on the next merge
from `master` for no good reason.  That commit has been reverted.

A change that seems too small for its own branch is exactly the one to watch:
the comment rewrite looked like part of writing up item 10, and it was not.

---

## Status at a glance

| | item | |
|---|---|---|
| 1 | F4 modulus reported as a user error, not an internal one | **DONE** — PR #69 |
| 2 | `MATHICGB_DEBUG` reaches every target that uses our headers | **DONE** — PR #70 |
| 3 | man page installed outside `man1`, where `man` can't find it | **DONE** — PR #71 |
| 4 | cmake build sets no soversion | **DONE** — PR #71 |
| 5 | pkg-config: missing from cmake, wrong in autotools | **DONE** — PR #72 |
| 6 | man page content eleven years out of date | **DONE** — PR #73 |
| 7 | `mgb matrix`'s unvalidated modulus | **DONE** — PR #74 |
| 8 | three wrong or inconsistent strings in `src/cli`'s help text | **DONE** — PR #75 |
| 9 | comments that mislead: `isPrime`'s precondition, obsolete compilers, stale URLs | **DONE** — PR #76 |
| 10 | performance claims nobody has checked this decade: TBB default, F4 inner loop | **DONE** — PR #77, with the dead macros it uncovered; the TBB half became item 21 |
| 11 | remove `build/setup/make-Makefile.sh` | **DONE** — PR #78 |
| 12 | remove `build/vs12` | **DONE** — PR #78, which also flattened `build/autotools` away |
| 13 | autotools `--enable-debug` | **DONE** — PR #79 |
| 14 | expand the CI matrix | **DONE** — PR #80 |
| 15 | hand-written atomics, live on GCC since 2013 | **DONE** — PR #81 |
| 16 | `QuadMatrix::read` reads three of four submatrices only in Debug | **DONE** — PR #82 |
| 17 | `SparseMatrix::read` truncates the file's modulus to 16 bits | **DONE** — PR #83 |
| 18 | where input validation belongs, and asserts that outrank their throws | **DONE** — PR #85, all three parts |
| 19 | `mgb gb` advertises `-monomialTable`, which only `sig` reads | **DONE** — PR #86 |
| 20 | `total compute time` reports CPU time as if it were elapsed | **DONE** — PR #87 |
| 21 | the default thread count uses every core, but nothing scales past four | open — blocked on a 192-core sweep, see below |
| 22 | dead store in `setSPairGroupSize`, in two files | open |
| 23 | a UBSan job, and the 325 misaligned-access reports behind it | open |
| 24 | 19 clang warnings in `mathicgb.cpp`, visible only since the matrix grew | **DONE** — PR #85, with item 18's part 2 |
| 25 | the `Pimpl` pointers are raw: a reachable leak and an unreachable double free | open |
| 26 | `SigPolyBasis` takes a monomial table code it has never used | open |
| 27 | the S-pair queue choice is dead, and only mathic can bring it back | open — **do last**, the one item needing a mathic change |
| 28 | `mgb sig` reports its S-pair queue type as `todo` | open |

---

## [DONE] 1. The F4 modulus check was fixed in one place and left in three

PR #69, merged as d16300e.

PR #66 removed `MATHICGB_ASSERT_NO_ASSUME(false)` from the modulus check in
`mathicgb.cpp`, because asserting on caller input made a Debug build abort
where a Release build throws.  Three sites with exactly that shape survive:

- `src/mathicgb/F4MatrixBuilder2.cpp:282`
- `src/mathicgb/F4MatrixBuilder.cpp:67`
- `src/mathicgb/F4MatrixReducer.cpp:787`

Each asserts that the characteristic fits in `SparseMatrix::Scalar` and then
reports an error.  Confirmed with a 17-bit prime:

```
$ mgb gb big -reducer 26          # p = 65537, Release
INTERNAL ERROR: F4MatrixBuilder2: too large characteristic.

$ mgb gb big -reducer 26          # p = 65537, Debug
mgb: F4MatrixBuilder2.cpp:282: Assertion `ring().charac() <= maxScalar' failed.
Aborted (core dumped)
```

Two distinct problems:

- **Debug aborts on valid input.**  This is the defect PR #66 describes, in
  three more places.
- **Release calls it an internal error.**  `mgb gb <file> -reducer 26` above
  2^16 is ordinary user input, and the program answers by asking the user to
  report a MathicGB bug.  PR #66 gave the library interface a real message for
  this case ("too large for the matrix (F4) reducer ... Use the classic
  reducer"); the CLI, which is the interface most users touch, never got it.

The classic reducers now compute correctly from the CLI at 101, 10000019,
2147483647 and 4294967291, so the fix is to say which reducer to use rather
than to reject the modulus.

## [DONE] 2. cmake Debug builds carry the ODR mismatch PR #68 just fixed

PR #70, merged.  The `MATHICGB_DEBUG`
half is fixed; the `TBB_USE_DEBUG` half was investigated and dropped, see below.

PR #68 stopped defining `MEMT_DEBUG`/`MATHIC_DEBUG` for our translation units
only, because that changes the layout of types shared with libraries built
without them.  Two instances of the same shape remain in a Debug cmake build:

```
d/src/CMakeFiles/mathicgb.dir/flags.make:  CXX_DEFINES = -DTBB_USE_DEBUG
                                           (plus -DMATHICGB_DEBUG via add_compile_options)
d/CMakeFiles/mgb.dir/flags.make:           CXX_DEFINES = -DTBB_USE_DEBUG
```

- **`MATHICGB_DEBUG` does not reach `mgb`.**  `add_compile_options(-DMATHICGB_DEBUG)`
  is in `src/CMakeLists.txt:2`, but `mgb` is added at the top level *after*
  `add_subdirectory(src)`, so it never sees the flag.  `MonoMonoid.hpp` alone
  has 170 `MATHICGB_ASSERT`s in inline and template code linked into both
  binaries; the linker merges those definitions by name, so which assertions
  actually run is arbitrary.  It also means `mgb`'s own 5 asserts never fire in
  any build.
- ~~**`TBB_USE_DEBUG` is set against a release libtbb.**~~  **Retracted.**  I
  grouped this with the above and should not have.  It comes from oneTBB's own
  export (`$<$<CONFIG:DEBUG>:TBB_USE_DEBUG>` in `TBBTargets.cmake`), and in
  `_config.h` it only sets `TBB_USE_ASSERT` and `TBB_USE_PROFILING_TOOLS`.
  Every use in the headers is an assertion inside a function body -- no data
  member is added and no type changes size, which is exactly what made
  `MEMT_DEBUG` (`Arena` 32 -> 56 bytes) and `MATHIC_DEBUG` ABI problems.
  `tbb::detail::r1::assertion_failure` is exported by the release
  `libtbb.so.12`, so the assertions link.  It is a formal ODR difference
  confined to inline assertions, with no ABI mismatch, and overriding a
  dependency's own exported interface needs a better reason than that.

## [DONE] 3. The man page is installed where `man` cannot find it

PR #71, merged, together with section 4 -- both were cmake install rules that
had diverged from autotools.

```
autotools:  dist_man_MANS = doc/mgb.1            ->  share/man/man1/mgb.1
cmake:      install(FILES doc/mgb.1 TYPE MAN)    ->  share/man/mgb.1
```

Verified in a real install tree.  `TYPE MAN` resolves to
`CMAKE_INSTALL_MANDIR` with no section subdirectory; automake infers the
section from the `.1` suffix and cmake does not.  `Macaulay2/homebrew-tap`
builds mathicgb with cmake (`depends_on "cmake" => :build`), so `man mgb` is
broken for every Homebrew install right now.  `CMakeLists.txt:59`.

## [DONE] 4. The cmake build sets no soversion

PR #71, merged, together with section 3.

```
autotools:  -version-info 0:4:0        ->  libmathicgb.so.0
cmake:      -Wl,-soname,libmathicgb.so     (verified in link.txt)
```

Honest assessment of who this affects:

- Debian packages from autotools -- unaffected.
- Homebrew builds with cmake but passes only `std_cmake_args`, which does not
  set `BUILD_SHARED_LIBS`, so it ships `libmathicgb.a` -- unaffected.
- **Macaulay2/M2#4319** ("Build shared library libM2-engine", mkoeppe) is the
  reason `BUILD_SHARED_LIBS` works at all: our PR #56 exists solely to unblock
  it.  That is the one consumer.  It is **not** in flight, though -- as of
  2026-08-29 it is a draft, conflicting, and untouched since 2026-05-28.  I
  earlier called it in flight, which overstated the urgency here.

So the argument is about timing, not present breakage.  Nothing has shipped a
shared `libmathicgb` yet, so the soname is unclaimed and setting it costs
nothing; once #4319 lands and M2 links `libmathicgb.so`, moving to
`libmathicgb.so.0` becomes an ABI break for M2.

What the unversioned soname costs M2: `libmathicgb.so` is both the linker name
and the runtime name, so there is no `.so.0`, no side-by-side install, and no
way for the loader to reject an incompatible rebuild -- `ldd libM2-engine.so`
records `libmathicgb.so` and any file by that name satisfies it.  That is the
failure PR #68 wrote its commit message about, arriving as a segfault instead
of a load-time error.

It is an omission rather than a decision: `MATHICGB_SO_VERSION 0:4:0` already
exists at `configure.ac:69` and cmake simply never reads it, which also means
the two drift silently -- the release PR bumps configure.ac and cmake stays
unversioned forever.  Fix is two lines beside the `PUBLIC_HEADER` property,
plus a line in the "also update version in CMakeLists.txt" note.

## [DONE] 5. The pkg-config file was missing from cmake and wrong in autotools

PR #72, merged, in seven commits.  `mathicgb.pc` named only memtailor and
mathic, so a static link through pkg-config failed with 359 undefined
references; the cmake build produced no `.pc` at all.  Both now generate a
byte-identical, working file.

Found along the way and fixed in the same PR:

- *The librt check has never worked.*  `$oldLibs` vs `oldLIBS` emptied `LIBS`
  rather than restoring it, so the retry dropped `$TBB_LIBS` and failed for
  want of TBB rather than librt -- answering "yes" on every machine with TBB
  for thirteen years.  Deleted rather than repaired: oneTBB's `tick_count.h`
  is pure `std::chrono` and glibc moved `clock_gettime` into libc in 2.17.
- *The cmake install rules ignored GNUInstallDirs.*  Three destinations were
  literals, so a static and a shared build disagreed about where the library
  goes, and `mathicgb.h` was installed apart from the headers it includes.
  This corrects my earlier reasoning for deferring the hardcoded
  `lib`/`include` destinations -- the archive already moved with
  `CMAKE_INSTALL_LIBDIR`, so it was never a settled packager decision, just
  an inconsistency.

Not closed: a consumer who does not use pkg-config still gets `mtbb.hpp`'s
TBB branch whatever the library was built with.  That needs a generated
config header.  `Libs.private` is empty from cmake by choice -- the libatomic
probe exists for m68k and powerpc, which are built with autotools.

## [DONE] 6. The man page content was eleven years out of date

PR #73, merged.  Rewritten by hand rather than generated: `help2man` works but yields only the
fourteen-line action list, since the options live behind per-action help and
it can invoke just one help command.  The new page covers all five actions,
the file formats and the project-name convention, and points at
`mgb help <action>` for options rather than duplicating the part that
actually changes between releases.

Original finding: it was dated April 2015 and documented four
actions -- `gb`, `help`, `matrix`, `siggb` -- but `mgb help` now lists five.
`version`, added in PR #51, is missing.  It also documents no *options* at
all, while `mgb help gb` lists around twenty (`-reducer`, `-outputResult`,
`-module`, `-sPairGroupSize`, `-preferSparseReducers`, ...).  This is
user-visible: both build systems install the page, and PR #71 fixed the cmake
build so `man mgb` can now actually find it.

## [DONE] 7. `mgb matrix` was the last unvalidated modulus entry point

PR #74, open.  A `checkModulus` helper on both read paths in `MatrixAction`,
using `isPrime` and `mathic::reportError` the way `GroebnerConfiguration` and
`MathicIO::readBaseField` already do for polynomial input.

Original finding: `src/cli/MatrixAction.cpp:63` read it straight out of the
file -- `SparseMatrix::Scalar modulus = 0; // will be changed below to a
(hopefully) prime number`.

Two things I had wrong.  I called the consequence the SIGFPE case PR #66
documents; that is only half of it.  `PrimeField::inverse` is the extended
Euclidean algorithm, so the fault needs a pivot sharing a factor with the
modulus.  Sweeping every modulus from 0 to 120 through a stored `cyclic5`
matrix never reached it -- all 121 quietly reduced in a non-field and exited
0.  The crash takes a matrix built to provoke it; the silent wrong answer is
the common case, and it is the worse of the two.

And I said it needs a hand-made binary file, which made it sound exotic
enough to be low priority.  It does not.  `.qmat` files are ordinary output
of `mgb gb -storeMatrices`, which is how the F4 reducer's own regression
workflow feeds `mgb matrix`; changing one field of one of them is enough.

Found along the way and fixed in the same PR: `CFile::close` called `fclose`
but never zeroed `mFile`, so `~CFile` closed the file a second time and
`mgb matrix` aborted on every `.qmat` file it was given -- including the ones
`mgb gb -storeMatrices` had just written.  That is the bug 435edb1 set out to
fix in 2013 from a diagnosis by Christian Eder: it replaced a raw
`fclose(file.handle())` with `file.close()`, moving the second `fclose` inside
the class rather than removing it.  The `.qmat` half of the matrix action has
therefore never run to completion.  It now carries the suite's first
filesystem-touching test.

Fixing it is what made 16 visible.

## [DONE] 8. Three wrong or inconsistent strings in the tool's own help text

PR #75, open.  Two of these were left over from the man page work and one
turned up while fixing 7.

The `-reducer` fix went further than the item asked for, and should have:
writing 21 into `GBCommonParams` would have been correct today and free to
drift again tomorrow, so the fall-through is now named `Reducer_Default` in
the enum and both `reducerType`'s `default:` and the command line parameter
use that symbol.  One line in the library buys an invariant instead of a
correction.

The spelling fix covers only what the tool prints.  The library's own four
"Grobner" strings are still there -- see the note below -- so `mgb` can still
print both spellings in one session, just not in one help screen.

Original finding:

*`-reducer`'s advertised default is not a reducer.*  `GBCommonParams.cpp:70`
passes `4` as the parameter default, so `mgb help gb` prints
`-reducer INTEGER   (default is 4)` above a table listing only 7 to 26.
`Reducer::reducerType` (`Reducer.cpp:113`) ends `default: return
Reducer_Geobucket_Hashed`, which is case 21, so 4 quietly means 21 -- as does
every other value outside the table.  The fix is to make the parameter default
21, so the help states what runs.  Changing the fall-through instead would
alter behaviour, which is not what this item is about.

*"Grobner" and "Groebner".*  Inconsistent inside `src/cli` itself, not just
against the man page: `GBAction.cpp` has two occurrences of "Grobner" and
`SigGBAction.cpp` three, while `GBCommonParams.cpp:24` already writes
"Groebner".  The new man page uses "Groebner", so that is the spelling to
settle on.

*`-storeMatrices` promises a file name it does not write.*
`src/cli/GBAction.cpp:43` says the matrices are stored "in files named
X-1.mat, X-2.mat and so on", while `F4Reducer::saveMatrix` writes `X-1.qmat`
(`F4Reducer.cpp:338`).  `mgb matrix` registers `.qmat`, `.brmat` and
`.rbrmat`, so an unregistered `.mat` is taken for part of the project name and
the real extension appended to it:

```
$ mgb matrix cyclic5-1.mat
ERROR: Could not open file cyclic5-1.mat.qmat in mode rb.
```

## [DONE] 9. Comments that describe a world that no longer exists

Merged with what was 16.  Both halves are the same defect -- a comment that
tells the reader something that is not true any more, or was never quite
true -- and they want the same judgement applied per comment, so they travel
together.

### The one that is done: `mgb::isPrime`'s precondition

PR #76.  The comment now reads
"Returns true if n is prime and n < 2^32", which states the range as part of
what the function returns rather than as a hedge about accuracy.

Original finding: it is `inline` at namespace scope in the installed
`PrimeField.hpp`; above 2^32 `modularPower` overflows, guarded only by a
Debug assert.  The comment said "Exact for every n up to 2^32" but did not
say it is wrong past that.

"Silently returns garbage" was wrong, and worth correcting because it points
at the wrong risk.  Measured against a 128-bit Miller-Rabin, `isPrime` above
2^32 returns *false*, always: of two million consecutive values above the
bound it called all 90093 primes composite and misreported no composite as
prime, and across three million random values from the rest of the 64-bit
range plus twenty thousand primes drawn from it, it never once returned true.
So the failure is a false negative, never a false positive -- a caller
validating a modulus rejects a legitimate large prime rather than accepting a
composite.  Not proven, but 5 million samples without a counterexample.

Narrowing the parameter, the other option I offered, is the worse of the two.
C++ would silently truncate a `uint64` argument to `uint32`, so
`isPrime(2^32 + 7)` becomes `isPrime(7)` and returns true -- inventing the
false positive that does not currently exist -- unless a deleted `uint64`
overload is added alongside.  It would also break `MathicIO.hpp:187`, which
casts to `uint64` explicitly, and any external caller's source.

Context that makes documenting the right scope: the 2^32 ceiling is the whole
of `PrimeField`, not an `isPrime` quirk.  `ModularProdType<uint64>` maps to
`uint64` under a `@todo` conceding "64 bits is not enough to store a 64 bit
product", and `modularPower` already documents the same bound.  `isPrime` was
the one public entry point stating its range without stating what lay past
it.  Making it correct for all of `uint64` would need `__int128`, a compiler
extension, and would leave one function serving a range the rest of the file
cannot represent -- see 17, which is the same ceiling seen from the matrix
reader.

### The rest of the pass

Scattered through the source are notes about compilers no longer in use, kept
verbatim since 2013.  They are harmless individually but collectively they
misdirect: a reader cannot tell which constraints still bind.  Each wanted a
different answer, which is why the item asked for judgement per comment
rather than a sweep:

- `mtbb.hpp:273` -- "This really should be `std::chrono::steady_clock`, but
  GCC 4.5.3 doesn't have that."  **Done**, 5c9b31b: did what it asked.
  `system_clock` is wall time and can be stepped, and every "Time/s (real)"
  figure `mgb` prints came from it.  With TBB, `mtbb::tick_count` is
  `tbb::tick_count`, which already used a steady clock, so only the fallback
  measured with a clock that can move.
- `stdinc.h:311` -- an OpenMP 3 caveat qualified by what "MSVC 2012 only
  supports".  **Done**, 5304055: `OMPIndex` was dead, not merely stale.
  f6010fc took the last `#pragma omp` out in November 2012 and the typedef
  outlived it by thirteen years, so both it and the comment went.
- `ModuleMonoSet.cpp:71` -- "workaround for gcc 4.5.3 issue".  **Done**,
  a214516: comment only.  The line it annotates is worth keeping -- and the
  `this->` is load-bearing for a reason the comment never mentions, since the
  local is named `monoid` and is in scope inside its own initializer.  A
  reader who believed the comment would have removed exactly the wrong thing.
- `Range.hpp:409` -- "That worked fine on gcc 4.7.3.  It did not compile on
  MSVC 2012", explaining a workaround that may no longer be needed.
  **Done**, fab6684: it is not needed.  The comment says inlining "would
  inded make more sense", so verify and do it -- GCC 13.3 and clang 18.1.3
  both compile the inlined form and pass, and MSVC 2012 cannot build this
  code at all since PR #60.  Thirteen lines for one.
- `F4MatrixReducer.cpp:131,147,156` -- three performance claims measured on
  **MSVC 2012**, one of which explains why a Duff's-device unrolling was
  rejected.  **Moved to 10.**  These are not wrong about a compiler, they are
  unverifiable claims about speed guarding code that looks gratuitously ugly
  without them; settling them means measuring, which belongs with the other
  performance question rather than in a comment pass.

The URLs: **done**, 99c4113.  `mathicgb.pc.in` and `README.md` pointed at
`github.com/broune`.  The pc file's is the one that travels -- it is what
`pkg-config --variable=URL` reports and what a packager copies into a package
description.  `README.md`'s copyright line keeps its `broune.com` address,
which is attribution rather than a project pointer.  ce090ac then fixed the
README's first sentence, which read "computing Groebner basis and signature
Grobner bases" -- singular where it meant plural, and both spellings of the
name inside one sentence.

`doc/description.txt` was left alone on purpose.  Its `broune` link is to
`make-Makefile.sh`, one of the four references 11 will delete along with the
script, and the rest of that file's staleness is 12's "Installation for
Visual Studio" section.  Repointing URLs that are about to be deleted is
churn; 11 and 12 should remove them instead.

Verified across the three configurations these touch: GCC 13.3 with TBB,
GCC 13.3 with `-Dwith_tbb=OFF` (the only build that compiles the
`mtbb::tick_count` this changes), and clang 18.1.3.  245 tests in 28 suites
pass in each.

## [DONE] 10. Performance claims nobody has checked this decade

PR #77, merged as b84cbf4, 8166d85 and 45872b6.  That closes the F4 inner-loop
half; the TBB half turned into item 21 and is still open.

Measured 2026-08-31 on two machines: an i5-6300U (2 physical cores + HT,
`powersave`, GCC 11.4, x86-64) and an Apple M5 Pro (6 performance cores + 12
efficiency cores, clang, arm64).  The three
`F4MatrixReducer::addRowMultiple` claims now have numbers.  PR #77 did not
rewrite the comments to match, as the first attempt at this item did; it deleted
the `restrict` local that the false claim existed to justify, and the claim went
with it.  The TBB question is sized, and turned out to be a different problem
from the one filed: not a wall-clock regression on one pathological input, but a
parallel ceiling of 1.2x to 1.8x that applies to everything measured, against a
default that uses every core.  Capping the default belongs in its own patch.

Both remaining loose ends are closed: the recorded `yang1` baseline is shown
below not to have been a full run, and `hilbertkunz1` is confirmed too small to
discriminate between variants.

### The harness

`bench.py` at the top of the tree.  It runs every (binary, input) cell once per
round in round-robin order, so that machine drift is spread across all cells
instead of landing on whichever variant was measured last, and reports min,
median, mean and standard deviation of user CPU and wall time.

Interleaving turned out to be the whole game.  This machine is an i5-6300U --
two physical cores plus hyperthreading, `powersave` governor -- and sequential
measurements of the same binary minutes apart differ by up to 40%: one
`yang1 -breakAfter 4750` run timed at 46 s against 32.5 s for the same binary
inside the harness.  **Only comparisons within a single interleaved run are
valid.**  Two mistakes were made and caught by re-running: reading a faster
base time in a later run as a 9% win for `-falign-loops`, when nothing had
been interleaved to support it.

The variants were built from a `git archive` of HEAD with a
`MATHICGB_BENCH_VARIANT` bitmask over `addRowMultiple` (bit 0: member
`mEntries` instead of the local `restrict` pointer; bit 1: plain loop instead
of the 2x unrolling; bit 2: Duff's-device goto).  All variants produced
byte-identical `.gb` output and distinct object files, so the compiler was not
collapsing them.

Note for anyone repeating this: `mgb gb` defaults to `-reducer 21`, the
*classic* reducer, so every F4 measurement needs an explicit `-reducer 26`, and
the project name has to come before the flags.  Plain `mgb gb
hyclic8-101-trimmed` takes 78 s here and says nothing about F4 or TBB.

### The three `addRowMultiple` claims

GCC 11.4 `-O2`, serial, as a change in whole-computation user CPU against the
shipped code.  The two columns are plain `-O2` and `-O2` plus
`-falign-loops=32 -falign-functions=64 -falign-jumps=32`, which perturbs code
layout and nothing else.

On clang 21 / arm64 the first of the three needs no timing at all: variants 0
and 1 compile to **byte-identical object files**, so the compiler emits the
same code with and without the restrict local.  Where the object files match
there is nothing to measure, and that is a firmer answer than any timing run.
GCC emits *different* code for the same two, of identical size, timing within
+-1.4%.  So
one compiler acts on the aliasing hint and gains nothing by it, and the other
ignores it outright.

| variant | hyclic8 -O2 | hyclic8 +align | yang1 -O2 | yang1 +align |
|---|---|---|---|---|
| `mEntries` instead of local | +0.3% | +1.4% | +0.0% | -0.9% |
| plain loop, no unrolling | +8.2% | +9.9% | -0.6% | -2.5% |
| Duff's-device goto | +7.6% | +22.5% | -10.8% | -3.3% |

- **The 14% claim at `:131` is dead.**  Eight measurements across three inputs
  and two flag sets, every one inside +-1.4%, which is under the run-to-run
  noise on the hyclic8 cells.  It was an MSVC 2012 register-allocation quirk,
  and its own author wrote "that does not make sense to me."  The local is kept
  as an aliasing hint -- that is what `MATHICGB_RESTRICT` is for -- but the
  comment no longer claims it buys anything, and says plainly that replacing it
  is fine.
- **The unrolling claim at `:147` holds, but not as a flat 5%.**  It is worth
  8-12% on hyclic8 and slightly *negative* on yang1.  The comment now gives the
  per-input table, so the caution survives without implying a number that only
  applies to one shape of matrix.
- **The Duff's-device claim at `:156` reaches the right conclusion for the
  wrong reason.**  That variant is 8-23% slower on hyclic8 but 2-11% *faster*
  on yang1.  Both figures move by a factor of four under the alignment flags
  alone, so most of what it measures is where the compiler puts the loop rather
  than the branch structure.  Still not worth a hyclic8 regression to chase.

`hilbertkunz1` was measured too and every variant landed within +-1.1% of base
on it; it is too small to discriminate and is not worth carrying.

### The TBB default

Measured on two machines three hardware generations apart, which turns out to
matter: an i5-6300U (2 physical cores + HT, GCC 11.4, x86-64) and an Apple M5
Pro (6 performance cores + 12 efficiency cores, clang 21, arm64).  Full runs,
`-reducer 26`.

The complaint in PR #65 is real, but it is not the complaint that was filed.
Enabling TBB does not make anything slower in wall-clock terms on current
hardware.  It burns two to six times the CPU to shave a little off the wall.

M5 Pro, `yang1`, sweeping `-threadCount`:

| threads | wall | speedup | user+sys | sys |
|---|---|---|---|---|
| 1 | 43.76 s | 1.00x | 42.98 s | 0.42 s |
| 2 | 37.16 s | **1.18x** | 45.65 s | 1.86 s |
| 4 | 39.64 s | 1.10x | 53.42 s | 6.91 s |
| 8 | 38.42 s | 1.14x | 61.79 s | 16.05 s |
| 12 | 38.00 s | 1.15x | 69.81 s | 24.04 s |
| 16 | 38.65 s | 1.13x | 81.70 s | 35.12 s |

M5 Pro, `hyclic8-101-trimmed`, the input that was supposed to be the control
that scales:

| threads | wall | speedup | user+sys | sys |
|---|---|---|---|---|
| 1 | 0.77 s | 1.00x | 0.76 s | 0.01 s |
| 2 | 0.50 s | 1.54x | 0.84 s | 0.05 s |
| 4 | 0.44 s | **1.75x** | 1.21 s | 0.29 s |
| 8 | 0.47 s | 1.64x | 2.28 s | 1.14 s |
| 12 | 0.48 s | 1.60x | 3.42 s | 2.06 s |
| 16 | 0.48 s | 1.60x | 4.43 s | 2.89 s |

**Both inputs saturate and then degrade.**  `hyclic8` peaks at 1.75x on four
threads and gets worse above that; `yang1` peaks at 1.18x on two.  Neither
comes near six performance cores, let alone eighteen threads.  Past the peak
every added thread costs `sys` time roughly linearly and buys nothing.

The framing this review started with -- that `yang1` is a pathological input
that fails to parallelize -- is wrong.  `yang1` is the worse case of a ceiling
that applies to everything measured.  The corroborating number: the i5-6300U
reaches 1.79x on `hyclic8`, which is the *same ceiling* the M5 Pro hits at four
threads.  Two machines, two compilers, two architectures, same wall.  That is
the algorithm, not the hardware and not the TBB version.

What ships is `-threadCount 0`, which means use every core, so every user lands
on the far right of both tables:

| | wall | user | sys | total CPU |
|---|---|---|---|---|
| `yang1`, `--with-tbb=no` | 42.30 s | 42.00 s | 0.24 s | 42.24 s |
| `yang1`, TBB, default | 38.23 s | 46.62 s | 39.09 s | **85.71 s** |
| `hyclic8`, 1 thread | 0.77 s | 0.75 s | 0.01 s | 0.76 s |
| `hyclic8`, TBB, default | 0.47 s | 1.64 s | 3.29 s | **4.93 s** |

1.11x wall for 2.03x the CPU on `yang1`; 1.64x wall for 6.5x the CPU on
`hyclic8`.  `sys` goes from 0.24 s to 39.09 s, a factor of 163.  Essentially
all of the added cost is synchronization, which is what the structure predicts:
`reduceToEchelonForm` at `F4MatrixReducer.cpp:439-511` puts its `parallel_for`
inside a `while` loop, with a full join and a global `mtbb::mutex` on every
iteration, and `F4MatrixBuilder.cpp:153` has the same shape.

On a laptop that is battery and fan noise.  On a shared machine or a CI runner
it is two to six times the load for a marginal gain.  Worth the release note,
and worth a follow-up: **the fix is to cap the default thread count, not to
turn TBB off.**  That follow-up is item 21.

One measurement trap, which is likely how a 50% regression came to be recorded
in the first place.  What mgb prints as "total compute time" is **user + sys,
not wall**.  Every row above matches to three digits -- at 16 threads,
46.58 + 35.12 = 81.70 against a reported 81.699.  So the tool reports a
multithreaded run as getting monotonically slower while its wall time is flat.
Anyone timing mgb by reading its own output, rather than by wrapping it in
`time`, will see a large regression that does not exist.  That is worth fixing
on its own; at minimum the label is wrong.

### Resolved: the recorded yang1 baseline is not a full run

PR #65 records 14.2-14.4 s serial for `yang1`.  Calibrating three machines on
the classic reducer, which is ordinary single-threaded work, against the full
serial F4 run:

| | classic `yang1` | F4 `yang1`, full serial |
|---|---|---|
| i5-6300U | 5.07 s | 698 s |
| M5 Pro | 1.01 s (5.0x faster) | 42.3 s (16.5x faster) |
| 12-core, as recorded | 2.24 s (2.26x faster) | **14.2 s** |

Two things follow.

The F4 tail is memory-bandwidth-bound.  The i5-6300U is 5.0x slower than the
M5 Pro on ordinary work but 16.5x slower on the full F4 run -- a 3.3x excess
that tracks bandwidth rather than clock, and explains why that machine spends
650 s of its 698 s on the last eleven basis elements while the M5 Pro does the
whole computation in 42 s.  The tail itself is real everywhere; only its cost
is hardware-sensitive.

And the recorded 14.2 s cannot be a complete computation.  The 12-core machine
is 2.2x *slower* than the M5 Pro at ordinary single-threaded work, so a full
serial `yang1` F4 there should take on the order of 93 s, and not less than
42 s even granting it the M5 Pro's per-core bandwidth.  14.2 s is 6.5x faster
than the most generous estimate.  Whatever was measured, it was not this
computation -- most likely a run that stopped before the final stage.

So PR #65's yang1 figures should not be used as a baseline, and the 50% wall
regression attributed to TBB is not reproducible on either machine here.  What
is reproducible, and what the release note should say, is the CPU cost above.

For the record, the full computation is 4,761 basis elements, 54,324 terms and
11,331,180 S-pairs considered, identical on both machines and at every thread
count -- so none of the above is comparing different amounts of work.

### The dead macros behind the claim

Removing the `restrict` local left `MATHICGB_RESTRICT` with no uses at all, and
it went in the same PR, as 8166d85.  Auditing its neighbours in the same three
compiler blocks, on 2026-08-31, found five more with no uses either -- removed
as 45872b6:

| macro | uses anywhere in `src/` |
|---|---|
| `MATHICGB_ASSUME_AND_MAY_EVALUATE` | 0 |
| `MATHICGB_MUST_CHECK_RETURN_VALUE` | 0 |
| `MATHICGB_NOTHROW` | 0 |
| `MATHICGB_PURE` | 0 |
| `MATHICGB_RETURN_NO_ALIAS` | 0 |

Each is defined three times, once per compiler branch, so that is 15 lines.

The part that makes this worth doing rather than merely tidy: **in the
GCC/clang branch, all five are syntactically invalid.**  Four are written
`__attribute__(x)` where the attribute syntax needs `__attribute__((x))`:

    #define MATHICGB_RETURN_NO_ALIAS         __attribute__(malloc)
    #define MATHICGB_NOTHROW                 __attribute__(nothrow)
    #define MATHICGB_PURE                    __attribute__(pure)
    #define MATHICGB_MUST_CHECK_RETURN_VALUE __attribute__(warn_unused_result)

and the fifth has mismatched braces -- the `while(0)` is inside the `do`
block rather than closing it:

    #define MATHICGB_ASSUME_AND_MAY_EVALUATE(X) \
      do {if(!(X)){MATHICGB_UNREACHABLE;}while(0)}

Verified by expanding each one in a one-line translation unit against GCC
11.4.  `MATHICGB_PURE` gives `error: expected '(' before 'pure'`, its three
siblings give the same shape, and `MATHICGB_ASSUME_AND_MAY_EVALUATE` gives
`error: expected primary-expression before '}' token`.  So the first use of
any of them, on the compiler everyone actually builds with, is a build
failure.  They have been that way since 2013 and nothing noticed, because
nothing uses them.

The MSVC branch spellings look right (`__declspec(noalias)` and friends) and
the fallback branch defines them empty, which is harmless.  It is specifically
the branch every current build takes that is broken.

Deleting all five is the obvious call -- a macro that cannot compile is not a
facility anyone can adopt, and if one is wanted later it is two lines to add
correctly.  `stdinc.h` is installed, so this is nominally an API removal, with
no ABI effect.

Two near neighbours are **not** dead and should be left alone.  A naive grep
for uses outside `stdinc.h` reports zero for both:

- `MATHICGB_ASSUME` is used at `stdinc.h:137`, where it *is* the non-debug
  definition of `MATHICGB_ASSERT`.  Removing it would break every one of the
  898 assertions in release builds.
- `MATHICGB_CONCATENATE` is used at `stdinc.h:159` by
  `MATHICGB_CONCATENATE_AFTER_EXPANSION`, which `MATHICGB_UNIQUE` needs.

This section also proposed a general pass over `stdinc.h`, on the grounds that
the MSVC branch it is half made of cannot be reached.  Section 12 settled that
the other way when it removed `build/vs12`: the `#ifdef _MSC_VER` branch is the
ordinary shape of a compiler-abstraction header, is paired with a `__GNUC__`
one, and stays.  Nothing further is planned here.

### Found along the way

Three defects surfaced while measuring, none of them what this item set out to
look at, and all three are worth more than the comment rewrite that item 10
actually asked for.  They are written up as items 20, 21 and 22.

## [DONE] 11. Remove build/setup/make-Makefile.sh

PR #78, commit 4967c4e; open at the time of writing.

Reviewed 2026-08-30 with section 12.  `build/autotools` is fine -- two
`.gitignore`s and `ax_cxx_compile_stdcxx.m4`, which `configure.ac` calls for
the C++17 requirement.  (It is fine but not well placed; it moved to `m4/` and
`build-aux/` in the same PR, see the end of section 12.)  The other two
subdirectories are unreferenced by either build system and cannot work.

A 2013 developer script that cloned and built memtailor, mathic and mathicgb
together.  Last substantive commit 2013-09-24, and broken three ways over:

- It clones from `https://github.com/broune/{memtailor,mathic,mathicgb}.git`,
  the pre-Macaulay2 URLs.
- It downloads gtest 1.6.0 from `http://googletest.googlecode.com/files/`.
  Google Code shut down in 2016, so the `gtest` target -- which every other
  target depends on -- cannot run at all.
- It defines `MEMTAILOR_DEBUG`, which is not a macro.  memtailor's is
  `MEMT_DEBUG`, as PR #68 established.  So its assert-enabled targets never
  enabled memtailor's asserts even back when the script worked.

Referenced only by four mentions in `doc/description.txt`, which want updating
in the same commit.

## [DONE] 12. Remove build/vs12

PR #78, commit b254bfa, together with cd80f1d and 25735e1 for the flattening
described at the end of this section; open at the time of writing.

Visual Studio *2012* project files.  Last real change 2013-10-03; the two
commits since are a file-permission fix and the CRLF normalization in f78d6be.

Staleness is not the decisive part -- they cannot work.  `stdinc.h` has
required C++17 since PR #60 and VS2012 does not do complete C++11, so any
build stops at `#error "mathicgb requires C++17 or later"`.  Independently the
file list has been wrong since 2021: `MESClassicGBAlg.cpp`, added in e6996a3,
is missing from `mathicgb-lib.vcxproj`.  `notes.txt` is a 300-line personal
write-up of clicking through the VS2012 GUI, including how to build gtest 1.6
with `_VARIADIC_MAX=10`.

Three things go with it.  `.gitattributes:6` sets `*.sln text eol=crlf`, a rule
that exists solely for `build/vs12/mathicgb.sln` -- there are no other `.sln`,
`.vcxproj` or `.filters` files anywhere in the tree, so it would match nothing.
`.gitignore` has a block of seven MSVC artifact rules, one of which (`output/`)
names vs12's build output directory specifically; the others would still catch
artifacts from an in-tree cmake build with an MSVC generator, but MSVC is not a
supported compiler, so the block goes with the project files it was written for.
And `doc/description.txt` has an entire "Installation for Visual Studio"
section, lines 166-208 plus its table of contents entry at line 6, which
documents these project files and asserts that "at this writing (October 3,
2013) the code compiles in MSVC".

Not to be removed with it: the `#ifdef _MSC_VER` branch in `stdinc.h` that
defines `MATHICGB_INLINE`, `MATHICGB_ASSUME` and friends is the normal shape
of a compiler-abstraction header and is paired with a `__GNUC__` branch, and
`cmake/FindTBB.cmake` is vendored third-party code that carries its own MSVC
logic.  `Atomic.hpp`'s MSVC path is likewise not a Windows leftover -- see
section 15.

Both removals are the same shape as a8035a6 (Remove src/checksource):
unreferenced, unbuildable, bit-rotted past the point where fixing beats
deleting.

### The flattening went into the same PR

This section proposed leaving `build/autotools` alone and flattening it later,
on the grounds that it churns paths in `configure.ac` and `Makefile.am`.  Doug
overruled that: the whole point of the PR is that `build/` should not survive
it, and a directory left holding one tracked file is not a finished cleanup.

`m4/` now holds `ax_cxx_compile_stdcxx.m4` and `build-aux/` holds the scripts
`autoreconf --install` writes, which is the layout autoconf's own manual and
gnulib default to -- not the top level, which would put nine generated files
next to `README.md`.  `build/` is gone.

Two things came out of it that were not in this section:

- `ACLOCAL_AMFLAGS` in `Makefile.am` gave way to `AC_CONFIG_MACRO_DIRS`, which
  aclocal has traced out of `configure.ac` since automake 1.13.  Otherwise the
  macro directory is written twice, in two files, where the copies can drift.
  The cost is that libtoolize 2.4.7 now ends every `autogen.sh` with `Consider
  adding '-I m4' to ACLOCAL_AMFLAGS in Makefile.am.` -- wrong, since the line
  above it in its own output reads `putting macros in AC_CONFIG_MACRO_DIRS,
  'm4'`, but it is new noise.
- `.gitignore` still listed `/build/autotools/mathicgb.pc`, but `configure` has
  generated `mathicgb.pc` at the top level since PR #72.  The built file had
  been showing up as untracked ever since, on both build systems.  Now
  `/mathicgb.pc`.

Verified both ways: `make distcheck` passes, which unpacks the tarball and
builds out of tree against a read-only srcdir, so it exercises the moved
directories from scratch; and the cmake build passes 246/246 with TBB detected.

## [DONE] 13. Give the autotools build a --enable-debug

PR #79, merged as b51204c.  It took the first of the two options below.
`--enable-debug` sets `DEBUG_CFLAGS=-DMATHICGB_DEBUG`, which reaches every
target through `AM_CPPFLAGS` and installed consumers through `mathicgb.pc`,
and sets no compiler flags of its own.

`configure.ac` and `Makefile.am` never mentioned `MATHICGB_DEBUG`, so an
autotools build cannot turn our assertions on at all -- the cmake build is the
only one that can.  That was the reason the CI matrix in section 14 had 12 real cells
rather than 16; it now has 16.

Two ways to close it.  Add `--enable-debug` to `configure.ac`, which is parity
the build system arguably should have anyway, or have CI pass
`./configure CXXFLAGS="-g -DMATHICGB_DEBUG"`.  Prefer the former: the latter
tests a configuration no user can conveniently request, which is a weak test.

Note that `MATHICGB_DEBUG` must reach every target that includes our headers,
not just the library -- that is the bug fixed in item 2, and an autotools
`--enable-debug` has to avoid repeating it for `mgb`.

---

## [DONE] 14. Expand the CI matrix

PR #80, merged as e166dba.  All 16 cells, and all 17 checks green on the
first run.

Reopened 2026-08-29, having been declined earlier in the review.  There are
four axes worth varying:

| axis | what it would have caught |
|---|---|
| cmake vs autotools | PR #65's TBB detection bug was cmake-only; PR #59's `-latomic` was autotools-only |
| debug vs not | PR #68's `Arena` abort and PR #66's assert-abort were both debug-only, both found by hand |
| tbb vs not | `mtbb.hpp`'s hand-rolled `mutex`, `task_arena` and `enumerable_thread_specific` are compiled by nobody today |
| macos vs ubuntu | already in place; catches AppleClang/libc++ and Homebrew paths |

The full cross product is 16 jobs, all of them real since PR #79 gave the
autotools build a `--enable-debug` (see section 13); before that it was 12,
because the autotools build had no debug mode to vary.  Cost is not
the objection -- this is a public repo, Actions minutes are free, and
`fail-fast: false` is already set.  Three things to get right rather than
taking the cross product literally:

- **The current cmake job sets no `CMAKE_BUILD_TYPE` at all**, so it is neither
  Debug nor Release -- no optimisation and no `NDEBUG`.  Adding a debug axis
  means the other cell should become an explicit `Release`, or CI still never
  builds what users ship.
- **`make distcheck` in all four autotools cells is the expensive part.**  It
  configures and builds twice plus a tarball round trip.  Run distcheck in one
  cell (ubuntu, tbb on) and plain `make && make check` in the rest.

  This advice was half wrong, and the reason matters.  distcheck's inner
  configure receives only `$(AM_DISTCHECK_CONFIGURE_FLAGS)` and
  `$(DISTCHECK_CONFIGURE_FLAGS)`, neither of which this project sets, and
  distcheck never builds the outer tree at all -- so in an `--enable-debug`
  or `--without-tbb` cell it would silently test the default configuration
  and the axis would mean nothing.  Not a matter of cost.  It also should
  not be one cell but one *per OS*: the tarball and its install/uninstall
  machinery are where GNU and BSD userland differ, and the old workflow ran
  distcheck on macOS.  PR #80 runs it as its own step in the two
  release-with-TBB cells.
- **The debug axis is the one to prioritise** if this gets phased in.  It is
  where this release cycle's actual bugs lived.

A UBSan job is the natural companion: four of the five most recent commits
before this review were UBSan findings, all found by hand.  Left out of
PR #80 and written up as item 23, because it is a job of a different shape
rather than another cell.

### What the first run settled

All eight configurations that nothing had ever compiled -- macOS with
assertions on, and either OS without TBB -- pass 246/246.  So `mtbb.hpp`'s
hand-rolled `mutex`, `task_arena` and `enumerable_thread_specific` do still
work; they had simply never been checked.

The old autotools jobs ran `configure && make distcheck` and nothing else,
and distcheck does not build the outer tree, so those jobs never compiled
the configuration they had just configured.  Every autotools cell now runs
its own `make && make check`; the two distcheck cells show `PASS: unittest`
twice.

The cmake Debug cells run the suite in about 28 s against 0.5 s for
Release, which is 898 assertions actually executing -- the axis is real and
not nominal.

The jobs also build in parallel now, which they never did.  `MAKEFLAGS` is
set once in the environment, so it reaches the sub-makes distcheck spawns:
`-j4` on the ubuntu runners and `-j3` on the macOS ones, read back out of
the `env:` block GitHub prints for each step.  `getconf _NPROCESSORS_ONLN`
does work on macOS.  Even so the two distcheck cells came in at 154 s and
171 s against 162 s and 272 s for the old distcheck-only jobs.

One trap worth recording.  `${{ matrix.tbb && '' || '--without-tbb' }}` is
the natural way to write a flag that is present only when a boolean is
false, and it is wrong: GitHub's `&&`/`||` is truthiness-based, so the
empty string on the true branch falls through and *every* cell gets the
flag.  Write it as `${{ !matrix.tbb && '--without-tbb' || '' }}`, with the
non-empty value on the taken branch.  Caught by expanding all 16 cells
before pushing, not by CI, which would have been green either way.

## [DONE] 15. Reconsider the hand-written atomics

PR #81, merged as eb2581c and the three commits before it.  It went further
than this section proposed: not just the implementation but `Atomic<T>`
itself, `MATHICGB_USE_FAKE_ATOMIC`, and the `doc/description.txt` section
describing them.  About 440 lines, against the 250 estimated below.

The premise was understated.  `std::atomic` is not merely no worse: compiled
at `-O2` with GCC 13.3 on x86-64 the two emit the same instructions for every
load and for relaxed and release stores, and on the one operation where they
differ -- the sequentially consistent store -- the hand-written version is the
*worse* of the two, `movq` plus `lock cmpxchgq` plus a retry branch against a
single `xchgq`.  Timing could not see it, and did not need to: where the
emitted code is identical there is nothing to time.

Removing the wrapper as well needed its own evidence, since `Atomic<T>` was
not equivalent to `std::atomic<T>`.  Its constructor value-initialized, where
`std::atomic`'s default constructor is trivial in C++17, and that is why
`FixedSizeMonomialMap` nulls its buckets by hand.  Deleting the nulling from
both constructors and rebuilding each way settles what the wrapper was worth:
with it the tests still pass 246/246, without it the same build segfaults, and
valgrind reports the buckets walked as a hash chain in
`F4MatrixBuilder2::findOrCreateColumn` under TBB.  Both constructors now say
why the nulling is required.

One measurement trap, recorded so it is not rediscovered: multithreaded `.gb`
output is not reproducible.  The same binary produced five different hashes on
five consecutive `cyclic7` runs, and identical output at `-threadCount 1`.  A
first comparison of the two builds looked like a discrepancy and was not one.

`src/mathicgb/Atomic.hpp` was 398 lines implementing atomic load and store by
hand: compiler barriers via `__asm__ __volatile__ ("" ::: "memory")`, CPU
fences via `__sync_synchronize()`, and a CAS loop on
`__sync_bool_compare_and_swap` -- GCC's legacy pre-C++11 builtins -- with
`_InterlockedExchange` on the MSVC side.  Last substantive change 2013-08-16.

It is not dead Windows code.  `stdinc.h` defines
`MATHICGB_USE_CUSTOM_ATOMIC_X86_X64` in *both* the `_MSC_VER` and `__GNUC__`
branches, so on GCC x86/x64 -- every ordinary Linux build -- the hand-written
implementation is what `MonomialMap.hpp` and `FixedSizeMonomialMap.h` get,
not `std::atomic`.

Its own comment states the premise to re-test:

> The default is to use `std::atomic` which is a fine choice if `std::atomic`
> is implemented in a reasonable way by the standard library implementation
> you are using. ... it is surprising that any compiler ships with a
> `std::atomic` that is worse than this - but that is very much the case.

That was written in 2013 against GCC 4.x, when it was true.  We now require
C++17 and build with GCC 13.  The premise is measurable: build with and
without `MATHICGB_USE_CUSTOM_ATOMIC_X86_X64` and compare.  If `std::atomic` is
no worse, this deletes roughly 250 lines of hand-rolled memory-ordering code,
which is exactly the category of code that is hard to get right -- and the
kind of thing the UBSan findings in PR #67 suggest is worth a second look.

## [DONE] 16. `QuadMatrix::read` reads three of four submatrices only in Debug

PR #82, merged as 53bad6b.

The Release result turned out to be worse than "differs": it is *empty*.  On
`cyclic5-1.qmat` the Release build writes 20-byte `.brmat` and `.rbrmat` files
against Debug's 228, because only `topLeft` was ever read.  The fixed Release
output is byte-identical to what Debug produced before the fix, so Debug was
the correct side throughout.

`src/test/QuadMatrix.cpp` is the regression test, and it reproduces the bug
rather than merely covering the fix: without the fix it fails in Release and
passes in Debug, which is the defect stated as a test.  Worth recording that
the gap here was never the CI matrix -- the pre-PR #80 cmake job set no
`CMAKE_BUILD_TYPE` at all, so it built this broken path too.  Nothing exercised
`QuadMatrix::read`.

Checked while fixing it: 320c4a5 added exactly two `#ifdef MATHICGB_DEBUG`
blocks.  The other, at `src/test/gb-test.cpp:251`, wraps
`Reducer::ReducerType(reducerType)` -- a pure cast, so nothing is lost when it
compiles out, and it is left alone.  The remaining `#ifdef MATHICGB_DEBUG`
blocks under `src/mathicgb/` wrap asserts and debug-only helpers, not work the
release build needs.

`src/mathicgb/QuadMatrix.cpp:359` wrapped the reads of `topRight`,
`bottomLeft` and `bottomRight` in `#ifdef MATHICGB_DEBUG`, so a Release build
reads `topLeft` and stops.  The other three quadrants stay empty, the
reduction runs on them anyway, and the file offset never advances past the
first submatrix.  Same file, same command, two builds:

```
$ cmp release/cyclic5-1.rbrmat debug/cyclic5-1.rbrmat
differ: byte 1, line 1
```

Both the bottom right matrix and its reduced form differ, so a Release
`mgb matrix` and a Debug one disagree about what a stored matrix reduces to.

It came from 320c4a5 (2022-01-04, "changes so code compiles with only a few
compiler warnings").  Three variables are consumed only by the
`MATHICGB_ASSERT`s below them, so they are unused in Release; making the
reads conditional silences that by deleting the work.  The fix is to read
unconditionally and mark the moduli `[[maybe_unused]]`, which is what the
same commit did in `SparseMatrix::read` for `colCount`.

Reachable only through `mgb matrix`, which aborted before it could show the
difference until PR #74 -- which is why this surfaced now and not in 2022.

## [DONE] 17. `SparseMatrix::read` truncates the file's modulus to 16 bits

PR #83, merged as d255e43 and d55763f.

Worse than "truncates" in practice: two `.brmat` files identical but for the
modulus field, 65637 and 101, reduce to byte-identical output, and the 65637
one is written back out claiming 101.  Silent corruption rather than a misread.
`read` now rejects the file, through the same error path the rest of the
function uses, and `src/test/SparseMatrix.cpp` covers it -- the test fails
without the check.

`src/mathicgb/SparseMatrix.cpp:500` read the modulus with `readOne<uint32>`
and returns it as `SparseMatrix::Scalar`, which is `uint16`.  A file claiming
65637 yields 101, and the primality check PR #74 adds sees only the truncated
value, so it passes and the reduction proceeds over a field the file did not
name.

Narrow in practice: `write` narrows on the way out too, so nothing mgb
produces can trigger it, and entries are `uint16`, so a modulus above 2^16 is
meaningless in this format regardless.  The fix is to reject a modulus that
does not fit rather than silently reinterpret it, inside `read` where the
`uint32` is still intact.

## [DONE] 18. Where input validation belongs, and asserts that outrank their throws

PR #85, four commits: 41dce00, ac4054b, ecd68c4 and 2f12500.

Three findings that are one decision.  Absorbed 2026-09-02 from sections 17
and 23, which each raised part of it.

All three are done, and two of them did not come out the way this section
proposed -- see the notes under each.  It closed item 24 along the way and
turned up item 25.

### The immediate bug: `StaticMonoMap.hpp:431`

Item 1 all over again:

```cpp
  default:
    MATHICGB_ASSERT_NO_ASSUME(false);
    throw std::runtime_error("Unknown code for monomial data structure");
```

PR #69 dealt with three sites of exactly this shape; this one was not in that
list because those three were found by grepping the F4 files.  It is reachable
from ordinary command line input, `-divisorLookup` being validated nowhere
earlier:

```
$ mgb gb cyclic5 -divisorLookup 99     # Release
Unknown code for monomial data structure

$ mgb gb cyclic5 -divisorLookup 99     # Debug
mgb: StaticMonoMap.hpp:431: Assertion `false' failed.
Aborted (core dumped)
```

The Release behaviour is right -- a bad `-divisorLookup` is user error and
throwing is the correct answer.  Only the assert has to go, exactly as in
PR #66's reasoning: a Debug build must not abort on input a Release build
handles.  One line.

**Done differently**, in 41dce00 and ac4054b.  Deleting the assert would have
left the value unchecked until it reached a switch four call levels down, so
the branch followed PR #69's precedent instead: check the code at the two
entry points that take it from outside -- `MonoLookup::makeFactory` and
`ModuleMonoSet::make` -- and leave the assert alone, now guarding an
invariant rather than user input.  The error names the bad value and lists
the valid codes, and `src/test/MonoLookup.cpp` covers both entry points.

That turned up a path this section had missed.  `-monomialTable` reaches
`ModuleMonoSet::make` and aborted the same way, so `mgb sig -monomialTable
99` is now an error too.  `mgb gb -monomialTable 99` still exits 0, because
the `gb` action never reads the value -- that is item 19's first bullet, and
it is unchanged.

### The grep this section asked for, done

All 13 `MATHICGB_ASSERT_NO_ASSUME` sites outside `stdinc.h`, classified:

| site | verdict |
|---|---|
| `StaticMonoMap.hpp:431` | **the bug above** -- asserts on `-divisorLookup` |
| `F4MatrixBuilder.cpp:67`, `F4MatrixBuilder2.cpp:282`, `F4MatrixReducer.cpp:778` | fine now.  PR #69 put the check in `F4Reducer`'s constructor and said so: "Establishing the bound here lets F4MatrixBuilder, F4MatrixBuilder2 and F4MatrixReducer assert it."  They assert an invariant established earlier, which is what asserts are for |
| `SigPolyBasis.hpp:221,224,227,250,253,256` | fine.  Checks a cached ratio rank against a freshly computed comparison -- pure internal invariant |
| `mathicgb.cpp:389,656,761` | fine.  `!hasBeenDestroyed` on use-after-destroy.  Assert-only, with no throw to be inconsistent with, and no correct recovery available |

So `StaticMonoMap.hpp:431` is the last of this shape, not merely the fourth.
The macro itself is not the problem, and the other 12 uses should stay.

### The same defect in the streaming interface, at 20 sites

Found while writing up section 23.  `MATHICGB_STREAM_CHECK`
(`src/mathicgb.cpp:22-36`) asserts before it throws:

```cpp
  const bool value = (X); \
  if (!value) { \
    MATHICGB_ASSERT(( ... , false )); \
    throw std::invalid_argument( ... ); \
  }
```

So a Debug build aborts where a Release build throws, on exactly the caller
protocol violations the macro exists to report -- calling `idealBegin()` twice,
and so on.  Twenty sites, in the public streaming interface.

Nothing catches it: every test that constructs an `IdealStreamChecker` uses the
protocol correctly, so no check ever fires, and before PR #80 no CI cell built
this code with assertions at all.

**Done** in ecd68c4.  The assert is gone and the macro only throws, and
`src/test/mathicgb.cpp` now drives a `StreamStateChecker` into three
protocol violations -- the first time any of these twenty checks has been
exercised.  It closed item 24 as a side effect and exposed item 25.

**Done** in 2f12500, and it was one change rather than two.  The check went
into `SparseMatrix::read` beside the size check from PR #83, and
`QuadMatrix::read` needed nothing of its own: it reads its four submatrices
through `SparseMatrix::read`, so all four are checked.  This section assumed
two changes where one does.

It also covered a read the CLI never checked at all -- the reference
`.rbrmat` a second run compares its output against -- and the file name,
which `read` cannot know, is now supplied by the CLI on the way past:

```
While reading composite.brmat:
ERROR: The modulus 100 is not prime. MathicGB only supports prime fields.
```

### Where the checks belong, which is the reason to do these together

Section 17 found that a check in the right place is also a check that can be
tested.  `mathicgb-unit-tests` links only `libmathicgb`: no `src/cli` object is
in it and there is no CLI-level harness, so PR #74's primality check, which
lives in `MatrixAction.cpp`, has no unit test and cannot be given one where it
stands.  Moving that validation into `SparseMatrix::read` and
`QuadMatrix::read` would make item 7 testable -- and PR #82's new
`src/test/QuadMatrix.cpp` and section 17's new `SparseMatrix` test are already
the harness it would use.

That is the through-line.  Each of these is a question about where a check
belongs and what it should do when it fires, and they should get one answer
rather than three:

- a check on user input belongs where the input is still intact and where a
  test can reach it, not in the CLI;
- when it fires it throws, and it does not also assert, because a Debug build
  must not abort on input a Release build handles.

The `StaticMonoMap.hpp` line is a one-line change and could go on its own
today.  The other two are a small design change to the library's input
handling, and are worth doing in one PR with a test for each check moved.

## [DONE] 19. `mgb gb` advertises `-monomialTable`, which only `sig` reads

PR #86, commit 61b992b.

Found while surveying the CLI defaults for 8.  Narrowed 2026-09-18: this
section used to carry `-spairQueue` as well, and to propose validation for
both.  Investigating that found `-spairQueue` is not unvalidated but dead,
which is a different problem with a different fix; it is now item 27.

`mgb gb -monomialTable 99` exits 0 and computes normally.  `mMonomialTable`
is read only by `SigGBAction.cpp:70`, so for the `gb` action the option does
nothing whatever -- any value is accepted because none is used.  It is
offered by `GBCommonParams`, which both actions share, so `mgb help gb`
advertises it.

Under `sig` the option works, and PR #85 now validates it: `mgb sig
-monomialTable 99` is refused, in Debug and Release alike.  So the
inconsistency is no longer about how a bad value is handled.  It is that one
action advertises an option it never reads, and accepts every value for it,
good or bad, because none reaches anything.

Contrast `-divisorLookup`, which both actions read and PR #85 validates, and
`-reducer`, which quietly substitutes its default (see 8).

The fix was to stop `gb` offering it: the parameter moved out of
`GBCommonParams` into `SigGBAction`'s own parameters, so `sig` keeps it with
its validation and `gb` no longer lists it.  Validating it for `gb` would
have been the wrong shape -- it would reject a typo for an option that does
nothing with a correct value either.

Making it work under `gb` was considered and has nowhere to go.  `PolyBasis`
holds one lookup structure, a `MonoLookup` (`PolyBasis.hpp:226`), and
`-divisorLookup` already selects it from the same four codes through the same
`staticMonoLookupMake`.  A working `-monomialTable` for `gb` could only
duplicate `-divisorLookup`.

Found along the way: this is a misfiling, not a regression.  At `31d363b`
there was one action and one flat option list, and the option fed the
signature code from it.  `399c07b` split `gb` and `siggb`, and `4753243`
sorted the options into `GBCommonParams` for the shared ones -- putting this
one there although `SigGBAction` already had its own parameter list.  It has
never worked under `gb`, not for a day.

Also found: `SigPolyBasis` takes the value and ignores it, which is item 26.

## [DONE] 20. `total compute time` reports CPU time as if it were elapsed

PR #87, commit c291c27.

Found 2026-08-31 while measuring item 10, and probably the reason item 10 was
filed in the first place.

`ClassicGBAlg::printStats` at `ClassicGBAlg.cpp:444` prints

    out << " total compute time: " << mTimer.getMilliseconds()/1000.0 ...

and `mathic::Timer` is a wrapper around `std::clock()`.  mathic's own header
says so plainly -- *"Measures spans of CPU time"* -- so the number is process
CPU time, the sum over all threads, not elapsed time.  The label says
otherwise, and so does "Time spent" a few lines down at `:460`.
`MESClassicGBAlg.cpp:426` has the same line.

Single-threaded this is invisible, because the two agree.  Multithreaded it
inverts the result.  From the `yang1` sweep in item 10, on an 18-thread
machine:

| threads | wall | mgb reports | user + sys |
|---|---|---|---|
| 1 | 43.76 s | 42.976 s | 42.98 s |
| 4 | 39.64 s | 53.410 s | 53.42 s |
| 16 | 38.65 s | 81.699 s | 81.70 s |

Every row matches `user + sys` to three digits.  So mgb reports itself getting
almost twice as slow across a sweep in which it actually got slightly faster.
Anyone timing mgb by reading its own output -- which is the obvious thing to
do, since it prints a time without being asked -- measures a large regression
that does not exist.  That is very likely how PR #65's 50% TBB regression came
to be recorded; no such regression reproduces under `time` on either machine.

Both are printed, and both are named.  `SignatureGB.cpp:294` had the same
defect and got the same treatment in the same pass, and the statistics block in
each algorithm relabels `Time spent:` to `CPU time spent:`.

The wall clock is `mtbb::tick_count` -- the one `LogDomain` already uses, TBB's
clock with a `std::chrono::steady_clock` fallback, so it needs no new
dependency and works with TBB off.  The elapsed figure is rounded to whole
milliseconds so that it prints at `mathic::Timer`'s granularity rather than as
a raw double beside it.

Verified against `/usr/bin/time` on `yang1` with `-reducer 26`:

| threads | mgb CPU | `user + sys` | mgb elapsed | wall |
|---|---|---|---|---|
| 1 | 264.416 | 264.45 | 264.512 | 264.55 |
| 12 | 681.794 | 681.89 | 591.234 | 591.33 |

Each figure matches its reference to within a tenth of a second.

`SignatureGB`'s line also lost a stray `--` between the value and its unit,
which read `0 -- seconds`.  Nothing else in either algorithm's output used that
separator.

`MESClassicGBAlg.cpp:426` has the same line and was left alone, because that
file is in neither build system -- see item 22.

Found along the way and split out as item 28: `mgb sig` reports its S-pair
queue as `todo`.

## [OPEN] 21. The default thread count uses every core, but nothing scales past four

Found 2026-08-31 while measuring item 10, whose `The TBB default` section holds
the tables and the argument.  This item is only what to do about them.

In short: F4's parallel speedup peaks at 1.75x on four threads and degrades
above that, and an i5-6300U hits the same ceiling with two physical cores that
an M5 Pro hits with four -- two machines, two compilers, two architectures, so
the ceiling is algorithmic.  `-threadCount 0`, the shipped default, means use
every core, which puts every user past the peak: 2.03x the CPU for 1.11x the
wall on `yang1`, and 6.5x the CPU for 1.64x the wall on `hyclic8`.

Two fixes, not exclusive:

- **Cap the default.**  Cheap, keeps essentially all of the available speedup,
  and needs only a defensible number.  Picking that number honestly wants more
  than the two inputs measured so far, and that measurement is the bulk of the
  work.
- **Restructure `reduceToEchelonForm`** so the parallel region spans the
  `while` loop rather than sitting inside it, with its full join and global
  `mtbb::mutex` on every iteration (`F4MatrixReducer.cpp:439-511`;
  `F4MatrixBuilder.cpp:153` has the same shape).  The real fix, and much the
  larger job.

The cap is worth doing first and on its own.  Neither is an argument for
reverting PR #65: TBB still wins on wall time everywhere measured -- with the
caveat below.

### A third machine, where more threads cost wall time outright

Measured 2026-09-18 as a side effect of verifying item 20, on an AMD Ryzen 5
2600 -- six physical cores, twelve threads -- with GCC 13.3, on `yang1` with
`-reducer 26`:

| threads | wall | CPU |
|---|---|---|
| 1 | 264.55 s | 264.45 s |
| 12 | 591.33 s | 681.89 s |

Twelve threads took **2.2x the wall time of one**.  That is not the shape this
section records: the tables above have wall time improving to a 1.75x peak and
then degrading, never falling below the single-threaded time.

Three reasons to repeat this before believing it, in order of how much they
could explain:

- `-threadCount 12` is twice the physical core count on this machine, so half
  the threads are SMT siblings.  The sweeps above stopped at or near the
  physical count.
- `lscpu` reports `CPU(s) scaling MHz: 68%`, so an all-core run is very likely
  clock-limited in a way a single-threaded run is not.  None of the earlier
  measurements noted governor or boost behaviour.
- It is one unrepeated pair of runs, where the tables above are sweeps.

`BENCH-RUNBOOK.md` exists for exactly this -- a sweep on this machine, at 1, 2,
4 and 6 threads rather than 1 and 12, would settle whether the ceiling is
simply lower here or whether oversubscription and clock throttling account for
all of it.  Until then this is a lead, not a result, and the "TBB still wins on
wall time" sentence above should be read as "everywhere measured *so far*".

### Why the cap is blocked, and on what

Reviewed 2026-09-18.  This section previously proposed capping the default at
four and called it the cheap fix worth doing first.  The one-line change is
cheap; the number is not supported.  Four problems with it:

- **The measurement behind "four" is a sub-second run.**  The 1.75x peak is
  `hyclic8-101-trimmed` on the M5 Pro, whose fastest cell is 0.44 s.
- **The two inputs disagree.**  `hyclic8` peaks at four threads, `yang1` at
  **two**, and `yang1` is the longer and more trustworthy of the pair.  Four
  is already a split between the only two inputs measured.
- **Every machine measured is small** -- 2 physical cores, 6 P-cores, 6 cores.
  No machine with 32 or more has been tested, which is both where the current
  default does the most harm and where a hardcoded four would idle the most
  hardware.  "The ceiling is algorithmic, not hardware" is extrapolated from
  three small machines.
- **The three machines disagree about the shape past the peak**, per the
  subsection above.

### The sweep that would settle it, and what it is waiting on

Georgia Tech's PACE Phoenix cluster has `cpu-gnr` nodes with 192 CPUs (Intel
Granite Rapids, 54 nodes) and `cpu-amd` nodes with 128 (4 nodes).  That is an
order of magnitude past anything in the tables above, and running both tests
the central claim directly: if Intel-192 and AMD-128 flatten at the same small
thread count, the ceiling is algorithmic after all.  If they do not, it never
was.

`yang1` is the input to use.  It is the only one in a usable range -- 43.76 s
on the M5 Pro, 264.5 s on the Ryzen -- where `hyclic8` is 0.77 s and
`hyclic9-101-trimmed` exceeds 600 s at one thread.

The release build is fine for this.  `reduceToEchelonForm` and
`F4MatrixBuilder` have **no change to the parallel structure between `v1.4` and
master** -- not one line touching `parallel_for`, the mutex, the `while` loop
or `task_arena`; the only difference in those files is a trailing space from
the whitespace-normalisation commit.  So v1.4 measures the same scaling
behaviour, and no source build is needed.

**What blocks it: spack's `mathicgb` has no TBB.**  The package declares
`mathic` and `memtailor` and passes only `--enable-shared`, so TBB is never in
the build environment, `configure` falls through `--with-tbb=detect` to its
`AC_MSG_WARN` path, and the result is built with `-DMATHICGB_NO_TBB`.
Single-threaded, no error, one warning in a build log.  `ldd` on the installed
`mgb` shows no `libtbb`.

Every spack-installed mathicgb since v1.1 is affected, and spack's Macaulay2
depends on `mathicgb` *and* on `tbb` separately -- so it has had TBB available
to itself while linking a single-threaded mathicgb.

spack/spack-packages#6536 adds a `tbb` variant defaulting to on, and passes
`--with-tbb` explicitly rather than leaving detection to fall through.  Once
that lands the sweep can run against a stock module.

### The footgun underneath the packaging bug

Worth recording separately from the packaging fix: this was possible because
mathicgb's own `configure` treats multithreading as optional and silent.
`--with-tbb=detect` is the default, and when detection fails it warns and
continues rather than failing.  That is how a competent packager -- who is also
the upstream maintainer -- shipped a single-threaded build for four releases
without noticing.

Whether `detect` should remain the default, or a missing TBB should be an error
unless `--without-tbb` is explicit, is its own question.  Same family as the
review's other findings where the build quietly does something other than what
was asked.

## [OPEN] 22. Dead store in `setSPairGroupSize`, in two files

Found 2026-08-31 while chasing item 10's `yang1` baseline.

`ClassicGBAlg::setSPairGroupSize` at `ClassicGBAlg.cpp:150` assigns
`mReducer.preferredSetSize()` to its own by-value parameter and drops it:

    void ClassicGBAlg::setSPairGroupSize(unsigned int groupSize) {
      if (groupSize == 0)
        groupSize = mReducer.preferredSetSize();   // dead store
      else
        mSPairGroupSize = groupSize;
    }

Behaviour is correct only because the constructor at `:127` already initializes
the member to the same value, so the branch that looks like it computes the
default is the branch that does nothing.  `MESClassicGBAlg.cpp:137` is
identical.

Either the assignment should go to `mSPairGroupSize`, or the branch should
collapse to a comment saying the constructor already handled it.  The second is
more honest about what the code does.  Low priority -- there is no user-visible
symptom -- but it is a trap for anyone changing how the default is chosen,
which item 21 might well involve.

### The second file is not built at all

Noted 2026-09-18 while doing item 20.  `MESClassicGBAlg.cpp` appears in
neither `Makefile.am` nor `src/CMakeLists.txt`, so it compiles nowhere.  It is
a stale copy of `ClassicGBAlg.cpp` that has been carried along and has drifted:

| | `ClassicGBAlg.cpp` | `MESClassicGBAlg.cpp` |
|---|---|---|
| the dead store | `:150` | `:137` |
| `total compute time` mislabelled | `:444`, fixed by item 20 | `:426`, left alone |

So "in two files" overstates it -- one of the two ships and one does not.
Item 20 deliberately did not touch its copy of the timing line, since a fix
there compiles nowhere and only widens the drift.

That makes the real question about this file prior to the dead store: whether
it should exist.  Deleting it would close this item, remove item 27's
`queueType` from a third site, and stop future greps turning up two answers to
every question.  Keeping it means it should at least be built.  Either way the
dead store is the smaller half of the decision.

## [OPEN] 23. A UBSan job

Split out of section 14, which called it the natural companion to the matrix:
four of the five commits before this review were UBSan findings, all found by
hand.  It is a job of a different shape rather than a seventeenth cell, so it
was left out of PR #80.

Measured 2026-09-01 against the *packaged* dependencies -- `libmathic-dev` and
`libmemtailor-dev` 1.0~git20230916-1, the same ones CI installs -- with cmake
Debug, GCC 13.3 and `-fsanitize=undefined` left recoverable so that one run
collects everything rather than dying on the first report:

| | |
|---|---|
| reports | 325 |
| distinct sites | 113 |
| tests still passing | 246/246 |

Every one is an alignment error.  Not a single integer overflow, shift, null
dereference, bounds or vptr error in the whole run:

```
117  reference binding to misaligned address
 98  member access within misaligned address
 73  member call on misaligned address
 25  load of misaligned address
 12  constructor call on misaligned address
```

179 are in mathic's installed headers, 94 in our own code, and 52 in libstdc++
headers -- the last being merely where a misaligned object gets touched, not a
libstdc++ bug.  Our own are dominated by `StaticMonoMap.hpp:54`, the `Entry`
whose payload is `std::pair<ConstMonoPtr, Data>`: it holds a pointer, so it
wants 8-byte alignment, and mathic's `KDEntryArray` places it in memtailor
`Arena` memory on a 4-byte boundary.  mathic's own flagged types (`Extender`,
`DivMask`) have the same shape.  One cause, not 113 bugs.

Nothing crashes, because x86-64 tolerates misaligned access.  It is still
undefined behaviour, and it is the class that faults on strict-alignment
targets -- not hypothetical for a package Debian builds on m68k and powerpc,
architectures `configure.ac` already names in its `-latomic` check.

**This is fixed in mathic's git already, but not in the Ubuntu package CI
installs**, so the job cannot gate alignment yet.

What to add now: ubuntu, cmake Debug,

```
-fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=all
-fno-omit-frame-pointer -g
```

with `UBSAN_OPTIONS=print_stacktrace=1`.  `-fno-sanitize-recover=all` is what
makes the job fail rather than print and pass.  Verified against the packaged
mathic: 246/246, zero reports, exit 0, in 48 s against 28 s for the same suite
without the sanitizer.  So it lands green and gates every other class of
undefined behaviour from day one.

The one real cost is that `-fno-sanitize=alignment` is blunt: it silences new
alignment UB in our own code as well as the inherited kind.  Targeted runtime
suppressions would scope it to mathic's headers, but they would not help while
our own 94 sites share the root cause and would still fire.  The trigger for
dropping the exclusion is the packaged mathic catching up; our own sites should
be re-checked then, since they may well go with it.

## [DONE] 24. 19 clang warnings in mathicgb.cpp, and an assert that outranks its throw

Fixed by ecd68c4, in PR #85 as part of item 18.  Not by the respelling
proposed below, but by deleting the assert altogether, which removes the
comma-operator idiom the warning is about.  clang 18.1.3 reports 19
warnings in this file before and 0 after.  The two loose ends recorded at
the end of this section are untouched.

Found 2026-09-02 in PR #81's CI, in the macOS debug cells -- which is the
point: nothing built macOS with assertions before PR #80 added them, so these
have been there unseen.  19 warnings, all in `src/mathicgb.cpp`, all the same:

```
../src/mathicgb.cpp:136:7: warning: left operand of comma operator has no
effect [-Wunused-value]
  136 |  MATHICGB_STREAM_CHECK(isPrime(modulus), "The modulus must be prime");
../src/mathicgb.cpp:28:9: note: expanded from macro 'MATHICGB_STREAM_CHECK'
```

They appear in all four macOS debug cells, cmake and autotools alike, and in
none of the ubuntu cells and none of the release cells.  Debug-only because
`MATHICGB_ASSERT` is empty otherwise; clang-only because GCC does not warn on
this.  Reproduced locally with clang 18.1.3: the same 19, at the same sites.

The cause is the `assert(("message", condition))` idiom in
`MATHICGB_STREAM_CHECK` (`mathicgb.cpp:22-36`), which smuggles the message
into what `assert` stringifies.  The left operand genuinely has no effect, so
the warning is correct even though the intent is fine.

The fix is one line, to the conventional spelling:

    MATHICGB_ASSERT(false && \
      "MathicGB stream protocol error: "#MSG \
      "\nAssert expression: "#X"\n" \
    );

Verified: 0 warnings from clang 18.1.3 and 0 from GCC 13.3, with the message
still reaching the assertion text.  It also retires the
`[[maybe_unused]] const bool ignoreMe = false;` line above it, which is
declared, never used, and looks like an earlier attempt at this same problem.

Underneath the warning, the same macro also asserts before it throws, at 20
sites in the public streaming interface.  That is item 18's subject, not this
one's: fixing the warning does not change the abort, and the two are separable.

### Also in those logs, and not worth items of their own

- `ld: warning: -single_module is obsolete` and `ld: warning: -bind_at_load is
  deprecated on macOS`, in every macOS autotools cell.  Both flags come from
  libtool, not from us.
- `Makefile.am:64: warning: wildcard \ $(top_srcdir: non-POSIX variable name`,
  from automake on every autotools cell including ubuntu.  That is the
  `$(wildcard ...)` in `mathicgbB_include_HEADERS`, which installs the headers
  by glob rather than by list.  It predates this review and is the only
  automake warning in the build.

## [OPEN] 25. The `Pimpl` pointers are raw, and two constructors get it wrong

Found 2026-09-18 while doing item 18's part 2.

`src/mathicgb.h` declares three `Pimpl* const mPimpl` members -- at `:300`
(`GroebnerConfiguration`), `:342` (`GroebnerInputIdealStream`) and `:524`
(`mgbi::StreamStateChecker`).  Each is allocated in a mem-initializer list, so
a constructor body that throws leaks it: the object was never constructed, so
the destructor never runs.  Two of those constructors can throw, and they fail
in opposite directions.

### The leak, which is reachable

`GroebnerConfiguration::GroebnerConfiguration` (`src/mathicgb.cpp:410`)
allocates the `Pimpl` and then rejects a composite modulus at `:416` with
`mathic::reportError`.  Nothing frees it.  Under ASan, constructing
`GroebnerConfiguration(4, 2, 1)`:

```
Direct leak of 120 byte(s) in 1 object(s) allocated from:
    #1 mgb::GroebnerConfiguration::GroebnerConfiguration(unsigned int, unsigned long, unsigned int)
SUMMARY: AddressSanitizer: 128 byte(s) leaked in 2 allocation(s).
```

`RejectsCompositeModulus` (`src/test/mathicgb.cpp:913`) drives that path five
times, so the suite has leaked this on every run since the test was added.
Nothing notices because no job runs a sanitizer; item 23's, once it exists, is
what would catch it.

### The double free, which is not

`StreamStateChecker`'s constructor (`src/mathicgb.cpp:128`) does remember the
cleanup, and then swallows the exception:

```cpp
    try {
      MATHICGB_STREAM_CHECK(isPrime(modulus), "The modulus must be prime");
      MATHICGB_ASSERT(mPimpl->debugAssertValid());
    } catch (...) {
      delete mPimpl;
    }
```

No rethrow, so the constructor completes with `mPimpl` dangling and the
destructor deletes it a second time.  Every other `catch (...)` in the sources
cleans up and rethrows -- `ClassicGBAlg.cpp:308`, `MESClassicGBAlg.cpp:290`,
`GBMain.cpp:44` -- so this is an omission rather than a choice.

It is unreachable through the public interface.  The checker takes its modulus
from `conf.modulus()` (`src/mathicgb.cpp:620`), and `GroebnerConfiguration` has
already rejected a composite one by then -- which is the leak above.  Only
direct construction of `mgbi::StreamStateChecker`, which nothing but a test
does, reaches it.  Before ecd68c4 the assert in `MATHICGB_STREAM_CHECK` fired
first and hid it in Debug; in Release it has been a double free all along.

### One fix for both

`std::unique_ptr<Pimpl>` for all three members.  The try/catch in
`StreamStateChecker` then goes away rather than gets corrected, the
`GroebnerConfiguration` leak goes away without anyone having to remember a
cleanup path, and the `delete mPimpl` in each destructor goes with it.
`Pimpl` is a complete type at every point that matters -- all three are
defined in `src/mathicgb.cpp` alongside their users -- so the destructor
requirement is already met.

A one-line `throw;` in `StreamStateChecker` was written, tested and then
declined: it corrects the unreachable half, leaves the reachable one, and the
`unique_ptr` conversion would immediately undo it.

## [OPEN] 26. `SigPolyBasis` takes a monomial table code it has never used

Found 2026-09-18 while doing item 19.

`SigPolyBasis`'s constructor takes `int monTableType` (`SigPolyBasis.hpp:38`,
`SigPolyBasis.cpp:18`) and the definition never mentions it again.
`SignatureGB` passes the `-monomialTable` value to two places -- `SigPolyBasis`
and the `Hsyz` module monomial sets -- and only the second reads it.

Not recent decay.  At the initial commit, `31d363b`, the ancestor class
`GroebnerBasis` took the same `int monTableType` and its `.cpp` never
referenced it either.  Dead since 2012-07-09.

Harmless in itself, since the value is computed for `Hsyz` regardless.  It is
recorded because it is the third instance of one pattern, and the three are
worth one decision rather than three:

| site | what is dead |
|---|---|
| `SigPolyBasis.hpp:38` | `monTableType`, never read -- this item |
| `ClassicGBAlg.cpp:122`, `SigSPairs.cpp:23` | `queueType`, `(void)`-cast away -- item 27 |
| `ClassicGBAlg.cpp:150` | the `setSPairGroupSize` dead store -- item 22 |

`monTableType` is the one that can go on its own.  The `queueType` parameters
have to wait for item 27, since restoring that option would want them back;
nothing would ever bring `monTableType` back, because `-monomialTable`
reaches `Hsyz` directly without it.

## [OPEN] 27. The S-pair queue choice is dead, and only mathic can bring it back

Found 2026-09-18 on starting item 19, which had assumed `-spairQueue` was
merely unvalidated.  Placed last deliberately: it is the only item that
cannot be finished without a change to mathic, and everything ahead of it can
be done without one.

### It does nothing at all

`queueType` is threaded through four layers and discarded at both ends --
`ClassicGBAlg.cpp:141` and `SigSPairs.cpp:36` are each `(void)queueType;`.
On `cyclic5`, codes 0, 1, 2, 3 and 99 all give byte-identical output and the
same reported queue type, and `mgb` prints one fixed answer regardless:

```
S-pair queue type:  PairQueue-t-tree (si)
```

Meanwhile `mgb help gb` advertises four choices.

`28fb618`, "Removed PairTriangle as it is no longer used (it's replaced by
the mathic version)", orphaned it in 2013.  `33d4394`, "Fix unused parameter
compiler warnings", then added the `(void)` casts that have kept it quiet.

### What it used to select

Not four things.  Two bits, at `31d363b`, `PairTriangle.cpp:98-104`:

```cpp
PairTriangle::PairTriangle(const FreeModuleOrder& order, const PolyRing& ring,
                           size_t queueType):
  mUseSingletonGroups((queueType & 2) != 0),
  mQueue(order.makeQueue((queueType & 1))),
```

Bit 0 chose the priority queue -- `FreeModuleOrder::makeQueue` switched
`mic::TourTree` against `mic::Heap`.  Bit 1 chose whether S-pairs were staged
behind the triangle: clear meant one group per newly added basis element with
only its smallest pair competing in the queue, set meant every pair pushed
individually.

| code | bit 1 | bit 0 | help text |
|---|---|---|---|
| 0 | staged behind triangle | TourTree | tournament tree in front of triangle |
| 1 | staged behind triangle | Heap | heap in front of triangle |
| 2 | every pair in the queue | TourTree | tournament tree |
| 3 | every pair in the queue | Heap | heap |

### Half of it can come back, and only through mathic

Bit 0 looks cheap.  `mathic::Heap` implements all eight operations
`PairQueue` calls on its column queue -- `push`, `pop`, `top`, `empty`,
`decreaseTop`, `forAll`, `getName`, `getMemoryUse` -- and
`TourTreeSuggestedOptions` is an empty class, so the base it contributes
costs nothing to generalise.  The hardcoding is one line, `PairQueue.h:371`:

```cpp
typedef TourTree<QueueConfiguration> ColumnQueue;
```

A template parameter defaulting to `TourTree` would keep every existing
`PairQueue<C>` spelling working.

Bit 1 cannot come back.  Staging *is* `mathic::PairQueue`'s architecture --
lazy per-column expansion is the design -- so turning it off means a second
implementation, not a parameter, and it is the half its author deliberately
replaced.  The ceiling is therefore two codes rather than four, and the
option returns renumbered whatever happens.

### Measure first, and the measurement needs no release

Item 10 exists because this codebase carried performance claims nobody had
checked this decade.  Restoring a queue knob without knowing whether `Heap`
ever beats `TourTree` would add another one, with the help text asserting a
tradeoff that may not exist.

The measurement needs no mathicgb change and no released mathic: build
against a locally patched mathic with that typedef flipped, and run the
harness item 10 left behind -- `bench.py` and `BENCH-RUNBOOK.md`.  If `Heap`
wins on some class of input, that is the reason to make the mathic change and
reintroduce the option honestly.  If it does not, the question is settled for
good and the option can simply go.

### Until then

`-spairQueue` is left exactly as it is: accepted, advertised and inert.
Removing it now would churn against a possible return and would turn
`mgb gb -spairQueue 0` into a parse error for no gain, and item 23 already
records what depending on an unreleased mathic costs.

## [OPEN] 28. `mgb sig` reports its S-pair queue type as `todo`

Found 2026-09-18 while comparing `mgb sig`'s statistics before and after
item 20.

`SigSPairQueue.cpp:106` is

```cpp
virtual std::string name() const {return "todo";}
```

so `mgb sig` prints

```
 S-pair queue type: todo
```

where the classic algorithm prints `PairQueue-t-tree (si)`.  A placeholder
that was never filled in, visible to every user of the signature algorithm.
Same family as item 8's wrong strings, but in the statistics rather than the
help text, and found too late to go in that PR.

One line.  `ConcreteSigSPairQueue` wraps a `mathic::PairQueue` exactly as
`SPairs` does, and `SPairs::name()` simply returns `mQueue.name()`, so the
honest value is available the same way.

Numbered after item 27 although it should be done long before it: from here on
new items are appended rather than inserted, so item 27 keeps the number it
has.  Reading order is not priority order past item 25 -- the table's notes
carry that.

## Considered and declined

Recorded so they do not get re-raised.

- **A `push` trigger for the workflow.**  Withdrawn -- I had inferred from the
  linear history that these commits bypassed CI.  They did not; PRs #60-#68
  cover all of them and `pull_request` tested each one.
- **Making `DefaultReducer` fall back to the classic reducer above 2^16.**
  `DefaultReducer` means the default reducer, which is F4.  Working as
  intended.
- **`_MSVC_LANG` in the C++17 guard at `stdinc.h:6`.**  Windows is not
  supported.
