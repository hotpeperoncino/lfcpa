
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/CallSite.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"


using namespace llvm;



// llvmAAmock for llvm 3.9

#include "LivenessBased.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include <stdio.h>

using namespace llvm;

cl::opt<bool> DumpDebugInfo("dump-debug", cl::desc("Dump debug info into stderr"), cl::init(false), cl::Hidden);
cl::opt<bool> DumpResultInfo("dump-result", cl::desc("Dump result info into stderr"), cl::init(false), cl::Hidden);
cl::opt<bool> DumpConstraintInfo("dump-cons", cl::desc("Dump constraint info into stderr"), cl::init(false), cl::Hidden);

LivenessBased::LivenessBased(){
  printf("not supported\n");
}

LivenessBased::LivenessBased(const Module& module)
{
	run(module);
}


bool LivenessBased::run(const Module &M)
{
	return false;
}


bool LivenessBased::run(const Function &F)
{
	return false;
}



    bool LivenessBased::areAllSubNodes(const std::set<PointsToNode *> A, const std::set<PointsToNode *> B) {
        for (auto N : A)
            for (auto M : B)
                if (!N->isSubNodeOf(M))
                    return false;
        return true;
    }

    AliasResult LivenessBased::getResult(const MemoryLocation &LocA,
                          const MemoryLocation &LocB) {
        if (LocA.Size == 0 || LocB.Size == 0)
            return NoAlias;

        // Pointer casts (including GEPs with indices that are all zero) do not
        // affect what is pointed to.
        const Value *A = LocA.Ptr->stripPointerCasts();
        const Value *B = LocB.Ptr->stripPointerCasts();

        // Some preliminary (and very fast!) checks.
        if (!A->getType()->isPointerTy() || !B->getType()->isPointerTy())
            return NoAlias;
        if (A == B)
            return MustAlias;
        if (isa<UndefValue>(A) || isa<UndefValue>(B)) {
            // We don't know what undef points to, but we are allowed to assume
            // that it doesn't alias with anything.
            return NoAlias;
        }

        bool allowMustAlias = true;
        std::set<PointsToNode *> ASet = analysis.getPointsToSet(A, allowMustAlias);
        std::set<PointsToNode *> BSet = analysis.getPointsToSet(B, allowMustAlias);

        // If either of the sets are empty, then we don't know what one of the
        // values can point to, and therefore we don't know if they can alias.
        if (ASet.empty() || BSet.empty())
            return MayAlias;

        std::pair<const PointsToNode *, SmallVector<uint64_t, 4>> address;
        bool possibleMustAlias = allowMustAlias, foundAddress = false;
        for (PointsToNode *N : ASet) {
            if (possibleMustAlias) {
                auto currentAddress = N->getAddress();
                if (foundAddress && address != currentAddress)
                    possibleMustAlias = false;
                else if (!foundAddress) {
                    foundAddress = true;
                    address = currentAddress;
                }
            }
        }
        for (PointsToNode *N : BSet) {
            if (possibleMustAlias) {
                auto currentAddress = N->getAddress();
                // ASet contains at least one element.
                assert(foundAddress);
                if (address != currentAddress)
                    possibleMustAlias = false;
            }
        }

        if (possibleMustAlias) {
            // This happens when ASet and BSet each contain exactly one node,
            // and that node is the same (mod trailing zeros).
            return MustAlias;
        }

        if (allowMustAlias) {
            // If all of the nodes in one set are subnodes of all of the nodes in
            // the other, then they partially alias.
            if (areAllSubNodes(ASet, BSet))
                return PartialAlias;
            if (areAllSubNodes(BSet, ASet))
                return PartialAlias;
        }

        for (PointsToNode *N : ASet)
            for (PointsToNode *M : BSet)
                if (M->isSubNodeOf(N) || N->isSubNodeOf(M))
                    return MayAlias;

        // If the values do not share any pointees then they cannot alias.
        return NoAlias;
    }
