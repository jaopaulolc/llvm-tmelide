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

struct TransactionSafeCreation : public ModulePass {
  // Pass identification, replacement for typeid
  static char ID;

  TransactionSafeCreationPass Impl;

  TransactionSafeCreation() : ModulePass(ID) {
    initializeTransactionSafeCreationPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    return Impl.runImpl(M);
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

ModulePass* createTransactionSafeCreationPass() {
  return new TransactionSafeCreation();
}

} // end namespace llvm

bool TransactionSafeCreationPass::runImpl(Function &F) {

  if (!F.hasFnAttribute(Attribute::AttrKind::TransactionSafe)) {
    return false;
  }

bool TransactionSafeCreationPass::runImpl(Module &M) {

  for (Function &F : M.getFunctionList()) {

    if (F.empty()) continue;

    if (!F.hasFnAttribute(Attribute::AttrKind::TransactionSafe)) {
      continue;
    }

    errs() << "Function '" << F.getName() << "' is transaction_safe"<< '\n';

    ValueToValueMapTy VMap;

    Function* TxSafeClone = CloneFunction(&F, VMap);
    Twine cloneName = "__transactional_clone." + Twine(F.getName());
    TxSafeClone->setName(cloneName);
    TxSafeClone->removeFnAttr(Attribute::AttrKind::TransactionSafe);
  }
  return true;
}

PreservedAnalyses TransactionSafeCreationPass::run(Module &M,
    ModuleAnalysisManager &MAM) {
  bool Changed = runImpl(M);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
