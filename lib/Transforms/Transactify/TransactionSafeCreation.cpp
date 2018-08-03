#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/TransactionAtomicInfo.h"
#include "llvm/Transforms/Transactify/TransactionSafeCreation.h"

using namespace llvm;

#define DEBUG_TYPE "clonetransactionsafe"

namespace {

struct TransactionSafeCreation : public FunctionPass {
  // Pass identification, replacement for typeid
  static char ID;

  TransactionSafeCreationPass Impl;

  TransactionSafeCreation() : FunctionPass(ID) {
    initializeTransactionSafeCreationPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    return Impl.runImpl(F);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<TransactionAtomicInfoPass>();
  }
};

} // end anonymous namespace

char TransactionSafeCreation::ID = 0;
INITIALIZE_PASS(TransactionSafeCreation, "clonetransactionsafe",
    "[GNU-TM] Transaction Safe Functions Creation Pass", false, false);

namespace llvm {

FunctionPass* createTransactionSafeCreationPass() {
  return new TransactionSafeCreation();
}

} // end namespace llvm

bool TransactionSafeCreationPass::runImpl(Function &F) {

  if (!F.hasFnAttribute(Attribute::AttrKind::TransactionSafe)) {
    return false;
  }

  errs() << "Function '" << F.getName() << "' is transaction_safe"<< '\n';

  ValueToValueMapTy VMap;

  Function* TxSafeClone = CloneFunction(&F, VMap);
  TxSafeClone->setName("__transactional_clone." + F.getName());
  TxSafeClone->removeFnAttr(Attribute::AttrKind::TransactionSafe);

  return true;
}

PreservedAnalyses TransactionSafeCreationPass::run(Function &F,
    FunctionAnalysisManager &AM) {
  bool Changed = runImpl(F);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
