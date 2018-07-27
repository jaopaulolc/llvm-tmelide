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

#include "llvm/Transforms/Transactify/SlowPathCreation.h"

#include <set>
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
    return Impl.runImpl(F);
  }

};

struct DualPathInfoCollector : public InstVisitor<DualPathInfoCollector> {

  struct TransactionAtomicInfo {
    BasicBlock* fastPathEnterBB;
    BasicBlock* fastPathExitBB;
    BasicBlock::iterator beginSlowPathCall;
    BasicBlock::iterator endSlowPathCall;
    BasicBlock* transactionEntryBB;
    std::set<BasicBlock*> transactionTerminators;
  public:
    TransactionAtomicInfo() {
      fastPathEnterBB = nullptr;
      fastPathExitBB = nullptr;
      transactionEntryBB = nullptr;
    }
    BasicBlock* getFastPathEnterBB() {
      return fastPathEnterBB;
    }
    BasicBlock* getFastPathExitBB() {
      return fastPathExitBB;
    }
    BasicBlock::iterator getBeginSlowPathCall() {
      return beginSlowPathCall;
    }
    BasicBlock::iterator getEndSlowPathCall() {
      return endSlowPathCall;
    }
    BasicBlock* getTransactionEntry() {
      return transactionEntryBB;
    }
    void insertTransactionTerminator(BasicBlock* TerminatorBB){
      transactionTerminators.insert(TerminatorBB);
    }
    std::set<BasicBlock*>& getTransactionTerminators() {
      return transactionTerminators;
    }
  };
  std::list<TransactionAtomicInfo> listOfAtomicBlocks;

  void visitCallInst(CallInst &C) {
    if (isa<Function>(C.getCalledValue())) {
      Function* calledFunction = static_cast<Function*>(C.getCalledValue());
      if (calledFunction->hasName()) {
        StringRef targetName = calledFunction->getName();
        if (targetName.compare("__begin_tm_slow_path") == 0) {
          TransactionAtomicInfo &TAI = listOfAtomicBlocks.back();
          TAI.beginSlowPathCall = C.getIterator();
        } else if (targetName.compare("__end_tm_slow_path") == 0) {
          TransactionAtomicInfo &TAI = listOfAtomicBlocks.back();
          TAI.endSlowPathCall = C.getIterator();
        } else if (targetName.compare("__begin_tm_fast_path") == 0) {
          TransactionAtomicInfo &TAI = listOfAtomicBlocks.back();
          TAI.fastPathEnterBB = C.getParent();
        } else if (targetName.compare("__end_tm_fast_path") == 0) {
          TransactionAtomicInfo &TAI = listOfAtomicBlocks.back();
          TAI.fastPathExitBB = C.getParent();
        } else if (targetName.compare("_ITM_beginTransaction") == 0) {
          TransactionAtomicInfo TAI;
          TAI.transactionEntryBB = C.getParent();
          listOfAtomicBlocks.push_back(TAI);
        } else if (targetName.compare("_ITM_commitTransaction") == 0) {
          TransactionAtomicInfo &TAI = listOfAtomicBlocks.back();
          TAI.transactionTerminators.insert(C.getParent());
        }
      }
    }
  }
public:
  bool isListOfAtomicBlocksEmpty() {
    return listOfAtomicBlocks.empty();
  }
  std::list<TransactionAtomicInfo>& getListOfAtomicBlocks() {
    return listOfAtomicBlocks;
  }
};
} // end anonymous namespace

char SlowPathCreation::ID = 0;
INITIALIZE_PASS(SlowPathCreation, "slowpathcreation",
    "[GNU-TM] SlowPath Creation Pass", false, false);

namespace llvm {

FunctionPass* createSlowPathCreationPass() {
  return new SlowPathCreation();
}

} // end namespace llvm

bool SlowPathCreationPass::runImpl(Function &F) {
  errs() << "Hello from " << F.getName() << '\n';
  DualPathInfoCollector DPIC;
  DPIC.visit(F);

  if (DPIC.isListOfAtomicBlocksEmpty()) {
    // If function does not contain _ITM_XXX call,
    // then do nothing
    return false;
  }

  for (DualPathInfoCollector::TransactionAtomicInfo &TAI
        : DPIC.getListOfAtomicBlocks()) {

    BasicBlock::iterator beginSlowPathCall = TAI.getBeginSlowPathCall();
    BasicBlock::iterator endSlowPathCall = TAI.getEndSlowPathCall();
    BasicBlock* slowPathEnterBB = beginSlowPathCall->getParent();
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

    BasicBlock* fastPathEnterBB = TAI.getFastPathEnterBB();
    BasicBlock* fastPathExitBB = TAI.getFastPathExitBB();

    std::set<BasicBlock*> &transactionTerminators =
      TAI.getTransactionTerminators();

    // Mapping of fastPath instructions and basic blocks to
    // their slowPath clones
    ValueToValueMapTy VMap;

    std::set<BasicBlock*> VisitedBBs;
    std::queue<BasicBlock*> WorkQueue;
    WorkQueue.push(TAI.getFastPathEnterBB());
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
        BasicBlock::iterator it;
        it = currBBClone->begin();
        it->eraseFromParent();
        slowPathEnterBB->getTerminator()->setSuccessor(0, currBBClone);
        if (fastPathEnterBB == fastPathExitBB) {
          // remove __end_tm_fast_path call
          TerminatorInst* currBBCloneTerminator = currBBClone->getTerminator();
          it = currBBCloneTerminator->getPrevNode()->getIterator();
          it->eraseFromParent();
        }
      } else if (currBB == fastPathExitBB) {
        TerminatorInst* currBBCloneTerminator = currBBClone->getTerminator();
        BasicBlock::iterator it;
        // remove __end_tm_fast_path call
        it = currBBCloneTerminator->getPrevNode()->getIterator();
        it->eraseFromParent();
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
    WorkQueue.push(TAI.getFastPathEnterBB());
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
  bool Changed = runImpl(F);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
