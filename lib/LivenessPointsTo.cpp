#include <set>

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include "../include/LivenessPointsToMisc.h"
#include "../include/LivenessPointsTo.h"
#include "../include/PointsToData.h"
#include "../include/PointsToNode.h"

void LivenessPointsTo::subtractKill(std::set<PointsToNode *> &Lin,
                                    Instruction *I,
                                    PointsToRelation *Ain) {
    Lin.erase(factory.getNode(I));
    if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *Ptr = SI->getPointerOperand();
        PointsToNode *PtrNode = factory.getNode(Ptr);

        bool moreThanOne = false;
        PointsToNode *PointedTo = nullptr;

        for (auto P : *Ain) {
            if (P.first == PtrNode) {
                if (PointedTo == nullptr)
                    PointedTo = P.second;
                else {
                    moreThanOne = true;
                    break;
                }
            }
        }

        if (!moreThanOne) {
            if (PointedTo == nullptr || PointedTo == factory.getUnknown()) {
                // We have no information about what Ptr can point to, so kill
                // everything.
                Lin.clear();
            }
            else {
                // Ptr must point to PointedTo, so we can do a strong update
                // here.
                Lin.erase(PointedTo);
            }
        }

        // If there is more than one value that is possibly pointed to by Ptr,
        // then we need to perform a weak update, so we don't kill anything
        // else.
    }
}

void LivenessPointsTo::unionRef(std::set<PointsToNode *>& Lin,
                                Instruction *I,
                                std::set<PointsToNode *>* Lout,
                                PointsToRelation* Ain) {
    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        // We only consider the pointer and the possible values in memory to be
        // ref'd if the load is live.
        if (Lout->find(factory.getNode(I)) != Lout->end()) {
            Value *Ptr = LI->getPointerOperand();
            PointsToNode *PtrNode = factory.getNode(Ptr);
            Lin.insert(PtrNode);
            for (auto &P : *Ain)
                if (P.first == PtrNode)
                    Lin.insert(P.second);
        }
    }
    else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *Ptr = SI->getPointerOperand();
        PointsToNode *PtrNode = factory.getNode(Ptr);
        Lin.insert(PtrNode);

        // We only consider the stored value to be ref'd if at least one of the
        // values that can be pointed to by x is live.
        for (auto P : *Ain) {
            if (P.first == PtrNode && Lout->find(P.second) != Lout->end()) {
                Lin.insert(factory.getNode(SI->getValueOperand()));
                break;
            }
        }
    }
    else if (isa<PHINode>(I) || isa<SelectInst>(I)) {
        // We only consider the operands of a PHI node or select instruction to
        // be ref'd if I is live.
        if (Lout->find(factory.getNode(I)) != Lout->end()) {
            for (Use &U : I->operands())
                if (Value *Operand = dyn_cast<Value>(U))
                    Lin.insert(factory.getNode(Operand));
        }
    }
    else {
        // If the instruction is not a load or a store, we consider all of it's
        // operands to be ref'd, even if the instruction is not live.
        for (Use &U : I->operands())
            if (Value *Operand = dyn_cast<Value>(U))
                Lin.insert(factory.getNode(Operand));
    }
}

void LivenessPointsTo::unionRelationRestriction(PointsToRelation &Result,
                                                PointsToRelation *Aout,
                                                std::set<PointsToNode *> *Lin) {
    for (auto &P : *Aout)
        if (Lin->find(P.first) != Lin->end())
            Result.insert(P);
}

std::set<PointsToNode *>
LivenessPointsTo::getRestrictedDef(Instruction *I,
                                   PointsToRelation *Ain,
                                   std::set<PointsToNode *> *Lout) {
    std::set<PointsToNode *> s;
    if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *Ptr = SI->getPointerOperand();
        PointsToNode *PtrNode = factory.getNode(Ptr);

        for (auto &P : *Ain)
            if (P.first == PtrNode && Lout->find(P.second) != Lout->end())
                s.insert(P.second);
    }
    else {
        switch (I->getOpcode()) {
            case Instruction::Alloca:
            case Instruction::Load:
            case Instruction::PHI:
            case Instruction::Select:
                {
                    PointsToNode *N = factory.getNode(I);
                    if (Lout->find(N) != Lout->end())
                        s.insert(N);
                }
                break;
            default:
                break;
        }
    }
    return s;
}

