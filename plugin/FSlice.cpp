/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/Transforms/Scalar.h>

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
  bool is_used;
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
  void allocaVSetArray(Function &F);
  void runOnInstructions(Function &F);
  void runOnInstruction(Module *M, Function *F, const IInfo &II);

  std::map<Argument *, VSet *> ArgToVSet;
  std::vector<IInfo> IIs;
  std::vector<VSet> VSets;
  std::map<Value *,VSet *> VtoVSet;
  int vsetArrSize;
};

FSliceFunctionPass::FSliceFunctionPass(void)
    : FunctionPass(ID),
      vsetArrSize(0) {}

// Instrument every instruction in a function.
bool FSliceFunctionPass::runOnFunction(Function &F) {
  if (F.begin() == F.end()) return false;

  F.dump();

  collectInstructions(F);
  initVSets(F);
  combineVSets();
  labelVSets();
  allocaVSetArray(F);
  runOnInstructions(F);

  for (auto V : VtoVSet) {
    V.first->dump();
    std::cout << "var: " << getVSet(V.second)->index << std::endl;
  }

  IIs.clear();
  VSets.clear();
  VtoVSet.clear();

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
                     !isa<InvokeInst>(I)});
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
    if (!II.is_used || II.is_store || II.is_return) continue;
    auto VSet = &(VSets[i++]);
    VtoVSet[II.I] = VSet;
    VSet->is_used = true;
  }
}

// Combine value sets.
void FSliceFunctionPass::combineVSets(void) {
  for (auto &II : IIs) {
    if (!II.is_used || II.is_store || II.is_return) continue;
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
      pVSet->index = vsetArrSize++;
    }
  }
}

// Allocate an array to hold the slice taints for each variable in this
// function.
void FSliceFunctionPass::allocaVSetArray(Function &F) {
  auto &B = F.getEntryBlock();
  auto &IList = B.getInstList();
  auto &FirstI = *IList.begin();
  (void) FirstI;
}

// Instrument the original instructions.
void FSliceFunctionPass::runOnInstructions(Function &F) {
  auto M = F.getParent();
  for (auto II : IIs) {
    if (!II.is_used) continue;
    runOnInstruction(M, &F, II);
  }
}

// Instrument a single instruction.
void FSliceFunctionPass::runOnInstruction(Module *M, Function *F,
                                          const IInfo &II) {

}

char FSliceFunctionPass::ID = '\0';

static RegisterPass<FSliceFunctionPass> X(
    "fslice",
    "File system runtime program slicing pass.",
    false,  // Only looks at CFG.
    false);  // Analysis Pass.
