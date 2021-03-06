#ifndef LFCPA_POINTSTONODE_H
#define LFCPA_POINTSTONODE_H

#include <sstream>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "CallString.h"

#define MAX_DESCENDANT_LEVEL 8

using namespace llvm;

class GEPPointsToNode;

class PointsToNode {
public:
    enum PointsToNodeKind {
        PTNK_Unknown,
        PTNK_Init,
        PTNK_Value,
        PTNK_Global,
        PTNK_NoAlias,
        PTNK_GEP
    };
    friend class GEPPointsToNode;
    friend class PointsToNodeFactory;
    friend class LivenessSet;
    friend class LivenessPointsTo;
private:
    const PointsToNodeKind Kind;
protected:
    StringRef name;
    static int nextId;
    bool summaryNode = false, summaryNodePointees = false, fieldSensitive = true;

    PointsToNode(PointsToNodeKind K) : Kind(K) {}
public:
    SmallVector<PointsToNode *, 4> children;
    PointsToNodeKind getKind() const { return Kind; }

    virtual bool hasPointerType() const { return false; }
    virtual bool multipleStackFrames() const { return false; }
    virtual bool singlePointee() const { return false; }
    virtual PointsToNode *getSinglePointee() const {
        llvm_unreachable("This node doesn't always have a single pointee.");
    }
    inline void markPointeesAreSummaryNodes() {
        summaryNodePointees = true;
        if (singlePointee())
            getSinglePointee()->markAsSummaryNode();
    }
    inline void markNotFieldSensitive() {
        fieldSensitive = false;
    }
    inline void markAsSummaryNode() {
        assert(!singlePointee());
        summaryNode = true;
    }
    virtual bool isAlwaysSummaryNode() const {
        return summaryNode;
    }
    virtual bool isSummaryNode(const CallString &) const {
        return summaryNode;
    }
    inline bool pointeesAreSummaryNodes() const {
        return summaryNodePointees;
    }
    inline bool isFieldSensitive() const {
        return fieldSensitive;
    }
    inline StringRef getName() const {
        return name;
    }
    inline bool isAggregate() const {
        return fieldSensitive && !children.empty();
    }
    inline bool isSubNodeOf(PointsToNode *N) {
        if (this == N)
            return true;

        for (PointsToNode *Child : N->children)
            if (isSubNodeOf(Child))
                return true;

        return false;
    }
    virtual std::pair<const PointsToNode *, SmallVector<uint64_t, 4>> getAddress() const {
        return std::make_pair(this, SmallVector<uint64_t, 4>());
    }
    virtual const Function *getFunction() const {
        return nullptr;
    }
};

class UnknownPointsToNode : public PointsToNode {
    private:
        std::string stdName;
    public:
        UnknownPointsToNode() : PointsToNode(PTNK_Unknown), stdName("?") {
            name = StringRef(stdName);
        }

        bool isAlwaysSummaryNode() const override {
            // Unknowns nodes are never considered to be summary nodes.
            return false;
        }

        bool isSummaryNode(const CallString &) const override {
            // Unknown nodes are never considered to be summary nodes.
            return false;
        }

        static bool classof(const PointsToNode *N) {
            return N->getKind() == PTNK_Unknown;
        }
};

inline Type *getEffectiveType(const Value *V) {
    auto I = V->user_begin(), E = V->user_end();
    if (I != E)
        if (const BitCastInst *CI = dyn_cast<BitCastInst>(*I))
            if ((++I) == E)
                return CI->getType();
    return V->getType();
}

class ValuePointsToNode : public PointsToNode {
    private:
        std::string stdName;
        bool isPointer, userOrArg;
        PointsToNode *Pointee;
    public:
        ValuePointsToNode(const Value *V, PointsToNode *Pointee) : PointsToNode(PTNK_Value), Pointee(Pointee) {
            assert(V != nullptr);
            name = V->getName();
            if (name == "") {
                stdName = std::to_string(nextId++);
                name = StringRef(stdName);
            }
            isPointer = getEffectiveType(V)->isPointerTy();
            userOrArg = isa<User>(V) || isa<Argument>(V);
        }

