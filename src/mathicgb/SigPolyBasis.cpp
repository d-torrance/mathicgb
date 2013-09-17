// MathicGB copyright 2012 all rights reserved. MathicGB comes with ABSOLUTELY
// NO WARRANTY and is licensed as GPL v2.0 or later - see LICENSE.txt.
#include "stdinc.h"
#include "SigPolyBasis.hpp"

#include "Poly.hpp"
#include <mathic.h>
#include <limits>
#include <iostream>
#include <iomanip>

MATHICGB_NAMESPACE_BEGIN

SigPolyBasis::SigPolyBasis(
  const PolyRing& R0,
  int monoLookupType,
  int monTableType,
  bool preferSparseReducers
):
  mMonoLookupFactory
    (MonoLookup::makeFactory(R0.monoid(), monoLookupType)),
  mRatioSorted(RatioOrder(mSigLeadRatio, R0.monoid())),
  mMinimalMonoLookup(mMonoLookupFactory->make(preferSparseReducers, true)),
  mBasis(R0, mMonoLookupFactory->make(preferSparseReducers, true)),
  mPreferSparseReducers(preferSparseReducers)
{
  mTmp = mBasis.ring().allocMonomial();
  const_cast<MonoLookup&>(mBasis.monoLookup()).setSigBasis(*this);
  mMinimalMonoLookup->setSigBasis(*this);
}

SigPolyBasis::~SigPolyBasis()
{
  MATHICGB_ASSERT(mBasis.size() == mSignatures.size());
  MATHICGB_ASSERT(mBasis.size() == mSigLeadRatio.size());

  for (size_t i = 0; i < mBasis.size(); i++) {
    if (!mSignatures[i].isNull())
      ring().freeMonomial(mSignatures[i]);
    if (!mSigLeadRatio[i].isNull())
      ring().freeMonomial(mSigLeadRatio[i]);
  }
  for (size_t i = 0; i < mSignatureLookup.size(); ++i)
    delete mSignatureLookup[i];
  mBasis.ring().freeMonomial(mTmp);
}

void SigPolyBasis::addComponent() {
  std::unique_ptr<MonoLookup> lookup =
    mMonoLookupFactory->make(mPreferSparseReducers, true);
  lookup->setSigBasis(*this);
  mSignatureLookup.push_back(0);
  mSignatureLookup.back() = lookup.release(); // only release after alloc
}

