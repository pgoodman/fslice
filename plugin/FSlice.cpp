/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#define DEBUG_TYPE "FSlice"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Pass.h>

#include <iostream>
#include <map>
#include <vector>

using namespace llvm;

// Set of llvm values that represent a logical variables.
struct VSet {
  VSet *rep;
  int index;
  bool is_used;
};

// Information about an instruction.
struct IInfo {
  BasicBlock *B;
  Instruction *I;
  bool is_load;
  bool is_store;
  bool is_call;
  bool is_return;
  bool is_ignored;
};

// Introduces generic dynamic program slic recording into code.
class FSliceFunctionPass : public FunctionPass {
 public:
  FSliceFunctionPass(void);

  virtual bool runOnFunction(Function &F) override;

  static char ID;

 private:
  void collectInstructions(Function &F);
  void initVSets(Function &F);
  void combineVSets(void);
  static void combineVSet(VSet *VSet1, VSet *VSet2);
  static VSet *getVSet(VSet *VSet);
  void labelVSets(void);
  void allocaVSetArray(void);
  void runOnInstructions(void);
  void runOnLoad(const IInfo &II);
  void runOnStore(const IInfo &II);

  Value *getTaint(Value *V);

  // Creates a function returning void on some arbitrary number of argument
  // types.
  template <typename... ParamTypes>
  Function *CreateFunc(Type *RetTy, std::string name, std::string suffix,
                       ParamTypes... Params) {
    std::vector<Type *> FuncParamTypes = {Params...};
    auto FuncType = llvm::FunctionType::get(RetTy, FuncParamTypes, false);
    return dyn_cast<Function>(M->getOrInsertFunction(name + suffix, FuncType));
  }

  Function *F;
  Module *M;
  LLVMContext *C;
  const DataLayout *DL;

  Type *IntPtrTy;
  Type *VoidTy;

  std::map<Argument *, VSet *> ArgToVSet;
  std::vector<IInfo> IIs;
  std::vector<VSet> VSets;
  std::map<Value *,VSet *> VtoVSet;
  std::vector<Value *> IdxToVar;

  int numVSets;
};

FSliceFunctionPass::FSliceFunctionPass(void)
    : FunctionPass(ID),
      F(nullptr),
      M(nullptr),
      C(nullptr),
      DL(nullptr),
      IntPtrTy(nullptr),
      VoidTy(nullptr),
      numVSets(0) {}

// Instrument every instruction in a function.
bool FSliceFunctionPass::runOnFunction(Function &F_) {
  F = &F_;
  M = F->getParent();
  C = &(M->getContext());
  DL = M->getDataLayout();
  IntPtrTy = Type::getIntNTy(*C,  DL->getPointerSizeInBits());
  VoidTy = Type::getVoidTy(*C);
  numVSets = 0;

  if (F->begin() == F->end()) return false;

  F->dump();

  collectInstructions(*F);
  initVSets(*F);
  combineVSets();
  labelVSets();
  allocaVSetArray();
  runOnInstructions();

  for (auto V : VtoVSet) {
    V.first->dump();
    std::cout << "var: " << getVSet(V.second)->index << std::endl;
  }

  F->dump();

  ArgToVSet.clear();
  IIs.clear();
  VSets.clear();
  VtoVSet.clear();
  IdxToVar.clear();

  return true;
}

// Collect a list of all instructions. We'll be adding all sorts of new
// instructions in so having a list makes it easy to operate on just the
// originals.
void FSliceFunctionPass::collectInstructions(Function &F) {
  for (auto &B : F) {
    for (auto &I : B) {
      IIs.push_back({&B, &I, isa<LoadInst>(I), isa<StoreInst>(I),
                     isa<CallInst>(I), isa<ReturnInst>(I),
                     !I.use_empty() && !isa<BranchInst>(I) &&
                     !isa<InvokeInst>(I) && !isa<CmpInst>(I)});
    }
  }
}

// Group the values (instructions, arguments) into sets where each set
// represents a logical variable in the original program.
void FSliceFunctionPass::initVSets(Function &F) {
  VSets.resize(IIs.size() + F.arg_size());

  for (auto &VSet : VSets) {
    VSet.rep = &VSet;
    VSet.index = -1;
    VSet.is_used = false;
  }
  auto i = 0UL;
  for (auto &A : F.args()) {
    if (A.use_empty()) continue;
    auto VSet = &(VSets[i++]);
    VtoVSet[&A] = VSet;
    ArgToVSet[&A] = VSet;
    VSet->is_used = true;
  }
  for (auto &II : IIs) {
    if (!II.is_ignored || II.is_store || II.is_return) continue;
    auto VSet = &(VSets[i++]);
    VtoVSet[II.I] = VSet;
    VSet->is_used = true;
  }
}

