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
| 10 | performance claims nobody has checked this decade: TBB default, F4 inner loop | open |
| 11 | remove `build/setup/make-Makefile.sh` | open |
| 12 | remove `build/vs12` | open |
| 13 | autotools `--enable-debug` | open |
| 14 | expand the CI matrix | open |
| 15 | hand-written atomics, live on GCC since 2013 | open |
| 16 | `QuadMatrix::read` reads three of four submatrices only in Debug | open |
| 17 | `SparseMatrix::read` truncates the file's modulus to 16 bits | open |
| 18 | a fourth `MATHICGB_ASSERT_NO_ASSUME` on user input, in `StaticMonoMap.hpp` | open |
| 19 | two `gb` options accept out-of-range values silently | open |

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

## [OPEN] 10. Performance claims nobody has checked this decade

PR #65's own
commit message records `yang1` at 20.7-21.8 s wall and 28-30 s user against
14.2-14.4 s serial, while `hyclic8-101-trimmed` goes 3.6-3.8 s -> 1.06-1.07 s.
Enabling TBB was right; it made a pre-existing 50% slowdown reachable by
default for a whole class of input.  Worth investigating, or at minimum a
note in the release announcement.

Moved here from 9, because it is the same kind of question and wants the same
harness: three performance claims in `F4MatrixReducer::addRowMultiple`, the
innermost loop of matrix reduction, all measured on **MSVC 2012** in 2013 and
never since.

- `:131` -- using the local `entries` rather than the member `mEntries` was
  worth 2.8 s -> 2.4 s, and undoing it is said to cost 14% of the whole
  computation.  The author adds "That does not make sense to me, but it is a
  fact none-the-less."
- `:147` -- unrolling the loop by hand was worth 2.601 s -> 2.480 s, about
  5%; unrolling further gained nothing.
- `:156` -- a Duff's-device jump into the loop body was tried and was slower.

They are unverifiable as they stand, and they guard code that looks
gratuitously ugly without them, so a reader who deletes the comments will
eventually delete the unrolling too.  Deleting them loses a real caution;
keeping them implies a measurement that means nothing on any compiler in use.

Measuring is the only way out, and it is a real exercise rather than a
five-minute check: three variants, effects of 5-14%, on input where the
review already documents 50% run-to-run swings under TBB.  It wants the
serial build, many repetitions, and an honest note that the answer covers one
machine and one compiler.  Doing it alongside the TBB question is the point
of moving it here -- both need the same benchmark harness, and the TBB
result is worth little without knowing whether this loop is still shaped the
way its comments assume.

## [OPEN] 11. Remove build/setup/make-Makefile.sh

Reviewed 2026-08-30 with section 12.  `build/autotools` is fine -- two
`.gitignore`s and `ax_cxx_compile_stdcxx.m4`, which `configure.ac` calls for
the C++17 requirement.  The other two subdirectories are unreferenced by
either build system and cannot work.

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

## [OPEN] 12. Remove build/vs12

Visual Studio *2012* project files.  Last real change 2013-10-03; the two
commits since are a file-permission fix and the CRLF normalization in f78d6be.

Staleness is not the decisive part -- they cannot work.  `stdinc.h` has
required C++17 since PR #60 and VS2012 does not do complete C++11, so any
build stops at `#error "mathicgb requires C++17 or later"`.  Independently the
file list has been wrong since 2021: `MESClassicGBAlg.cpp`, added in e6996a3,
is missing from `mathicgb-lib.vcxproj`.  `notes.txt` is a 300-line personal
write-up of clicking through the VS2012 GUI, including how to build gtest 1.6
with `_VARIADIC_MAX=10`.

Two things go with it.  `.gitattributes:6` sets `*.sln text eol=crlf`, a rule
that exists solely for `build/vs12/mathicgb.sln` -- there are no other `.sln`,
`.vcxproj` or `.filters` files anywhere in the tree, so it would match nothing.
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
deleting.  Two commits.  Afterwards `build/` holds only `autotools/`, at which
point flattening it is a reasonable follow-up -- but that churns paths in
`configure.ac` and `Makefile.am`, so it wants to be separate.

## [OPEN] 13. Give the autotools build a --enable-debug

`configure.ac` and `Makefile.am` never mention `MATHICGB_DEBUG`, so an
autotools build cannot turn our assertions on at all -- the cmake build is the
only one that can.  That is the reason the CI matrix in section 14 has 12 real cells
rather than 16.

Two ways to close it.  Add `--enable-debug` to `configure.ac`, which is parity
the build system arguably should have anyway, or have CI pass
`./configure CXXFLAGS="-g -DMATHICGB_DEBUG"`.  Prefer the former: the latter
tests a configuration no user can conveniently request, which is a weak test.