void SigPolyBasis::insert(monomial sig, std::unique_ptr<Poly> f)
{
  MATHICGB_ASSERT(f.get() != 0);
  MATHICGB_ASSERT(f->getLeadCoefficient() != 0);
  // This assert really should work. todo: why doesn't it?
  //MATHICGB_ASSERT(sig.isNull() || ring().fromPool(sig));
  const size_t index = mSignatures.size();

  mSignatures.push_back(sig);
  
  monomial ratio = 0;
  if (!sig.isNull()) {
    const size_t component = ring().monomialGetComponent(sig);
    MATHICGB_ASSERT(component < mSignatureLookup.size());
    mSignatureLookup[component]->insert(sig, index);

    ratio = ring().allocMonomial();
    ring().monomialDivideToNegative(sig, f->getLeadMonomial(), ratio);
  }
  mSigLeadRatio.push_back(ratio);

  const_monomial const lead = f->getLeadMonomial();
  mBasis.insert(std::move(f));
  if (mBasis.leadMinimal(mBasis.size() - 1)) {
    mMinimalMonoLookup->removeMultiples(lead);
    mMinimalMonoLookup->insert(lead, index);
  }

  MATHICGB_ASSERT(mMinimalMonoLookup->type() == 0 ||
    mBasis.minimalLeadCount() == mMinimalMonoLookup->size());
  MATHICGB_ASSERT(mSignatures.size() == index + 1);
  MATHICGB_ASSERT(mBasis.size() == index + 1);
  if (!mUseRatioRank || sig.isNull())
    return;

  // compute rank of the ratio
  RatioSortedType::iterator pos = mRatioSorted.insert(index);
again:
  Rank prevRank;
  if (pos == mRatioSorted.begin())
    prevRank = 0;
  else {
    RatioSortedType::iterator prev = pos;
    --prev;
    prevRank = mRatioRanks[*prev];
    if (monoid().equal(ratio, mSigLeadRatio[*prev])) {
      mRatioRanks.push_back(prevRank);
      return;
    }
  }

  Rank nextRank;
  RatioSortedType::iterator next = pos;
  ++next;
  if (next == mRatioSorted.end())
    nextRank = std::numeric_limits<Rank>::max();
  else {
    nextRank = mRatioRanks[*next];
    if (monoid().equal(ratio, mSigLeadRatio[*next])) {
      mRatioRanks.push_back(nextRank);
      return;
    }
  }
  MATHICGB_ASSERT(prevRank < nextRank);

  // this formula avoids the overflow inherent in prevRank + nextRank;
  Rank rank = prevRank + (nextRank - prevRank) / 2;

  // must have at least 1 space between ranks to support
  // queries for non-basis element rank
  if (rank == 0 || // must leave space for smaller ratio
    rank == std::numeric_limits<Rank>::max() || // shouldn't happen
    nextRank - prevRank < 4) { // 4 as require: prev, gap, new, gap, next
    // size plus 1 to account for the gaps at the beginning and end.
    size_t increment = std::numeric_limits<Rank>::max() / (mSignatures.size() + 1);
    if (increment == 0)
      increment = 2;
    MATHICGB_ASSERT(!mRatioSorted.empty());
    size_t rankSum = increment; // leave a gap at beginning
    Rank prevRank = *mRatioRanks.begin();
    for (RatioSortedType::iterator it = mRatioSorted.begin();
      it != mRatioSorted.end(); ++it) {
      if (it == pos)
        continue;
      if (mRatioRanks[*it] != prevRank)
        rankSum += increment;
      prevRank = mRatioRanks[*it];
      mRatioRanks[*it] = rankSum;
    }
    goto again;
  }
  MATHICGB_ASSERT(rank > 0);
  MATHICGB_ASSERT(rank < std::numeric_limits<Rank>::max());
  MATHICGB_ASSERT(prevRank + 1 < rank && rank < nextRank - 1);
  mRatioRanks.push_back(rank);
  MATHICGB_ASSERT(mRatioRanks.size() == index + 1);

#ifdef MATHICGB_DEBUG
    // Check that at least one space has been left between every rank
    MATHICGB_ASSERT(mRatioRanks[*mRatioSorted.begin()] > 0);
    MATHICGB_ASSERT(mRatioRanks[*mRatioSorted.rbegin()] <
      std::numeric_limits<Rank>::max());
    RatioSortedType::iterator it2 = mRatioSorted.begin();
    for (++it2; it2 != mRatioSorted.end(); ++it2) {
      RatioSortedType::iterator prev = it2;
      --prev;
      MATHICGB_ASSERT(mRatioRanks[*it2] == mRatioRanks[*prev] ||
        mRatioRanks[*it2] - 1 > mRatioRanks[*prev]);
    }
#endif
}

size_t SigPolyBasis::regularReducer(
  const_monomial sig,
  const_monomial term
) const {
  size_t reducer = monoLookup().regularReducer(sig, term);
#ifdef MATHICGB_SLOW_DEBUG
  const size_t debugValue = regularReducerSlow(sig, term);
  if (reducer == static_cast<size_t>(-1)) {
    MATHICGB_SLOW_ASSERT(debugValue == static_cast<size_t>(-1));
  } else {
    MATHICGB_SLOW_ASSERT(debugValue != static_cast<size_t>(-1));
    monomial m = ring().allocMonomial();
    MATHICGB_SLOW_ASSERT
      (ring().monomialIsDivisibleBy(term, leadMono(reducer)));
    ring().monomialDivide(term, leadMono(reducer), m);
    ring().monomialMultTo(m, signature(reducer));
    MATHICGB_SLOW_ASSERT(monoid().lessThan(m, sig));
    ring().freeMonomial(m);
  }
#endif
  return reducer;
}

