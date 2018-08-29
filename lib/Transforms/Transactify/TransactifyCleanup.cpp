#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/TransactionAtomicInfo.h"
#include "llvm/Transforms/Transactify/ReplaceCallInsideTransaction.h"
#include "llvm/Transforms/Transactify/TransactifyCleanup.h"

#include <set>
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "transactifycleanup"

namespace {

struct TransactifyCleanup : public FunctionPass {
  // Pass identification, replacement for typeid
  static char ID;

  TransactifyCleanupPass Impl;

  TransactifyCleanup() : FunctionPass(ID) {
    initializeTransactifyCleanupPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto &L = getAnalysis<TransactionAtomicInfoPass>().
      getTransactionAtomicInfo();
    return Impl.runImpl(F, L);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TransactionAtomicInfoPass>();
  }

};

} // end anonymous namespace

const char * const pass_name = "[GNU-TM] Transactify Cleanup Pass";

char TransactifyCleanup::ID = 0;
INITIALIZE_PASS_BEGIN(TransactifyCleanup, DEBUG_TYPE, pass_name, false, false);
INITIALIZE_PASS_DEPENDENCY(ReplaceCallInsideTransaction)
INITIALIZE_PASS_END(TransactifyCleanup, DEBUG_TYPE, pass_name, false, false);

namespace llvm {

FunctionPass* createTransactifyCleanupPass() {
  return new TransactifyCleanup();
}

} // end namespace llvm

bool TransactifyCleanupPass::runImpl(Function &F,
    TransactionAtomicInfo &TAI) {
  errs() << "[TransactifyCleanup] Hello from " << F.getName() << '\n';

  if (TAI.isListOfAtomicBlocksEmpty() ) {
    // If function does not contain __transaction_atomic blocks, then do
    // nothing.
    return false;
  }

  std::unordered_set<Instruction*> instructionsToDelete;

  for (TransactionAtomic &TA : TAI.getListOfAtomicBlocks()) {

    BasicBlock* slowPathEnterBB = TA.getSlowPathEnterBB();
    BasicBlock* slowPathExitBB = TA.getSlowPathExitBB();
    BasicBlock* fastPathEnterBB = TA.getFastPathEnterBB();
    BasicBlock* fastPathExitBB = TA.getFastPathExitBB();

    std::queue<BasicBlock*> WorkQueue;
    WorkQueue.push(slowPathEnterBB);
    WorkQueue.push(slowPathExitBB);
    WorkQueue.push(fastPathEnterBB);
    WorkQueue.push(fastPathExitBB);
    while ( ! WorkQueue.empty() ) {
      BasicBlock* currBB = WorkQueue.front();
      WorkQueue.pop();

      //currBB->print(errs(), true);
      for (Instruction &I : currBB->getInstList()) {
        if (isa<CallInst>(I)) {
          CallInst &C = cast<CallInst>(I);
          Value *calledValue = C.getCalledValue();
          if (isa<Function>(calledValue)) {
            Function* calledFunction = cast<Function>(calledValue);
            if (calledFunction && calledFunction->hasName()) {
              StringRef name = calledFunction->getName();
              if (name.compare("__begin_tm_slow_path") == 0 ||
                  name.compare("__end_tm_slow_path") == 0   ||
                  name.compare("__begin_tm_fast_path") == 0 ||
                  name.compare("__end_tm_fast_path") == 0) {
                instructionsToDelete.insert(cast<Instruction>(&C));
              }
            }
          } // If C is a direct call
        } // If CallInst
      } // ForEach Instruction
    } // While WorkQueue is not empty
  }

  for (Instruction* I : instructionsToDelete) {
    I->eraseFromParent();
  }

  return true;
}

PreservedAnalyses TransactifyCleanupPass::run(Function &F,
    FunctionAnalysisManager &AM) {
  auto &L = AM.getResult<TransactionAtomicInfoAnalysis>(F);
  bool Changed = runImpl(F, L);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