Note that `MATHICGB_DEBUG` must reach every target that includes our headers,
not just the library -- that is the bug fixed in item 2, and an autotools
`--enable-debug` has to avoid repeating it for `mgb`.

---

## [OPEN] 14. Expand the CI matrix

Reopened 2026-08-29, having been declined earlier in the review.  There are
four axes worth varying:

| axis | what it would have caught |
|---|---|
| cmake vs autotools | PR #65's TBB detection bug was cmake-only; PR #59's `-latomic` was autotools-only |
| debug vs not | PR #68's `Arena` abort and PR #66's assert-abort were both debug-only, both found by hand |
| tbb vs not | `mtbb.hpp`'s hand-rolled `mutex`, `task_arena` and `enumerable_thread_specific` are compiled by nobody today |
| macos vs ubuntu | already in place; catches AppleClang/libc++ and Homebrew paths |

The full cross product reads as 16 jobs, but only **12 are real today**,
because the autotools build has no debug mode to vary (see section 13).  Cost is not
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
- **The debug axis is the one to prioritise** if this gets phased in.  It is
  where this release cycle's actual bugs lived.

A UBSan job is the natural companion: four of the five most recent commits
before this review were UBSan findings, all found by hand.

## [OPEN] 15. Reconsider the hand-written atomics

`src/mathicgb/Atomic.hpp` is 398 lines implementing atomic load and store by
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

## [OPEN] 16. `QuadMatrix::read` reads three of four submatrices only in Debug

`src/mathicgb/QuadMatrix.cpp:359` wraps the reads of `topRight`,
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

## [OPEN] 17. `SparseMatrix::read` truncates the file's modulus to 16 bits

`src/mathicgb/SparseMatrix.cpp:500` reads the modulus with `readOne<uint32>`
and returns it as `SparseMatrix::Scalar`, which is `uint16`.  A file claiming
65637 yields 101, and the primality check PR #74 adds sees only the truncated
value, so it passes and the reduction proceeds over a field the file did not
name.

Narrow in practice: `write` narrows on the way out too, so nothing mgb
produces can trigger it, and entries are `uint16`, so a modulus above 2^16 is
meaningless in this format regardless.  The fix is to reject a modulus that
does not fit rather than silently reinterpret it, inside `read` where the
`uint32` is still intact.

That is also the place a modulus check would have to live to be testable at
all.  `mathicgb-unit-tests` links only `libmathicgb`; no `src/cli` object is
in it and there is no CLI-level harness, so the check PR #74 adds has no unit
test and cannot be given one where it stands.  Moving the validation into
`SparseMatrix::read` and `QuadMatrix::read` would close this item and make
7 testable in one move.

## [OPEN] 18. A fourth assert-on-user-input, in `StaticMonoMap.hpp`

`src/mathicgb/StaticMonoMap.hpp:431` is 1 all over again:

```cpp
  default:
    MATHICGB_ASSERT_NO_ASSUME(false);
    throw std::runtime_error("Unknown code for monomial data structure");
```

PR #69 fixed three sites of exactly this shape in `F4MatrixBuilder2.cpp`,
`F4MatrixBuilder.cpp` and `F4MatrixReducer.cpp`; this one was not in that
list because I found the three by grepping the F4 files.  It is reachable
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

A grep for `MATHICGB_ASSERT_NO_ASSUME` across the tree would be worth doing
in the same pass, to find out whether 19 is the last of these or merely the
fourth.

## [OPEN] 19. Two `gb` options accept out-of-range values silently

Found while surveying the CLI defaults for 8.

- `mgb gb -monomialTable 99` exits 0 and computes normally.
  `mMonomialTable` is read only by `SigGBAction.cpp:70`, so for the `gb`
  action the option does nothing whatever -- any value is accepted because
  none is used.  It is offered by `GBCommonParams`, which both actions
  share, so `mgb help gb` advertises it.
- `mgb gb -spairQueue 99` also exits 0, but this one is passed on:
  `GBAction.cpp:103` assigns it to `params.sPairQueueType` and the value
  reaches the algorithm unchecked.

Contrast `-divisorLookup`, which throws on an unknown code (see 18), and
`-reducer`, which quietly substitutes its default (see 8).  Three options
selecting a data structure, three different answers to the same bad input.

The fix worth having is validating each of these where it is parsed, so a
typo is refused rather than ignored.  Deciding whether `gb` should advertise
an option it does not use is a separate question, and the smaller one.

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
