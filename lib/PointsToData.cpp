#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"

#include "../include/PointsToData.h"

void dumpPointsToRelation(const PointsToRelation *R) {
    bool first = true;
    for (auto P : *R) {
        if (!first)
            errs() << ", ";
        first = false;
        errs() << P.first->getName() << "-->" << P.second->getName();
    }
    errs() << "\n";
}

void dumpLivenessSet(const LivenessSet *L) {
    bool first = true;
    for (auto N : *L) {
        if (!first)
            errs() << ", ";
        first = false;
        errs() << N->getName();
    }
    errs() << "\n";
}

ProcedurePointsTo *PointsToData::getAtFunction(Function *F) const {
    auto result = data.find(F);
    assert (result != data.end() && "The points-to data does not contain an entry for the specified function.");
    return data.find(F)->second;
}

bool arePointsToMapsEqual(Function *F, IntraproceduralPointsTo *a, IntraproceduralPointsTo *b) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        auto p1 = a->find(&*I), p2 = b->find(&*I);
        assert (p1 != a -> end() && p2 != b -> end() && "Invalid points-to relations");
        LivenessSet *l1 = p1->second.first, *l2 = p2->second.first;
        if (*l1 != *l2)
            return false;
        PointsToRelation *r1 = p1->second.second, *r2 = p2->second.second;
        if (*r1 != *r2)
            return false;
    }
    return true;
}

IntraproceduralPointsTo *copyPointsToMap(IntraproceduralPointsTo *M) {
    IntraproceduralPointsTo *Result = new IntraproceduralPointsTo();
    for (auto P : *M) {
        LivenessSet *L = new LivenessSet(*P.second.first);
        PointsToRelation *R = new PointsToRelation(*P.second.second);
        Result->insert(std::make_pair(P.first, std::make_pair(L, R)));
    }
    return Result;
}

IntraproceduralPointsTo *PointsToData::getPointsTo(const CallString &CS, Function *F) {
    assert (!CS.isRecursive() && "Information has already been computed.");

    auto P = data.find(F);
    ProcedurePointsTo *Pointsto;
    if (P == data.end()) {
        Pointsto = new ProcedurePointsTo();
        data.insert(std::make_pair(F, Pointsto));
    }
    else
        Pointsto = P->second;

    for (auto P2 : *Pointsto) {
        assert ((!P2.first.isRecursive() || !P2.first.matches(CS)) && "Information has already been computed");
        if (CS == P2.first)
            return P2.second;
    }

    // The call string wasn't found.
    IntraproceduralPointsTo *Out = new IntraproceduralPointsTo();
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
        Out->insert(std::make_pair(&*I, std::make_pair(new LivenessSet(), new PointsToRelation())));
    Pointsto->push_back(std::make_pair(CS, Out));
    return Out;
}

bool PointsToData::attemptMakeRecursiveCallString(Function *F, const CallString &CS, IntraproceduralPointsTo *Out) {
    assert(!CS.isRecursive() && "The call string must be non-recursive");

    auto pair = data.find(F);
    assert(pair != data.end());

    ProcedurePointsTo *V = pair->second;
    // If the set already contains a pair of a call string s such that
    // CS = S . S' and a points to map which matches Out, then the call
    // string in the pair can be replaced with S . S'*, since adding an
    // extra S' to the end does not change the points to map.
    auto I = V->begin(), E = V->end();
    for (; I != E; ++I) {
        auto pair = *I;
        if (!pair.first.isRecursive() &&
            CS.isNonRecursivePrefix(pair.first) &&
            arePointsToMapsEqual(F, pair.second, Out)) {
            CallString newCS = CS.createRecursiveFromPrefix(pair.first);
            *I = std::make_pair(newCS, Out);
            break;
        }
    }

    if (I != E) {
        CallString R = I->first;
        // Remove all call strings that match the inserted one.
        for (auto I2 = V->begin(); I2 != E; ) {
            if (I != I2 && !I2->first.isRecursive() && R.matches(I2->first)) {
                I2=V->erase(I2);
                E = V->end();
            }
            else
                ++I2;
        }
        return true;
    }
    else
        return false;
}

bool PointsToData::hasDataForFunction(const Function *F) const {
    auto I = data.find(F);
    if (I == data.end())
        return false;
    return !I->second->empty();
}

IntraproceduralPointsTo *PointsToData::getAtLongestPrefix(const Function *F, const CallString &CS) const {
    assert(!CS.isRecursive() && "We can only check against non-recursive call strings.");

    auto I = data.find(F);
    if (I == data.end()) {
        // There is no prefix in the data.
        return nullptr;
    }

    ProcedurePointsTo *V = I->second;
    IntraproceduralPointsTo *Result = nullptr;
    int prefixLength = 0;
    for (auto P : *V) {
        if (P.first.isRecursive()) {
            // We only consider exact matches here.
            if (P.first.matches(CS))
                return P.second;
        }
        else {
            if (P.first.matches(CS))
                return P.second;
            else {
                int l = P.first.size();
                if (l >= prefixLength && CS.isNonRecursivePrefix(P.first)) {
                    prefixLength = l;
                    Result = P.second;
                }
            }
        }
    }

    return Result;
}