// Combine value sets.
void FSliceFunctionPass::combineVSets(void) {
  for (auto &II : IIs) {
    if (!II.is_ignored || II.is_store || II.is_return) continue;
    auto I = II.I;
    auto VSet = VtoVSet[I];
    if (PHINode *PHI = dyn_cast<PHINode>(I)) {
      auto nV = PHI->getNumIncomingValues();
      for (auto iV = 0U; iV < nV; ++iV) {
        auto V = PHI->getIncomingValue(iV);
        if (!isa<Constant>(V)) {
          auto incomingVSet = VtoVSet[V];
          combineVSet(VSet, incomingVSet);
        }
      }
    }
  }
}

// Combine two value sets. This implements disjoint set union.
void FSliceFunctionPass::combineVSet(VSet *VSet1, VSet *VSet2) {
  VSet1 = getVSet(VSet1);
  VSet2 = getVSet(VSet2);
  if (VSet1 < VSet2) {
    VSet2->rep = VSet1;
  } else if (VSet1 > VSet2){
    VSet1->rep = VSet2;
  }
}

// Get the representative of this VSet. Implements union-find path compression.
VSet *FSliceFunctionPass::getVSet(VSet *VSet) {
  while (VSet->rep != VSet) {
    VSet = (VSet->rep = VSet->rep->rep);
  }
  return VSet;
}

// Assign array indices to each VSet. This labels all variables from 0 to N-1.
void FSliceFunctionPass::labelVSets(void) {
  for (auto &rVSet : VSets) {
    if (!rVSet.is_used) continue;
    auto pVSet = getVSet(&rVSet);
    if (-1 == pVSet->index) {
      pVSet->index = numVSets++;
    }
  }
}

// Allocate an array to hold the slice taints for each variable in this
// function.
void FSliceFunctionPass::allocaVSetArray(void) {
  auto &B = F->getEntryBlock();
  auto &IList = B.getInstList();
  auto &FirstI = *IList.begin();
  for (auto i = 0; i < numVSets; ++i) {
    auto taintVar = new AllocaInst(IntPtrTy);
    IList.insert(FirstI, taintVar);
    IdxToVar.push_back(taintVar);
  }
}

// Instrument the original instructions.
void FSliceFunctionPass::runOnInstructions(void) {
  for (auto II : IIs) {
    if (II.is_load) {
      runOnLoad(II);
    } else if (II.is_store) {
      runOnStore(II);
    }
  }
}

// Returns the size of a loaded/stored object.
static uint64_t LoadStoreSize(const DataLayout *DL, Value *P) {
  PointerType *PT = dyn_cast<PointerType>(P->getType());
  return DL->getTypeStoreSize(PT->getElementType());
}

// Instrument a single instruction.
void FSliceFunctionPass::runOnLoad(const IInfo &II) {
  LoadInst *LI = dyn_cast<LoadInst>(II.I);
  if (auto TV = getTaint(II.I)) {
    auto &IList = II.B->getInstList();
    auto P = LI->getPointerOperand();
    auto S = LoadStoreSize(DL, P);
    auto A = CastInst::CreatePointerCast(P, IntPtrTy);
    auto LoadFunc = CreateFunc(IntPtrTy, "__fslice_load", std::to_string(S),
                               IntPtrTy);
    auto T = CallInst::Create(LoadFunc, {A});
    IList.insert(II.I, A);
    IList.insert(II.I, T);
    IList.insert(II.I, new StoreInst(T, TV));
  }
}

// Instrument a single instruction.
void FSliceFunctionPass::runOnStore(const IInfo &II) {
  StoreInst *SI = dyn_cast<StoreInst>(II.I);
  auto &IList = II.B->getInstList();
  auto V = SI->getValueOperand();
  auto P = SI->getPointerOperand();
  auto S = LoadStoreSize(DL, P);
  auto A = CastInst::CreatePointerCast(P, IntPtrTy);

  if (auto TV = getTaint(V)) {
    auto T = new LoadInst(TV);
    std::vector<Value *> args = {T, A};
    auto StoreFunc = CreateFunc(VoidTy, "__fslice_store", std::to_string(S),
                                IntPtrTy, IntPtrTy);
    IList.insert(II.I, T);
    IList.insert(II.I, A);
    IList.insert(II.I, CallInst::Create(StoreFunc, args));

  } else {
    auto ClearFunc = CreateFunc(VoidTy, "__fslice_clear", std::to_string(S),
                                    IntPtrTy);
    IList.insert(II.I, A);
    IList.insert(II.I, CallInst::Create(ClearFunc, {A}));
  }
}

Value *FSliceFunctionPass::getTaint(Value *V) {
  if (VtoVSet.count(V)) {
    return IdxToVar[VtoVSet[V]->index];
  } else {
    auto it = std::find(IdxToVar.begin(), IdxToVar.end(), V);
    if (it == IdxToVar.end()) return nullptr;
    return *it;
  }
}

char FSliceFunctionPass::ID = '\0';

static RegisterPass<FSliceFunctionPass> X(
    "fslice",
    "File system runtime program slicing pass.",
    false,  // Only looks at CFG.
    false);  // Analysis Pass.
