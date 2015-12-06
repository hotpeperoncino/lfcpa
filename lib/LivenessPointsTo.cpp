#include <set>

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include "../include/PointsToNode.h"
#include "../include/LivenessPointsTo.h"

Instruction *getNextInstruction(Instruction *Instr) {
    BasicBlock::iterator I(Instr);
    Instruction *Next = ++I;
    // There should be at least one more instruction in the basic block.
    assert(Next != Instr->getParent()->end());
    return Next;
}

Instruction *getPreviousInstruction(Instruction *Instr) {
    BasicBlock::iterator I(Instr);
    // There should be at least one instruction before Instr.
    assert (I != Instr->getParent()->begin());
    return --I;
}

void LivenessPointsTo::subtractKill(std::set<PointsToNode *>& Lin, Instruction *I) {
    // TODO
}

void LivenessPointsTo::unionRef(std::set<PointsToNode *>& Lin, Instruction *I, std::set<PointsToNode *>* Lout, std::set<std::pair<PointsToNode*, PointsToNode*>>* Ain) {
    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        // We only consider the pointer and the possible values in memory to be
        // ref'd if the load is live.
        if (Lout->find(factory.getNode(I)) != Lout->end()) {
            Value *Ptr = LI->getPointerOperand();
            PointsToNode *PtrNode = factory.getNode(Ptr);
            Lin.insert(PtrNode);
            for (auto P : *Ain)
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
    else {
        // If the instruction is not a load or a store, we consider all of it's
        // operands to be ref'd.
        for (Use &U : I->operands())
            if (Value *Operand = dyn_cast<Value>(U))
                Lin.insert(factory.getNode(Operand));
    }
}

void LivenessPointsTo::runOnFunction(Function &F) {
    // Points-to information
    DenseMap<const Instruction *, std::set<std::pair<PointsToNode*, PointsToNode*>> *> ain, aout;
    // Liveness information
    DenseMap<const Instruction *, std::set<PointsToNode*> *> lin, lout;

    // Create vectors to store the points-to information in.
    for (auto &BB : F) {
        for (auto &I : BB) {
            ain.insert(std::make_pair(&I, new std::set<std::pair<PointsToNode*, PointsToNode*>>()));
            aout.insert(std::make_pair(&I, new std::set<std::pair<PointsToNode*, PointsToNode*>>()));
            lin.insert(std::make_pair(&I, new std::set<PointsToNode*>()));
            lout.insert(std::make_pair(&I, new std::set<PointsToNode*>()));
        }
    }

    // Create and initialize worklist.
    SmallVector<Instruction *, 128> worklist;
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
        worklist.push_back(&*I);

    while (!worklist.empty()) {
        Instruction *I = worklist.pop_back_val();
        auto instruction_ain = ain.find(I)->second, instruction_aout = aout.find(I)->second;
        auto instruction_lin = lin.find(I)->second, instruction_lout = lout.find(I)->second;

        bool addPredsToWorklist = false, addSuccsToWorklist = false;

        // Compute lout for the current instruction.
        if (TerminatorInst *TI = dyn_cast<TerminatorInst>(I)) {
            // If this instruction is a terminator, it may have multiple
            // successors.
            instruction_lout->clear();
            for (unsigned i = 0; i < TI->getNumSuccessors(); i++) {
                Instruction *Succ = TI->getSuccessor(i)->begin();
                auto succ_lin = lin.find(Succ)->second;
                instruction_lout->insert(succ_lin->begin(), succ_lin->end());
            }
        }
        else {
            // If this instruction is not a terminator, it has exactly one
            // successor -- the next instruction in the function.
            Instruction *Succ = getNextInstruction(I);
            auto succ_lin = lin.find(Succ)->second;
            if (*succ_lin != *instruction_lout) {
                instruction_lout->clear();
                instruction_lout->insert(succ_lin->begin(), succ_lin->end());
            }
        }

        // Compute lin for the current instruction.
        std::set<PointsToNode *> n;
        n.insert(instruction_lout->begin(), instruction_lout->end());
        subtractKill(n, I);
        unionRef(n, I, instruction_lout, instruction_ain);
        // If the two sets are the same, then no changes need to be made to lin,
        // so don't do anything here. Otherwise, we need to update lin and add
        // the predecessors of the current instruction to the worklist.
        if (n != *instruction_lin) {
            instruction_lin->clear();
            instruction_lin->insert(n.begin(), n.end());
            addPredsToWorklist = true;
        }

        // Compute ain for the current instruction.
        // TODO

        // Compute aout for the current instruction.
        // TODO

        // Add preds to worklist
        if (addPredsToWorklist) {
            BasicBlock *BB = I->getParent();
            Instruction *FirstInBB = BB->begin();
            if (FirstInBB == I) {
                for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
                    BasicBlock *Pred = *PI;
                    worklist.push_back(Pred->begin());
                }
            }
            else
                worklist.push_back(getPreviousInstruction(I));
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
    }

   for (auto &KV : ain)
       pointsto.insert(KV);
}

std::set<std::pair<PointsToNode*, PointsToNode*>>* LivenessPointsTo::getPointsTo(Instruction &I) const {
    return pointsto.find(&I)->second;
}
