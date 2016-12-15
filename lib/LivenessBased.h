// LivenessBased for llvm 3.9

#ifndef LIVENESSBASED_H
#define LIVENESSBASED_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/CallSite.h"
#include "llvm/ADT/DenseMap.h"

#include <vector>
#include <set>

#include "LivenessPointsTo.h"

class LivenessBased
{
public:
	static char ID;
	LivenessBased();
	LivenessBased(const llvm::Module&);
	bool run(const llvm::Module& M);
	bool run(const llvm::Function& F);

	LivenessPointsTo analysis;

	friend class LivenessBasedAAResult;

	bool areAllSubNodes(const std::set<PointsToNode *> A, const std::set<PointsToNode *> B) ;
	llvm::AliasResult getResult(const MemoryLocation &LocA,
					 const MemoryLocation &LocB);
};

#endif