        ValuePointsToNode(const Value *V) : ValuePointsToNode(V, nullptr) {}

        bool hasPointerType() const override { return isPointer; }
        bool multipleStackFrames() const override { return userOrArg; }
        bool singlePointee() const override { return Pointee != nullptr; }
        PointsToNode *getSinglePointee() const override {
            assert(Pointee != nullptr);
            return Pointee;
        }

        static bool classof(const PointsToNode *N) {
            return N->getKind() == PTNK_Value;
        }
};

class GlobalPointsToNode : public PointsToNode {
    private:
        const GlobalObject *Object;
        std::string stdName;
        bool isPointer;
    public:
        GlobalPointsToNode(const GlobalObject *G) : PointsToNode(PTNK_Global), Object(G) {
           stdName = "global:" + G->getName().str();
           name = StringRef(stdName);
           auto GTy = getEffectiveType(G);
           assert(GTy->isPointerTy());
           isPointer = GTy->getPointerElementType()->isPointerTy();
        }

        bool hasPointerType() const override { return isPointer; }

        const Function *getFunction() const override {
            if (const Function *F = dyn_cast<Function>(Object)) {
                return F;
            }

            return nullptr;
        }

        static bool classof(const PointsToNode *N) {
            return N->getKind() == PTNK_Global;
        }
};

class InitPointsToNode : public PointsToNode {
    public:
        InitPointsToNode() : PointsToNode(PTNK_Init) {
            name = "init";
            summaryNode = true;
            fieldSensitive = false;
        }

        bool hasPointerType() const override { return true; }
        bool multipleStackFrames() const override { return true; }
        bool singlePointee() const override { return true; }
        PointsToNode *getSinglePointee() const override { return const_cast<InitPointsToNode *>(this); }
        static bool classof(const PointsToNode *N) {
            return N->getKind() == PTNK_Init;
        }
};

class NoAliasPointsToNode : public PointsToNode {
    private:
        std::string stdName;
        bool isPointer;
    public:
        const Function *Definer;
        NoAliasPointsToNode(const AllocaInst *AI) : PointsToNode(PTNK_NoAlias), Definer(AI->getParent()->getParent()) {
            stdName = "alloca:" + AI->getName().str();
            name = StringRef(stdName);
            auto Ty = getEffectiveType(AI);
            assert(Ty->isPointerTy());
            isPointer = Ty->getPointerElementType()->isPointerTy();
        }
        NoAliasPointsToNode(const CallInst *CI) : PointsToNode(PTNK_NoAlias), Definer(CI->getParent()->getParent()) {
            assert(CI->paramHasAttr(0, Attribute::NoAlias));
            stdName = "noalias:" + CI->getName().str();
            name = StringRef(stdName);
            auto Ty = getEffectiveType(CI);
            assert(Ty->isPointerTy());
            isPointer = Ty->getPointerElementType()->isPointerTy();
        }

        bool hasPointerType() const override { return isPointer; }
        bool multipleStackFrames() const override { return true; }
        bool isSummaryNode(const CallString &CS) const override {
            if (summaryNode)
                return true;

            if (CS.reachedMoreThanOnce(Definer))
                return true;

            return false;
        }

        static bool classof(const PointsToNode *N) {
            return N->getKind() == PTNK_NoAlias;
        }
};