size_t SigPolyBasis::regularReducerSlow(
  const_monomial sig,
  const_monomial term
) const {
  monomial m = ring().allocMonomial();
  const size_t stop = size();
  for (size_t be = 0; be < stop; ++be) {
    if (!monoid().divides(leadMono(be), term))
      continue;
    monoid().divide(leadMono(be), term, m);
    monoid().multiplyInPlace(signature(be), m);
    if (monoid().lessThan(m, sig)) {
      ring().freeMonomial(m);
      return be;
    }
  }
  ring().freeMonomial(m);
  return static_cast<size_t>(-1);
}

void SigPolyBasis::lowBaseDivisors(
  std::vector<size_t>& divisors,
  size_t maxDivisors,
  size_t newGenerator) const
{
  MATHICGB_ASSERT(newGenerator < size());
  const auto sigNew = signature(newGenerator);
  const auto component = monoid().component(sigNew);
  mSignatureLookup[component]->
    lowBaseDivisors(divisors, maxDivisors, newGenerator);
#ifdef MATHICGB_DEBUG
  std::vector<size_t> debugValue;
  lowBaseDivisorsSlow(debugValue, maxDivisors, newGenerator);
  MATHICGB_ASSERT(divisors.size() <= maxDivisors);
  MATHICGB_ASSERT(debugValue.size() == divisors.size());
  for (size_t i = 0; i < divisors.size(); ++i) {
    MATHICGB_ASSERT(ratioCompare(debugValue[i], divisors[i]) == EQ);
  }
#endif
}

void SigPolyBasis::lowBaseDivisorsSlow(
  std::vector<size_t>& divisors,
  size_t maxDivisors,
  size_t newGenerator) const
{
  MATHICGB_ASSERT(newGenerator < size());

  divisors.clear();
  divisors.reserve(maxDivisors + 1);

  auto sigNew = signature(newGenerator);
  for (size_t i = 0; i < newGenerator; ++i) {
    auto sigi = signature(i);

    if (monoid().component(sigi) != monoid().component(sigNew))
      continue;
    if (!monoid().divides(sigi, sigNew))
      continue;
    for (size_t j = 0; j <= divisors.size(); ++j) {
      if (j == divisors.size()) {
        divisors.push_back(i);
        break;
      }
      if (ratioCompare(i, divisors[j]) == GT) {
        divisors.insert(divisors.begin() + j, i);
        break;
      }
    }
    if (divisors.size() > maxDivisors)
      divisors.pop_back();
    MATHICGB_ASSERT(divisors.size() <= maxDivisors);
  }
  MATHICGB_ASSERT(divisors.size() <= maxDivisors);
}

size_t SigPolyBasis::highBaseDivisor(size_t newGenerator) const {
  MATHICGB_ASSERT(newGenerator < size());
  size_t highDivisor = monoLookup().highBaseDivisor(newGenerator);
#ifdef MATHICGB_DEBUG
  size_t debugValue = highBaseDivisorSlow(newGenerator);
  MATHICGB_ASSERT((highDivisor == static_cast<size_t>(-1)) ==
    (debugValue == static_cast<size_t>(-1)));
  MATHICGB_ASSERT(highDivisor == static_cast<size_t>(-1) ||
    ratioCompare(debugValue, highDivisor) == EQ);
#endif
  return highDivisor;
}

size_t SigPolyBasis::highBaseDivisorSlow(size_t newGenerator) const {
  MATHICGB_ASSERT(newGenerator < size());

  auto highDivisor = static_cast<size_t>(-1);
  auto leadNew = leadMono(newGenerator);
  for (size_t i = 0; i < newGenerator; ++i) {
    // continue if this generator would not be an improvement
    // even if it does divide. This is a faster check than
    // checking divisiblity, so do it first.
    if (highDivisor != static_cast<size_t>(-1) &&
      ratioCompare(highDivisor, i) == LT)
      continue;
    auto leadi = leadMono(i);
    if (monoid().divides(leadi, leadNew))
      highDivisor = i;
  }
  return highDivisor;
}

