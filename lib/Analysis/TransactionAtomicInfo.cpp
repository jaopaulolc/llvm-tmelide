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

using namespace llvm;

#define TA_NAME "transactionatomicinfopass"
#define DEBUG_TYPE TA_NAME

namespace {

struct DualPathInfoCollector : public InstVisitor<DualPathInfoCollector> {

  std::list<TransactionAtomic>& listOfAtomicBlocks;

  DualPathInfoCollector(std::list<TransactionAtomic> &L) :
    listOfAtomicBlocks(L) {}

  void visitCallInst(CallInst &C) {
    if (isa<Function>(C.getCalledValue())) {
      Function* calledFunction = static_cast<Function*>(C.getCalledValue());
      if (calledFunction->hasName()) {
        StringRef targetName = calledFunction->getName();
        if (targetName.compare("__begin_tm_slow_path") == 0) {
          TransactionAtomic &TA = listOfAtomicBlocks.back();
          TA.beginSlowPathCall = C.getIterator();
        } else if (targetName.compare("__end_tm_slow_path") == 0) {
          TransactionAtomic &TA = listOfAtomicBlocks.back();
          TA.endSlowPathCall = C.getIterator();
        } else if (targetName.compare("__begin_tm_fast_path") == 0) {
          TransactionAtomic &TA = listOfAtomicBlocks.back();
          TA.fastPathEnterBB = C.getParent();
        } else if (targetName.compare("__end_tm_fast_path") == 0) {
          TransactionAtomic &TA = listOfAtomicBlocks.back();
          TA.fastPathExitBB = C.getParent();
        } else if (targetName.compare("_ITM_beginTransaction") == 0) {
          TransactionAtomic TA;
          TA.transactionEntryBB = C.getParent();
          listOfAtomicBlocks.push_back(TA);
        } else if (targetName.compare("_ITM_commitTransaction") == 0) {
          TransactionAtomic &TA = listOfAtomicBlocks.back();
          TA.transactionTerminators.insert(C.getParent());
        }
      }
    }
  }
};

} // end anonymous namespace

static const char ta_name[] = "[GNU-TM] Transaction Atomic Info Collector Pass";

char TransactionAtomicInfoPass::ID = 0;
INITIALIZE_PASS(TransactionAtomicInfoPass, "transactionatomicinfopass",
    "[GNU-TM] Transaction Atomic Info Collector Pass", true, true);

namespace llvm {

FunctionPass* createTransactionAtomicInfoPass() {
  return new TransactionAtomicInfoPass();
}

} // end namespace llvm

bool TransactionAtomicInfoPass::runOnFunction(Function &F) {
  errs() << "[TransactionAtomicInfoPass] Hello from " << F.getName() << '\n';
  TAI.getListOfAtomicBlocks().clear();
  DualPathInfoCollector DPIC(TAI.getListOfAtomicBlocks());
  DPIC.visit(F);
  return false;
}

AnalysisKey TransactionAtomicInfoAnalysis::Key;

TransactionAtomicInfo TransactionAtomicInfoAnalysis::run(Function
    &F, FunctionAnalysisManager &AM) {
  TransactionAtomicInfo TAI;
  DualPathInfoCollector DPIC(TAI.getListOfAtomicBlocks());
  DPIC.visit(F);
  return TAI;
}