class GEPPointsToNode : public PointsToNode {
    public:
        const PointsToNode *Parent;
    private:
        int level;
        bool pointerType;
        std::string stdName;
        PointsToNode *Pointee;
        friend class PointsToNodeFactory;
        friend class PointsToNode;
    public:
        const Type *NodeType;
        SmallVector<APInt, 8> indices;
        GEPPointsToNode(PointsToNode *Parent, const Type *Type, SmallVector<APInt, 8> indices, PointsToNode *Pointee) : PointsToNode(PTNK_GEP), Parent(Parent), Pointee(Pointee), NodeType(Type), indices(indices) {
            assert(!indices.empty());
            assert(isa<GEPPointsToNode>(Parent) || indices.begin()->getZExtValue() == 0);
            pointerType = Type->isPointerTy();

            std::stringstream ns;
            ns << Parent->getName().str();
            for (auto I : indices) {
                ns << "[";
                ns << I.getZExtValue();
                ns << "]";
            }
            Parent->children.push_back(this);
            stdName = ns.str();
            name = StringRef(stdName);

            if (GEPPointsToNode *P = dyn_cast<GEPPointsToNode>(Parent))
                level = P->level + indices.size();
            else
                level = indices.size();

            // Nested data structures could potentially result in the creation
            // of nodes at an arbitrarily large depth (in terms of the tree of
            // descendants of a node). Hence we limit the depth here to ensure
            // termination.
            if (level > MAX_DESCENDANT_LEVEL) {
                markNotFieldSensitive();
                if (pointerType)
                    markPointeesAreSummaryNodes();
            }

            assert(Pointee == nullptr || Parent->singlePointee());
            assert(Parent->isFieldSensitive());
        }
        GEPPointsToNode(PointsToNode *Parent, const Type *Type, User::const_op_iterator I, User::const_op_iterator E, PointsToNode *Pointee) : PointsToNode(PTNK_GEP), Parent(Parent), Pointee(Pointee), NodeType(Type) {
            assert(I != E);
            assert(isa<ConstantInt>(I) && "Can only treat GEPs with constant indices field-sensitively.");
            assert(isa<GEPPointsToNode>(Parent) || cast<ConstantInt>(I)->isZero());
            pointerType = Type->isPointerTy();

            std::stringstream ns;
            ns << Parent->getName().str();
            for (; I != E; ++I) {
                assert(isa<ConstantInt>(I) && "Can only treat GEPs with constant indices field-sensitively.");
                ConstantInt *Int = cast<ConstantInt>(I);
                indices.push_back(Int->getValue());
                ns << "[";
                ns << Int->getZExtValue();
                ns << "]";
            }
            Parent->children.push_back(this);
            stdName = ns.str();
            name = StringRef(stdName);

            if (GEPPointsToNode *P = dyn_cast<GEPPointsToNode>(Parent))
                level = P->level + indices.size();
            else
                level = indices.size();

            // Nested data structures could potentially result in the creation
            // of nodes at an arbitrarily large depth (in terms of the tree of
            // descendants of a node). Hence we limit the depth here to ensure
            // termination.
            if (level > MAX_DESCENDANT_LEVEL) {
                markNotFieldSensitive();
                if (pointerType)
                    markPointeesAreSummaryNodes();
            }

            assert(Pointee == nullptr || Parent->singlePointee());
            assert(Parent->isFieldSensitive());
        }

        GEPPointsToNode(PointsToNode *Parent, const Type *Type, const GEPOperator *V, PointsToNode *Pointee) : GEPPointsToNode(Parent, Type, V->idx_begin(), V->idx_end(), Pointee) { }

        bool hasPointerType() const override { return pointerType; }
        bool multipleStackFrames() const override { return true; }
        bool singlePointee() const override { return Pointee != nullptr; }
        PointsToNode *getSinglePointee() const override {
            assert(Pointee != nullptr);
            return Pointee;
        }

        static bool classof(const PointsToNode *N) {
            return N->getKind() == PTNK_GEP;
        }

        std::pair<const PointsToNode *, SmallVector<uint64_t, 4>> getAddress() const override {
            // We remove all of the zeros from the end of the list of indices
            // because they do not affect the address.
            int stagedZeros = 0;
            SmallVector<uint64_t, 4> resultIndices;
            for (const APInt &I : indices) {
                if (I == 0)
                    stagedZeros++;
                else {
                    while (stagedZeros > 0) {
                        resultIndices.push_back(0);
                        --stagedZeros;
                    }
                    resultIndices.push_back(I.getZExtValue());
                }
            }
            return std::make_pair(Parent, resultIndices);
        }
};

#endif