void LivenessPointsTo::insertPointedToBy(std::set<PointsToNode *> &S,
                                         Value *V,
                                         PointsToRelation *Ain) {
    PointsToNode *VNode = factory.getNode(V);
    for (auto &P : *Ain)
        if (P.first == VNode)
            S.insert(P.second);
}

std::set<PointsToNode *> LivenessPointsTo::getPointee(Instruction *I,
                                                      PointsToRelation *Ain) {
    std::set<PointsToNode *> s;
    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        Value *Ptr = LI->getPointerOperand();
        PointsToNode *PtrNode = factory.getNode(Ptr);
        std::set<PointsToNode *> t;
        for (auto &P : *Ain)
            if (P.first == PtrNode)
                t.insert(P.second);
        for (auto &P : *Ain)
            if (t.find(P.first) != t.end())
                s.insert(P.second);
    }
    else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *V = SI->getValueOperand();
        insertPointedToBy(s, V, Ain);
    }
    else if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
        // If the instruction is an alloca, then we consider the pointer to be
        // to a special location which does not correspond to any Value and is
        // disjoint from all other locations.
        s.insert(factory.getAllocaNode(AI));
    }
    else if (PHINode *Phi = dyn_cast<PHINode>(I)) {
        // The result of the phi can point to anything that an operand of the
        // phi can point to.
        for (auto &V : Phi->incoming_values())
            insertPointedToBy(s, V, Ain);
    }
    else if (SelectInst *SI = dyn_cast<SelectInst>(I)) {
        // The result of the select can point to anything that one of the
        // selected values can point to.
        insertPointedToBy(s, SI->getTrueValue(), Ain);
        insertPointedToBy(s, SI->getFalseValue(), Ain);
    }
    return s;
}

void LivenessPointsTo::unionCrossProduct(PointsToRelation &Result,
                                         std::set<PointsToNode *> &A,
                                         std::set<PointsToNode *> &B) {
    for (auto &X : A)
        for (auto &Y : B)
            Result.insert(std::make_pair(X, Y));
}

ProcedurePointsTo *LivenessPointsTo::getPointsTo(Function &F) const {
    return data.getAtFunction(&F);
}

bool hasPointee(PointsToRelation &S, PointsToNode *N) {
    for (auto &P : S)
        if (P.first == N)
            return true;

    return false;
}

void LivenessPointsTo::computeLout(Instruction *I, LivenessSet* Lout, IntraproceduralPointsTo *Result, PointsToRelation *Aout, bool insertGlobals) {
    if (ReturnInst *RI = dyn_cast<ReturnInst>(I)) {
        // For return instructions, if ExitLiveness was not nullptr, lout is
        // exactly ExitLiveness; it is initialized to this, so don't change it
        // here in this case. If it was nullptr, lout is the globals, return
        // value, and any anything which is reachable from them; these need to
        // be updated in this case.
        if (insertGlobals) {
            std::function<void(PointsToNode *)> insertReachable = [&](PointsToNode *N) {
                if (Lout->insert(N).second) {
                    for (auto P : *Aout)
                        if (P.first == N)
                            insertReachable(P.second);
                }
            };

            Lout->clear();
            for (auto I : globals)
                insertReachable(I);
            if (RI->getReturnValue() != nullptr)
                insertReachable(factory.getNode(RI->getReturnValue()));
        }
    }
    else if (TerminatorInst *TI = dyn_cast<TerminatorInst>(I)) {
        // If this instruction is a terminator, it may have multiple
        // successors.
        Lout->clear();
        for (unsigned i = 0; i < TI->getNumSuccessors(); i++) {
            Instruction *Succ = TI->getSuccessor(i)->begin();
            auto succ_result = Result->find(Succ);
            assert(succ_result != Result->end());
            auto succ_lin = succ_result->second.first;
            Lout->insert(succ_lin->begin(), succ_lin->end());
        }
    }
    else {
        // If this instruction is not a terminator, it has exactly one
        // successor -- the next instruction in the function.
        Instruction *Succ = getNextInstruction(I);
        auto succ_result = Result->find(Succ);
        assert(succ_result != Result->end());
        auto succ_lin = succ_result->second.first;
        if (*succ_lin != *Lout) {
            Lout->clear();
            Lout->insert(succ_lin->begin(), succ_lin->end());
        }
    }
}

