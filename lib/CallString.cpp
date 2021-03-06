#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include "CallString.h"

CallString CallString::empty() {
    return CallString();
}

CallString CallString::addCallSite(const Instruction *I) const {
    CallString result = *this;
    result.nonCyclic.push_back(I);
    return result;
}

bool CallString::isNonCyclicPrefix(const CallString &S) const {
    auto thisIter = nonCyclic.begin();
    auto thisEnd = nonCyclic.end();
    for (auto &I : S.nonCyclic) {
        if (thisIter == thisEnd) {
            // S is longer!
            return false;
        }
        else if (I != *thisIter) {
            // S contains a different element.
            return false;
        }
        else
            thisIter++;
    }
    return thisIter != thisEnd;
}

CallString CallString::createCyclicFromPrefix(const CallString &S) const {
    auto thisIter = nonCyclic.begin();
    auto thisEnd = nonCyclic.end();
    for (auto &I : S.nonCyclic) {
        thisIter++;
        // Supress unused variable warning in non-asserts builds.
        (void)I;
    }

    CallString result = S;
    result.cyclic = SmallVector<const Instruction *, 8>(thisIter, thisEnd);
    return result;
}

bool CallString::matches(const CallString &S) const {

    auto iter = S.nonCyclic.begin();
    auto end = S.nonCyclic.end();
    for (auto &I : nonCyclic) {
        if (iter == end) {
            // S isn't long enough!
            return false;
        }
        else if (I != *iter) {
            // S contains a different element.
            return false;
        }
        else
            iter++;
    }

    if (isCyclic()) {
        while (iter != end) {
            for (auto &I : cyclic) {
                if (iter == end) {
                    // S has an incomplete cyclic part at the end.
                    return false;
                }
                else if (I != *iter) {
                    // S contains a different element.
                    return false;
                }
                else
                    iter++;
            }
        }

        return true;
    }
    else
        return iter == end;
}

void CallString::dump() const {
    bool first = true;
    for (auto &I : nonCyclic) {
        if (!first)
            errs() << ", ";
        I->print(errs());
        first = false;
    }

    if (isCyclic()) {
        if (!first)
            errs() << ", ";
        errs() << "[";
        first = true;
        for (auto &I : cyclic) {
            if (!first)
                errs() << ", ";
            I->print(errs());
            first = false;
        }
        errs() << "]*";
    }

    errs() << "\n";
}

