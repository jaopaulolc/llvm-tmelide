#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/TransactionAtomicInfo.h"
#include "llvm/Transforms/Transactify/SlowPathCreation.h"

#include <set>
#include <unordered_set>
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "slowpathcreation"

namespace {

struct SlowPathCreation : public FunctionPass {
  // Pass identification, replacement for typeid
  static char ID;

  SlowPathCreationPass Impl;

  SlowPathCreation() : FunctionPass(ID) {
    initializeSlowPathCreationPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto &L = getAnalysis<TransactionAtomicInfoPass>().getTransactionAtomicInfo();
    return Impl.runImpl(F, L);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TransactionAtomicInfoPass>();
  }

};

} // end anonymous namespace

char SlowPathCreation::ID = 0;
INITIALIZE_PASS_BEGIN(SlowPathCreation, "slowpathcreation",
    "[GNU-TM] SlowPath Creation Pass", false, false);
INITIALIZE_PASS_DEPENDENCY(TransactionAtomicInfoPass)
INITIALIZE_PASS_END(SlowPathCreation, "slowpathcreation",
    "[GNU-TM] SlowPath Creation Pass", false, false);

namespace llvm {

FunctionPass* createSlowPathCreationPass() {
  return new SlowPathCreation();
}

} // end namespace llvm

static void
eraseFirstCallInst(BasicBlock* BB, StringRef calleeName) {
  BasicBlock::iterator it = BB->begin();
  for (; it != BB->end(); it++) {
    if (isa<CallInst>(*it)) {
      CallInst& C = cast<CallInst>(*it);
      Function* calledFunction = C.getCalledFunction();
      if (calledFunction && calledFunction->hasName() &&
          calledFunction->getName().compare(calleeName) == 0) {
        it->eraseFromParent();
        break;
      }
    }
  }
}

bool SlowPathCreationPass::runImpl(Function &F,
    TransactionAtomicInfo &TAI) {
  errs() << "[SlowPathCreation] Hello from " << F.getName() << '\n';

  if (TAI.isListOfAtomicBlocksEmpty()) {
    // If function does not contain _ITM_XXX call,
    // then do nothing
    return false;
  }

  for (TransactionAtomic &TA
        : TAI.getListOfAtomicBlocks()) {

    BasicBlock* slowPathEnterBB = TA.getSlowPathEnterBB();
    Instruction* endSlowPathCall = slowPathEnterBB->begin()->getNextNode();
    // SlowPathStmt generates a single BasicBlock
    //
    // ; <label>:XX:
    //   call void @__begin_tm_slow_path()
    //   call void @__end_tm_slow_path()
    //
    // Split it so we have an slow path with a single entry (slowPathEnterBB)
    // and a single exit (slowPathExitBB)
    //
    // ; <label>:XX_entry:
    //   call void @__begin_tm_slow_path()
    //   ...
    //
    // ; <label>:XX_exit:
    //   ...
    //   call void @__end_tm_slow_path()
    //
    BasicBlock* slowPathExitBB =
      slowPathEnterBB->splitBasicBlock(endSlowPathCall);

    BasicBlock* fastPathEnterBB = TA.getFastPathEnterBB();
    BasicBlock* fastPathExitBB = TA.getFastPathExitBB();

    std::unordered_set<BasicBlock*> &transactionTerminators =
      TA.getTransactionTerminators();

    // Mapping of fastPath instructions and basic blocks to
    // their slowPath clones
    ValueToValueMapTy VMap;

    std::set<BasicBlock*> VisitedBBs;
    std::queue<BasicBlock*> WorkQueue;
    WorkQueue.push(TA.getFastPathEnterBB());
    // First we clone each fastPath basic block and only set
    // successor of slowPath entry and exit blocks
    while ( ! WorkQueue.empty() ) {
      BasicBlock* currBB = WorkQueue.front();
      WorkQueue.pop();

      BasicBlock* currBBClone =  CloneBasicBlock(currBB, VMap);
      VMap[currBB] = currBBClone;
      currBBClone->insertInto(&F);
      VisitedBBs.insert(currBB);
      currBB->print(errs(), true);
      if (currBB == fastPathEnterBB) {
        // remove __begin_tm_fast_path call
        eraseFirstCallInst(currBBClone, "__begin_tm_fast_path");
        slowPathEnterBB->getTerminator()->setSuccessor(0, currBBClone);
        if (fastPathEnterBB == fastPathExitBB) {
          // remove __end_tm_fast_path call
          eraseFirstCallInst(currBBClone, "__end_tm_fast_path");
          currBBClone->getTerminator()->setSuccessor(0, slowPathExitBB);
        }
      } else if (currBB == fastPathExitBB) {
        eraseFirstCallInst(currBBClone, "__end_tm_fast_path");
        TerminatorInst* currBBCloneTerminator = currBBClone->getTerminator();
        currBBCloneTerminator->setSuccessor(0, slowPathExitBB);
      }

      TerminatorInst* currBBTerminator = currBB->getTerminator();
      for (BasicBlock* currBBSucc : currBBTerminator->successors()) {
        if (transactionTerminators.count(currBBSucc) == 0 &&
            VisitedBBs.count(currBBSucc) == 0) {
          VisitedBBs.insert(currBBSucc);
          WorkQueue.push(currBBSucc);
        }
      }
    }

    // One we have duplicated every fastPath block we proceed to
    // replace every slowPath User with its cloned Use value
    VisitedBBs.clear();
    WorkQueue.push(TA.getFastPathEnterBB());
    while ( ! WorkQueue.empty() ) {
      BasicBlock* currBB = WorkQueue.front();
      WorkQueue.pop();
      VisitedBBs.insert(currBB);
      BasicBlock* currBBClone = dyn_cast<BasicBlock>(VMap[currBB]);
      for (Instruction &I : currBBClone->getInstList()) {
        for (Use &U : I.operands()) {
          if (VMap.count(U)) {
            U.getUser()->replaceUsesOfWith(U, VMap[U]);
          }
        }
      }
      currBBClone->print(errs(), true);
      TerminatorInst* currBBTerminator = currBB->getTerminator();
      for (BasicBlock* currBBSucc : currBBTerminator->successors()) {
        if (transactionTerminators.count(currBBSucc) == 0 &&
            VisitedBBs.count(currBBSucc) == 0) {
          VisitedBBs.insert(currBBSucc);
          WorkQueue.push(currBBSucc);
        }
      }
    }
  }
  return true;
}

PreservedAnalyses SlowPathCreationPass::run(Function &F,
    FunctionAnalysisManager &AM) {
  auto &L = AM.getResult<TransactionAtomicInfoAnalysis>(F);
  bool Changed = runImpl(F, L);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