bool LivenessPointsTo::computeAin(Instruction *I, Function *F, PointsToRelation *Ain, LivenessSet *Lin, IntraproceduralPointsTo *Result) {
    // Compute ain for the current instruction.
    PointsToRelation s;
    if (I == &*inst_begin(F)) {
        // If this is the first instruction of the function, then apart from
        // the data in entry, we don't know what anything points to ain
        // already contains the data in entry, so add the remaining pairs.
        s = *Ain;
        for (PointsToNode *N : *Lin) {
            if (!hasPointee(s, N)) {
                std::pair<PointsToNode *, PointsToNode *> p =
                    std::make_pair(N, factory.getUnknown());
                s.insert(p);
            }
        }
    }
    else {
        // If this is not the first instruction, then the points to
        // information from the predecessors can be propagated forwards.
        BasicBlock *BB = I->getParent();
        Instruction *FirstInBB = BB->begin();
        if (FirstInBB == I) {
            for (pred_iterator PI = pred_begin(BB), E = pred_end(BB);
                 PI != E;
                 ++PI) {
                BasicBlock *PredBB = *PI;
                Instruction *Pred = --(PredBB->end());
                auto pred_result = Result->find(Pred);
                assert(pred_result != Result->end());
                PointsToRelation *PredAout = pred_result->second.second;
                unionRelationRestriction(s, PredAout, Lin);
            }
        }
        else {
            Instruction *Pred = getPreviousInstruction(I);
            auto pred_result = Result->find(Pred);
            assert(pred_result != Result->end());
            PointsToRelation *PredAout = pred_result->second.second;
            unionRelationRestriction(s, PredAout, Lin);
        }
    }
    if (s != *Ain) {
        Ain->clear();
        Ain->insert(s.begin(), s.end());
        return true;
    }

    return false;
}

std::pair<PointsToRelation *, LivenessSet *> LivenessPointsTo::getReachable(Function *Callee, CallInst *CI, PointsToRelation *Ain, LivenessSet *Lout) {
    // This is roughly the mark phase from mark-and-sweep garbage collection. We
    // begin with the roots, which are the arguments of the function and global
    // variables, then determine what is reachable using the points-to relation.
    LivenessSet reachable;

    std::function<void(PointsToNode *)> insertReachable = [&](PointsToNode *N) {
        if (reachable.insert(N).second) {
            for (auto P : *Ain)
                if (P.first == N)
                    insertReachable(P.second);
        }
    };

    PointsToRelation *PT = new PointsToRelation();
    LivenessSet *L = new LivenessSet();

    // We need to add formal arguments to the result, on only add things that
    // the actual arguments can point to to reachable, and add formal arguments
    // immediately.
    auto Arg = Callee->arg_begin(), ArgEnd = Callee->arg_end();
    for (Value *V : CI->arg_operands()) {
        PointsToNode *N = factory.getNode(V);
        // FIXME: What about varargs functions?
        assert(Arg != ArgEnd && "Argument count mismatch");
        Argument *A = &*Arg;
        PointsToNode *ANode = factory.getNode(A);
        for (auto P : *Ain) {
            if (P.first == N) {
                insertReachable(P.second);
                PT->insert(std::make_pair(ANode, P.second));
            }
        }
        if (Lout->find(N) != Lout->end())
            L->insert(ANode);
        ++Arg;
    }

    // Then add all of the relevant globals.
    for (auto P : *Ain)
        if (P.first->isGlobal())
            insertReachable(P.first);
    for (auto N : *Lout)
        if (N->isGlobal())
            insertReachable(N);

    for (auto P : *Ain)
        if (reachable.find(P.first) != reachable.end())
            PT->insert(P);
    for (auto N : *Lout)
        if (reachable.find(N) != reachable.end())
            L->insert(N);

    return std::make_pair(PT, L);
}

