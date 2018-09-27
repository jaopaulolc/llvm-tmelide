#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/TransactionAtomicInfo.h"
#include "llvm/Transforms/Transactify/TransactionSafeCreation.h"

#include <unordered_set>
#include <utility>

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

namespace {

void registerTransactionClones(Module &TheModule,
    std::unordered_set<std::pair<Function*, Function*>*>& Clones) {

  LLVMContext &C = TheModule.getContext();
  PointerType* VoidPtrTy =
    PointerType::get(TypeBuilder<void(void), false>::get(C), /*AS*/0);
  ArrayType* VoidPtrArrayTy = ArrayType::get(VoidPtrTy, 2*Clones.size());
  SmallVector<Constant*, 8> Pointers;
  for (std::pair<Function*,Function*>* p : Clones) {
    errs() << "created: " << p->first << ", " << p->second << "" << '\n';
    Pointers.push_back(p->first);
    Pointers.push_back(p->second);
  }
  Constant* initializer = ConstantArray::get(VoidPtrArrayTy, Pointers);
  initializer->print(errs(), true);
  GlobalVariable *TMC_LIST = new GlobalVariable(TheModule, VoidPtrArrayTy,
      /*isConstant*/true, GlobalVariable::LinkageTypes::LinkOnceAnyLinkage,
      nullptr);
  TMC_LIST->setName("__TMC_LIST__");
  TMC_LIST->setInitializer(initializer);
  TMC_LIST->setSection(".tm_clone_table");
  TMC_LIST->setAlignment(sizeof(void*));

  GlobalVariable *TMC_END = new GlobalVariable(TheModule,
      ArrayType::get(VoidPtrTy, 2), /*isConstant*/true,
      GlobalVariable::LinkageTypes::LinkOnceAnyLinkage, nullptr);
  TMC_END->setName("__TMC_END__");
  TMC_END->setAlignment(sizeof(void*));
  TMC_END->setSection(".tm_clone_table");
  TMC_END->setVisibility(GlobalVariable::VisibilityTypes::HiddenVisibility);

  SmallVector<GlobalValue*, 2> Values;
  Values.push_back(TMC_LIST);
  Values.push_back(TMC_END);
  appendToUsed(TheModule, Values);
}

} // end anonymous namespace

bool TransactionSafeCreationPass::runImpl(Module &M) {

  std::unordered_set<std::pair<Function*, Function*>*> tm_clones;
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
    tm_clones.insert(new std::pair<Function*,Function*>(&F, TxSafeClone));
  }

  if (tm_clones.empty()) {
    return false;
  } else {
    registerTransactionClones(M, tm_clones);
    return true;
  }
}

PreservedAnalyses TransactionSafeCreationPass::run(Module &M,
    ModuleAnalysisManager &MAM) {
  bool Changed = runImpl(M);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
