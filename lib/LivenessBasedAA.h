// LivenessBased for llvm 3.9

#ifndef LLVMAAMOCKAA_H
#define LLVMAAMOCKAA_H

#include "LivenessBased.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Pass.h"


class LivenessBasedAAResult : public llvm::AAResultBase<LivenessBasedAAResult> {
private:
    friend llvm::AAResultBase<LivenessBasedAAResult>;

    
    //    llvm::AliasResult LivenessBasedAAResult::mockAlias(const Value* v1, const Value* v2);

public:
    LivenessBased lb;

    LivenessBasedAAResult(const llvm::Module&);
    LivenessBasedAAResult(const llvm::Function&);

    llvm::AliasResult alias(const llvm::MemoryLocation&,
                            const llvm::MemoryLocation&);
};

class LivenessBasedAAWrapperPass : public llvm::ModulePass {
private:
    std::unique_ptr<LivenessBasedAAResult> result;

public:
    static char ID;

    LivenessBasedAAWrapperPass();

    LivenessBasedAAResult& getResult() { return *result; }
    const LivenessBasedAAResult& getResult() const { return *result; }

    bool runOnModule(llvm::Module&) override;
    //    bool doFinalization(llvm::Module&) override;
    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
};


class LivenessBasedAAFPWrapperPass : public llvm::FunctionPass {
private:
    std::unique_ptr<LivenessBasedAAResult> result;

public:
    static char ID;

    LivenessBasedAAFPWrapperPass();

    LivenessBasedAAResult& getResult() { return *result; }
    const LivenessBasedAAResult& getResult() const { return *result; }

    bool runOnFunction(llvm::Function&) override;
    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
};

#endif