void LivenessPointsTo::insertReachable(Function *Callee, CallInst *CI, LivenessSet &N, LivenessSet &Lin, PointsToRelation *Ain) {
    // FIXME: Do we need to look at return values here?
    // This is roughly the mark phase from mark-and-sweep garbage collection. We
    // begin with the roots, which are the arguments of the function and global
    // variables, then determine what is reachable using the points-to relation.
    LivenessSet reachable;

    std::function<void(PointsToNode *)> insertReachable = [&](PointsToNode *N) {
        if (reachable.insert(N).second) {
            for (auto P : *Ain)
                if (P.first == N)
                    insertReachable(P.second);
        }
    };

    // If there is a formal attribute that is live at the beginning of the
    // callee, the corresponding actual arguments is live before the call
    // instruction.
    auto Arg = Callee->arg_begin(), ArgEnd = Callee->arg_end();
    for (Value *V : CI->arg_operands()) {
        PointsToNode *Node = factory.getNode(V);
        // FIXME: What about varargs functions?
        assert(Arg != ArgEnd && "Argument count mismatch");
        Argument *A = &*Arg;
        PointsToNode *ANode = factory.getNode(A);
        if (Lin.find(ANode) != Lin.end())
            N.insert(Node);
        for (auto P : *Ain)
            if (P.first == Node)
                insertReachable(P.second);
        ++Arg;
    }
    // Then add all of the relevant globals.
    for (auto P : *Ain)
        if (P.first->isGlobal())
            insertReachable(P.first);
    for (auto N : Lin)
        if (N->isGlobal())
            insertReachable(N);

    // We now determine which live variables are relevant.
    for (auto Node : Lin)
        if (reachable.find(Node) != reachable.end())
            N.insert(Node);
}

void LivenessPointsTo::insertReachableDeclaration(CallInst *CI, LivenessSet &N, PointsToRelation *Ain) {
    // FIXME: Do we need to look at return values here? Can globals be accessed
    // by the function?
    // This is roughly the mark phase from mark-and-sweep garbage collection. We
    // begin with the roots, which are the arguments of the function,  then
    // determine what is reachable using the points-to relation.
    LivenessSet reachable;

    std::function<void(PointsToNode *)> insertReachable = [&](PointsToNode *N) {
        if (reachable.insert(N).second) {
            for (auto P : *Ain)
                if (P.first == N)
                    insertReachable(P.second);
        }
    };

    // Arguments are roots.
    for (Value *V : CI->arg_operands())
        insertReachable(factory.getNode(V));

    N.insert(reachable.begin(), reachable.end());
}