size_t SigPolyBasis::minimalLeadInSig(const_monomial sig) const {
  MATHICGB_ASSERT(! sig.isNull() );
  const size_t component = ring().monomialGetComponent(sig);
  const size_t minLeadGen = mSignatureLookup[component]->minimalLeadInSig(sig);
  MATHICGB_ASSERT(minLeadGen == minimalLeadInSigSlow(sig));
  return minLeadGen;
}

size_t SigPolyBasis::minimalLeadInSigSlow(const_monomial sig) const {
  monomial multiplier = ring().allocMonomial();
  monomial minLead = ring().allocMonomial();

  size_t minLeadGen = static_cast<size_t>(-1);
  const int sigComponent = ring().monomialGetComponent(sig);
  const size_t genCount = size();
  for (size_t gen = 0; gen < genCount; ++gen) {
    if (monoid().component(signature(gen)) != sigComponent)
      continue;
    if (!monoid().divides(signature(gen), sig))
      continue;
    monoid().divide(signature(gen), sig, multiplier);
    if (minLeadGen != static_cast<size_t>(-1)) {
      auto genLead = leadMono(gen);
      const auto leadCmp = monoid().compare(minLead, multiplier, genLead);
      if (leadCmp == Monoid::LessThan)
        continue;
      if (leadCmp == Monoid::EqualTo) {
        // If same lead monomial in signature, pick the one with fewer terms
        // as that one might be less effort to reduce.
        const size_t minTerms = poly(minLeadGen).nTerms();
        const size_t terms = poly(gen).nTerms();
        if (minTerms > terms)
          continue;
        if (minTerms == terms) {
          // If same number of terms, pick the one with larger signature
          // before being multiplied into the same signature. That one
          // might be more reduced as the constraint on regular reduction
          // is less.
          const auto minSig = signature(minLeadGen);
          const auto genSig = signature(gen);
          const auto sigCmp = monoid().compare(minSig, genSig);

          // no two generators have same signature
          MATHICGB_ASSERT(sigCmp != Monoid::EqualTo);
          if (sigCmp == GT)
            continue;
        }
      }
    }

    minLeadGen = gen;
    monoid().multiply(multiplier, leadMono(gen), minLead);
  }
  ring().freeMonomial(multiplier);
  ring().freeMonomial(minLead);
  return minLeadGen;
}

bool SigPolyBasis::isSingularTopReducibleSlow
(const Poly& poly, const_monomial sig) const {
  MATHICGB_ASSERT( ! sig.isNull() );
  if (poly.isZero())
    return false;

  monomial multiplier = ring().allocMonomial();
  const size_t genCount = size();
  const_monomial polyLead = poly.getLeadMonomial();
  for (size_t i = 0; i < genCount; ++i) {
    if (!monoid().divides(leadMono(i), polyLead))
      continue;
    monoid().divide(leadMono(i), polyLead, multiplier);
    if (monoid().compare(sig, multiplier, signature(i)) == EQ)
      return true;
  }
  ring().freeMonomial(multiplier);
  return false;
}

void SigPolyBasis::display(std::ostream& o) const {
  for (size_t i = 0; i < mBasis.size(); i++) {
    o << i << " ";
    if (!mSignatures[i].isNull())
      ring().monomialDisplay(o, mSignatures[i]);
    if (!mBasis.retired(i)) {
      o << "  ";
      mBasis.poly(i).display(o, false); // don't display component
    }
    o << std::endl;
  }
}