void LivenessPointsTo::insertReachablePT(CallInst *CI, PointsToRelation &N, PointsToRelation &Aout, PointsToRelation *Ain, LivenessSet &ReturnValues) {
    LivenessSet reachable;

    std::function<void(PointsToNode *)> insertReachable = [&](PointsToNode *N) {
        if (reachable.insert(N).second) {
            for (auto P : *Ain)
                if (P.first == N)
                    insertReachable(P.second);
        }
    };

    // We want the result of the call to point to anything that a return value
    // can point to. However, if there is a return value that we don't know what
    // points to, then we don't know anything about what the result can point
    // to.
    bool pointToUnknown = false;
    for (PointsToNode *V : ReturnValues) {
        bool unknown = true;
        for (auto P : Aout) {
            if (P.first == V) {
                if (P.second == factory.getUnknown())
                    pointToUnknown = true;
                else
                    unknown = false;
                break;
            }
        }

        pointToUnknown |= unknown;

        if (pointToUnknown)
            break;
    }

    if (pointToUnknown)
        N.insert(std::make_pair(factory.getNode(CI), factory.getUnknown()));
    else
        for (auto P : Aout)
            if (ReturnValues.find(P.first) != ReturnValues.end())
                N.insert(std::make_pair(factory.getNode(CI), P.second));

    // The function can change it's formal arguments, but not it's actual
    // arguments, since they are passed by value.
    for (Value *V : CI->arg_operands()) {
        PointsToNode *Node = factory.getNode(V);
        for (auto P : *Ain) {
            if (P.first == Node) {
                N.insert(P);
                insertReachable(P.second);
            }
        }
    }

    // Anything that return values can point to are roots.
    for (auto P : Aout)
        if (ReturnValues.find(P.first) != ReturnValues.end())
            insertReachable(P.second);
    // Globals are roots.
    for (auto P : *Ain)
        if (P.first->isGlobal())
            insertReachable(P.first);
    for (auto N : Aout)
        if (N.first->isGlobal())
            insertReachable(N.first);

    // We now determine which pairs are relevant.
    for (auto P : Aout)
        if (reachable.find(P.first) != reachable.end())
            N.insert(P);
}

bool LivenessPointsTo::getCalledFunctionResult(const CallString &CS, Function *F, std::pair<LivenessSet, PointsToRelation>& Result) {
    // If there is an exact match for F and CS in data, then this should be used.
    // Otherwise, we should use the data that is associated with the function
    // and longest possible prefix of the call string. If there is no data for F
    // at all, just return false.

    if (!data.hasDataForFunction(F))
        return false;

    IntraproceduralPointsTo *PT = data.getAtLongestPrefix(F, CS);
    auto I = PT->find(&*inst_begin(F));
    assert(I != PT->end());
    Result.first = *I->second.first;

    // For Aout, we need to union over all of the PointsToRelations associated
    // with ReturnInsts.
    PointsToRelation aout;
    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        Instruction *Inst = &*I;
        if (isa<ReturnInst>(Inst)) {
            auto J = PT->find(Inst);
            assert(J != PT->end());
            aout.insert(J->second.second->begin(), J->second.second->end());
        }
    }
    Result.second = aout;

    return true;
}

LivenessSet LivenessPointsTo::getReturnValues(const Function *F) {
    LivenessSet s;
    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I)
    {
        const Instruction* Inst = &*I;
        if (const ReturnInst *RI = dyn_cast<ReturnInst>(Inst))
            if (RI->getReturnValue() != nullptr)
                s.insert(factory.getNode(RI->getReturnValue()));
    }
    return s;
}

void LivenessPointsTo::runOnFunction(Function *F, const CallString &CS, IntraproceduralPointsTo *Result, PointsToRelation *EntryPointsTo, LivenessSet *ExitLiveness, SmallVector<std::tuple<CallInst *, Function *, PointsToRelation *, LivenessSet *>, 8> &Calls) {
    assert(!F->isDeclaration() && "Can only run on definitions.");

    // The result of the function is lin and aout (since liveness is propagated
    // backwards and points-to forwards); this variable contains lout and ain.
    IntraproceduralPointsTo nonresult;

    LivenessSet *Globals = ExitLiveness == nullptr ? new LivenessSet(globals) : nullptr;

    // Initialize ain, aout, lin and lout for each instruction.
    for (inst_iterator S = inst_begin(F), I = S, E = inst_end(F); I != E; ++I) {
        Instruction *inst = &*I;
        // If the instruction is a ReturnInst, the values that are live after
        // the instruction is executed are exactly those specified in
        // ExitLiveness, if it exists. If it does not exist, then they are the
        // globals and everything the globals can point to. If the instruction
        // is the first in the function, the points-to information before it is
        // executed is exactly that in EntryPointsTo.
        if (isa<ReturnInst>(inst) && I == S)
            nonresult.insert(std::make_pair(inst, std::make_pair(ExitLiveness == nullptr ? Globals : ExitLiveness, EntryPointsTo)));
        else if (isa<ReturnInst>(inst))
            nonresult.insert(std::make_pair(inst, std::make_pair(ExitLiveness == nullptr ? Globals : ExitLiveness, new PointsToRelation())));
        else if (I == S)
            nonresult.insert(std::make_pair(inst, std::make_pair(new LivenessSet(), EntryPointsTo)));
        else
            nonresult.insert(std::make_pair(inst, std::make_pair(new LivenessSet(), new PointsToRelation())));
    }

    // Create and initialize worklist.
    SmallVector<Instruction *, 128> worklist;
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; I++)
        worklist.push_back(&*I);

    // Update points-to and liveness information until it converges.
    while (!worklist.empty()) {
        Instruction *I = worklist.pop_back_val();

        auto instruction_nonresult = nonresult.find(I), instruction_result = Result->find(I);
        assert (instruction_nonresult != nonresult.end());
        assert (instruction_result != Result->end());
        auto instruction_ain = instruction_nonresult->second.second,
             instruction_aout = instruction_result->second.second;
        auto instruction_lin = instruction_result->second.first,
             instruction_lout = instruction_nonresult->second.first;

        bool addPredsToWorklist = false,
             addSuccsToWorklist = false,
             addCurrToWorklist = false;

        computeLout(I, instruction_lout, Result, instruction_aout, ExitLiveness == nullptr);

        if (CallInst *CI = dyn_cast<CallInst>(I)) {
            PointsToNode *CINode = factory.getNode(CI);
            bool isCallInstLive = instruction_lout->find(CINode) != instruction_lout->end();
            CallString newCS = CS.addCallSite(I);
            Function *Called = CI->getCalledFunction();
            assert(Called != nullptr && "Only direct calls are supported.");

            bool analysed = false;
            if (!Called->isDeclaration()) {
                // The set of values that are returned from the function.
                LivenessSet returnValues = getReturnValues(Called);
                // Add to the list of calls made by the function for analysis later.
                auto boundaryInformation = getReachable(Called, CI, instruction_ain, instruction_lout);
                auto EntryPT = boundaryInformation.first;
                auto ExitL = boundaryInformation.second;
                // If the call instruction is live, the values that can be
                // returned by the function are.
                if (isCallInstLive)
                    ExitL->insert(returnValues.begin(), returnValues.end());

                bool found = false;
                for (auto I = Calls.begin(), E = Calls.end(); I != E; ++I) {
                    CallInst *TupleCI;
                    Function *TupleF;
                    PointsToRelation *TuplePT;
                    LivenessSet *TupleL;
                    std::tie(TupleCI, TupleF, TuplePT, TupleL) = *I;
                    if (TupleCI == CI && TupleF == Called) {
                        // Update the call with the new points-to and liveness
                        // information.
                        *I = std::make_tuple(CI, Called, EntryPT, ExitL);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    Calls.push_back(std::make_tuple(CI, Called, EntryPT, ExitL));

                std::pair<LivenessSet, PointsToRelation> calledFunctionResult;
                if (getCalledFunctionResult(newCS, Called, calledFunctionResult)) {
                    auto calledFunctionLin = calledFunctionResult.first;
                    auto calledFunctionAout = calledFunctionResult.second;

                    // The set of nodes that are live after the call executes,
                    // but cannot possibly have been killed by the call.
                    LivenessSet survivesCall;
                    for (auto N : *instruction_lout)
                        if (N != CINode && ExitL->find(N) == ExitL->end())
                            survivesCall.insert(N);

                    // Compute lin for the current instruction. A live variable
                    // here is either used after the call and not accessible
                    // from the called function, or accessible from the called
                    // function and live at the beginning of it.
                    LivenessSet n = survivesCall;
                    insertReachable(Called, CI, n, calledFunctionLin, instruction_ain);
                    // If the two sets are the same, then no changes need to be made to lin,
                    // so don't do anything here. Otherwise, we need to update lin and add
                    // the predecessors of the current instruction to the worklist.
                    if (n != *instruction_lin) {
                        instruction_lin->clear();
                        instruction_lin->insert(n.begin(), n.end());
                        addPredsToWorklist = true;
                    }

                    addCurrToWorklist |= computeAin(I, F, instruction_ain, instruction_lin, Result);

                    // Compute aout for the current instruction. The pairs that
                    // should be included are those in ain that couldn't have
                    // been killed by the call, plus those at the end of the
                    // called instruction.
                    // FIXME: survivesCall can only be used here if the domain
                    // of Ain is a subset of Lin; is this always the case?
                    PointsToRelation s;
                    unionRelationRestriction(s, instruction_ain, &survivesCall);
                    insertReachablePT(CI, s, calledFunctionAout, instruction_ain, returnValues);
                    if (s != *instruction_aout) {
                        instruction_aout->clear();
                        instruction_aout->insert(s.begin(), s.end());
                        addSuccsToWorklist = true;
                    }

                    analysed = true;
                }
            }

            if (!analysed) {
                // We reach this point if we have a declaration or a function that
                // hasn't been analysed yet. Just assume the worst case -- the
                // function may invalidate or use anything that it has access to
                // and that it is allowed to according to the call's attributes.

                // Compute lin for the current instruction. This consists of
                // everything that is live after the call, plus anything that
                // the callee can access, minus the return value.
                LivenessSet n = *instruction_lout;
                insertReachableDeclaration(CI, n, instruction_ain);
                n.erase(CINode);
                // If the two sets are the same, then no changes need to be made to lin,
                // so don't do anything here. Otherwise, we need to update lin and add
                // the predecessors of the current instruction to the worklist.
                if (n != *instruction_lin) {
                    instruction_lin->clear();
                    instruction_lin->insert(n.begin(), n.end());
                    addPredsToWorklist = true;
                }

                addCurrToWorklist |= computeAin(I, F, instruction_ain, instruction_lin, Result);

                // Compute aout for the current instruction. Anything that can
                // be modified by the function (including the return value) must
                // point to unknown; anything else points to the same thing that
                // it does in ain.
                LivenessSet reachable;
                insertReachableDeclaration(CI, reachable, instruction_ain);
                reachable.insert(CINode);
                PointsToRelation s;
                for (PointsToNode *N : reachable)
                    if (instruction_lout->find(N) != instruction_lout->end())
                        s.insert(std::make_pair(N, factory.getUnknown()));
                for (auto P : *instruction_ain)
                    if (reachable.find(P.first) == reachable.end() && instruction_lout->find(P.first) != instruction_lout->end())
                        s.insert(P);

                if (s != *instruction_aout) {
                    instruction_aout->clear();
                    instruction_aout->insert(s.begin(), s.end());
                    addSuccsToWorklist = true;
                }
            }
        }
        else {
            // Compute lin for the current instruction.
            std::set<PointsToNode *> n;
            n.insert(instruction_lout->begin(), instruction_lout->end());
            subtractKill(n, I, instruction_ain);
            unionRef(n, I, instruction_lout, instruction_ain);
            // If the two sets are the same, then no changes need to be made to lin,
            // so don't do anything here. Otherwise, we need to update lin and add
            // the predecessors of the current instruction to the worklist.
            if (n != *instruction_lin) {
                instruction_lin->clear();
                instruction_lin->insert(n.begin(), n.end());
                addPredsToWorklist = true;
            }

            addCurrToWorklist |= computeAin(I, F, instruction_ain, instruction_lin, Result);

            PointsToRelation s;
            // Compute aout for the current instruction.
            std::set<PointsToNode *> notKilled = *instruction_lout;
            subtractKill(notKilled, I, instruction_ain);
            unionRelationRestriction(s, instruction_ain, &notKilled);
            std::set<PointsToNode *> def =
                getRestrictedDef(I, instruction_ain, instruction_lout);
            std::set<PointsToNode *> pointee = getPointee(I, instruction_ain);
            unionCrossProduct(s, def, pointee);
            if (s != *instruction_aout) {
                instruction_aout->clear();
                instruction_aout->insert(s.begin(), s.end());
                addSuccsToWorklist = true;
            }
        }

        // Add succs to worklist
        if (addSuccsToWorklist) {
            if (TerminatorInst *TI = dyn_cast<TerminatorInst>(I)) {
                for (unsigned i = 0; i < TI->getNumSuccessors(); i++)
                    worklist.push_back(TI->getSuccessor(i)->begin());
            }
            else
                worklist.push_back(getNextInstruction(I));
        }

        // Add current instruction to worklist
        if (addCurrToWorklist)
            worklist.push_back(I);

        // Add preds to worklist
        if (addPredsToWorklist) {
            BasicBlock *BB = I->getParent();
            Instruction *FirstInBB = BB->begin();
            if (FirstInBB == I) {
                for (pred_iterator PI = pred_begin(BB), E = pred_end(BB);
                     PI != E;
                     ++PI) {
                    BasicBlock *Pred = *PI;
                    worklist.push_back(--(Pred->end()));
                }
            }
            else
                worklist.push_back(getPreviousInstruction(I));
        }
    }
}

bool LivenessPointsTo::runOnFunctionAt(const CallString& CS,
                                       Function *F,
                                       PointsToRelation *EntryPointsTo,
                                       LivenessSet *ExitLiveness) {
    IntraproceduralPointsTo *Out = data.getPointsTo(CS, F);
    IntraproceduralPointsTo *Copy = copyPointsToMap(Out);
    SmallVector<std::tuple<CallInst *, Function *, PointsToRelation *, LivenessSet *>, 8> Calls;
    runOnFunction(F, CS, Out, EntryPointsTo, ExitLiveness, Calls);
    if (arePointsToMapsEqual(F, Out, Copy)) {
        // If there is a prefix with the same information, then make it
        // recursive. This requires that the information that was just computed
        // is correct, i.e., the caller does not need it's information computed.
        // This is true here because it would only run the analysis on it's
        // callee if it's information hadn't changed. What the callees of the
        // function do here is guaranteed to be correct (FIXME: Is this true?).
        if (data.attemptMakeRecursiveCallString(F, CS, Out))
            return false;
        // If there is no prefix with the same information, then we need to look
        // further down the tree until we reach a leaf or find one, so run the
        // analysis on the callees. The analysis doesn't need to be rerun on the
        // caller here for the same reason as above. If the information at a
        // callee changes, then they will rerun the analysis here.
        bool rerun = false;
        for (auto C : Calls) {
            CallInst *I;
            Function *F;
            PointsToRelation *PT;
            LivenessSet *L;
            std::tie(I, F, PT, L) = C;
            rerun |= runOnFunctionAt(CS.addCallSite(I), F, PT, L);
        }
        if (rerun)
            return runOnFunctionAt(CS, F, EntryPointsTo, ExitLiveness);
        else
            return false;
    }
    else {
        // Since the information at the caller depends on the information here,
        // rerun the analysis at the caller. We don't need to rerun it on the
        // callees yet because the caller will rerun on it's callees if
        // neccessary.
        if (!CS.isEmpty())
            return true;
        // If there is no caller, then rerun the analysis here. We'll consider
        // the callees when a fixed point is reached.
        return runOnFunctionAt(CS, F, EntryPointsTo, ExitLiveness);
    }
}

void LivenessPointsTo::runOnModule(Module &M) {
    globals.clear();
    // We assume that globals point to a fixed location.
    PointsToRelation pointsto;
    for (auto I = M.global_begin(), E = M.global_end(); I != E; ++I) {
        auto N = factory.getNode(&*I);
        pointsto.insert(std::make_pair(N, factory.getGlobalNode(&*I)));
        globals.insert(N);
    }

    for (Function &F : M) {
        if (!F.isDeclaration()) {
            PointsToRelation *PT = new PointsToRelation(pointsto);
            // Note that the lout for the return instructions should contain all
            // globals, and everything that they can point to. Since we don't
            // know what they can point to here, we pass nullptr and work out
            // the correct set during the analysis.
            runOnFunctionAt(CallString::empty(), &F, PT, nullptr);
        }
    }
}