void SigPolyBasis::displayFancy
  (std::ostream& out, const Processor& processor) const
{
  mathic::ColumnPrinter pr;
  auto& indexOut = pr.addColumn(false) << "Index\n";
  auto& sigOut = pr.addColumn(false) << "sig\n";
  auto& polyOut = pr.addColumn() << "poly\n";
  pr.repeatToEndOfLine('-');

  auto sig = monoid().alloc();
  for (size_t i = 0; i < mBasis.size(); i++) {
    indexOut << i << '\n';

    if (!mSignatures[i].isNull()) {
      monoid().copy(mSignatures[i], sig);
      processor.postprocess(sig);
      ring().monomialDisplay(sigOut, Monoid::toOld(*sig.ptr()));
    }
    sigOut << '\n';

    if (mBasis.retired(i))
      polyOut << "retired";
    else
      mBasis.poly(i).display(polyOut, false);
    polyOut << '\n';
  }
  out << pr;
}

void SigPolyBasis::postprocess(const MonoProcessor<PolyRing::Monoid>& processor) {
  for (size_t i = 0; i < mSignatures.size(); ++i)
    processor.postprocess(mSignatures[i]);
}

size_t SigPolyBasis::getMemoryUse() const
{
  // Note: we do not count the signatures as they are counted elsewhere.
  size_t total = 0;
  total += mBasis.getMemoryUse();
  total += mSignatures.capacity() * sizeof(mSignatures.front());
  total += mSigLeadRatio.capacity() * sizeof(mSigLeadRatio.front());
  total += mRatioRanks.capacity() * sizeof(mRatioRanks.front());
  total += monoLookup().getMemoryUse();
  total += mMinimalMonoLookup->getMemoryUse();

  // This is an estimate of how much memory mRatioSorted uses per item.
  // It is based on assuming a tree representation with a left pointer,
  // a right pointer and a data member for each node. This is probably
  // an underestimate.
  const size_t perItemOverhead =
    2 * sizeof(void*) + sizeof(*mRatioSorted.begin());
  total += mRatioSorted.size() * perItemOverhead;

  return total;
}

size_t SigPolyBasis::ratioRank(const_monomial ratio) const {
  MATHICGB_ASSERT(mUseRatioRank);
  const size_t index = size();
  if (index == 0)
    return 0; // any value will do as there is nothing to compare to
  auto& sigLeadRatioNonConst =
    const_cast<std::vector<monomial>&>(mSigLeadRatio);

  sigLeadRatioNonConst.push_back(ratio.castAwayConst());
  RatioSortedType::iterator pos = mRatioSorted.lower_bound(index);
  sigLeadRatioNonConst.pop_back();

  if (pos == mRatioSorted.end()) {
    MATHICGB_ASSERT(ratioRank(*mRatioSorted.rbegin()) <
      std::numeric_limits<Rank>::max());
    return std::numeric_limits<Rank>::max();
  } else {
    if (monoid().equal(ratio, sigLeadRatio(*pos)))
      return ratioRank(*pos);
    MATHICGB_ASSERT(ratioRank(*pos) > 0);
#ifdef MATHICGB_DEBUG
    if (pos != mRatioSorted.begin()) {
      RatioSortedType::iterator prev = pos;
      --prev;
      MATHICGB_ASSERT(ratioRank(*pos) - 1 > ratioRank(*prev));
    }
#endif
    return ratioRank(*pos) - 1;
  }
}

SigPolyBasis::StoredRatioCmp::StoredRatioCmp(
  const_monomial numerator,
  const_monomial denominator,
  const SigPolyBasis& basis):
  mBasis(basis)
{
  const PolyRing& ring = basis.ring();
  mRatio = ring.allocMonomial();
  ring.monomialDivideToNegative(numerator, denominator, mRatio);

  if (SigPolyBasis::mUseRatioRank) {
    mRatioRank = basis.ratioRank(mRatio);
    mTmp = 0;
  } else
    mTmp = mBasis.ring().allocMonomial();
}

SigPolyBasis::StoredRatioCmp::~StoredRatioCmp() {
  mBasis.ring().freeMonomial(mRatio);
  if (!SigPolyBasis::mUseRatioRank)
    mBasis.ring().freeMonomial(mTmp);
}

MATHICGB_NAMESPACE_END
