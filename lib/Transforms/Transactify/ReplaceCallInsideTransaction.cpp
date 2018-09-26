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
#include "llvm/Transforms/Transactify/TransactionSafeCreation.h"
#include "llvm/Transforms/Transactify/ReplaceCallInsideTransaction.h"

#include <set>
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "replacecallinsidetransaction"

namespace {

struct ReplaceCallInsideTransaction : public FunctionPass {
  // Pass identification, replacement for typeid
  static char ID;

  ReplaceCallInsideTransactionPass Impl;

  ReplaceCallInsideTransaction() : FunctionPass(ID) {
    initializeReplaceCallInsideTransactionPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto &L = getAnalysis<TransactionAtomicInfoPass>().
      getTransactionAtomicInfo();
    return Impl.runImpl(F, L);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    //AU.setPreservesCFG();
    AU.addRequired<TransactionAtomicInfoPass>();
  }

};

} // end anonymous namespace

const char * const pass_name = "[GNU-TM] Load/Store Barriers Insertion Pass";

char ReplaceCallInsideTransaction::ID = 0;
INITIALIZE_PASS_BEGIN(ReplaceCallInsideTransaction, DEBUG_TYPE, pass_name,
    false, false);
INITIALIZE_PASS_DEPENDENCY(TransactionAtomicInfoPass)
//INITIALIZE_PASS_DEPENDENCY(SlowPathCreation)
INITIALIZE_PASS_DEPENDENCY(TransactionSafeCreation)
INITIALIZE_PASS_END(ReplaceCallInsideTransaction, DEBUG_TYPE, pass_name, false,
    false);

namespace llvm {

FunctionPass* createReplaceCallInsideTransactionPass() {
  return new ReplaceCallInsideTransaction();
}

} // end namespace llvm

#define TxSafe Attribute::AttrKind::TransactionSafe

static void
replaceCall(Module* M, CallInst &C, const Function* calledFunction,
    Twine &replaceName) {
  //errs() << "Replace name: " << replaceName.str() << '\n';
  FunctionType* functionType = calledFunction->getFunctionType();
  AttributeList AttrList = calledFunction->getAttributes();
  Constant* ret = M->getOrInsertFunction(
      replaceName.str(), functionType, AttrList);
  if (isa<Function>(ret)) {
    C.setCalledFunction(cast<Function>(ret));
  } else {
    errs() << "fatal error: failed to find '" <<
      calledFunction->getName() << "' transactional_clone\n";
  }
}

static void
insertGetTMCloneCall(Module* M, CallInst &C, const Value* V) {
  LLVMContext &Context = M->getContext();
  FunctionType* functionType = TypeBuilder<void*(void*), false>::get(Context);
  Constant* ret = M->getOrInsertFunction( "_ITM_getTMCloneSafe", functionType);
  if (isa<Function>(ret)) {
    Function* getTMCloneFunction = cast<Function>(ret);
    SmallVector<Value*, 1> Args;
    Args.push_back(const_cast<Value*>(V));
    CallInst *getTMCloneCall = CallInst::Create(
        getTMCloneFunction, Args, "", &C);
    CastInst* castInst =
      CastInst::CreatePointerBitCastOrAddrSpaceCast(
        getTMCloneCall, V->getType(), "", &C);
    C.setCalledFunction(castInst);
  } else {
    errs() << "fatal error: failed to create _ITM_getTMCloneSafe\n";
  }
}

static void
replaceAndInsertCalls(Module *TheModule, Instruction &I) {
  if (isa<CallInst>(I)) {
    CallInst &C = cast<CallInst>(I);
    Value *calledValue = C.getCalledValue();
    if (isa<Function>(calledValue)) {
      Function* calledFunction = cast<Function>(calledValue);
      if (calledFunction && calledFunction->hasName()) {
        StringRef name = calledFunction->getName();
        if (calledFunction->hasFnAttribute(TxSafe)) {
          Twine cloneName = "__transactional_clone." + name;
          replaceCall(TheModule, C, calledFunction, cloneName);
        } else if (name.compare("malloc") == 0 ||
            name.compare("calloc") == 0 ||
            name.compare("free") == 0) {
          Twine txSafe = "_ITM_" + name;
          replaceCall(TheModule, C, calledFunction, txSafe);
        }
      }
    } else if (C.getCalledFunction() == nullptr) {
      //calledValue->print(errs(), true);
      //errs() << '\n';
      //calledValue->getType()->print(errs(), true);
      if (C.hasFnAttr(TxSafe)) {
        insertGetTMCloneCall(TheModule, C, calledValue);
      }
    }
  }
}

bool ReplaceCallInsideTransactionPass::runImpl(Function &F,
    TransactionAtomicInfo &TAI) {
  errs() << "[ReplaceCallInsideTransaction] Hello from " << F.getName() << '\n';

  StringRef functionName = F.getName();
  if (TAI.isListOfAtomicBlocksEmpty() &&
      !functionName.startswith("__transactional_clone")) {
    // If function does not contain __transaction_atomic blocks
    // or is not a transactional_clone, then do nothing.
    return false;
  }

  Module *M = F.getParent();

  if ( functionName.startswith("__transactional_clone") ) {
    for (BasicBlock &BB : F.getBasicBlockList()) {
      for (Instruction &I : BB.getInstList()) {
        replaceAndInsertCalls(M, I);
      }
    }
  }

  for (TransactionAtomic &TA : TAI.getListOfAtomicBlocks()) {

    BasicBlock* slowPathEnterBB = TA.getSlowPathEnterBB();

    std::unordered_set<BasicBlock*> &Terminators =
      TA.getTransactionTerminators();

    std::set<BasicBlock*> VisitedBBs;
    std::queue<BasicBlock*> WorkQueue;
    WorkQueue.push(slowPathEnterBB);
    while ( ! WorkQueue.empty() ) {
      BasicBlock* currBB = WorkQueue.front();
      WorkQueue.pop();

      VisitedBBs.insert(currBB);
      //currBB->print(errs(), true);
      for (Instruction &I : currBB->getInstList()) {
        replaceAndInsertCalls(M, I);
      }

      TerminatorInst* currBBTerminator = currBB->getTerminator();
      for (BasicBlock* currBBSucc : currBBTerminator->successors()) {
        if (Terminators.count(currBBSucc) == 0 &&
            Terminators.count(currBB) &&
            VisitedBBs.count(currBBSucc) == 0) {
          VisitedBBs.insert(currBBSucc);
          WorkQueue.push(currBBSucc);
        }
      }
    }
  }
  return true;
}

PreservedAnalyses ReplaceCallInsideTransactionPass::run(Function &F,
    FunctionAnalysisManager &AM) {
  auto &L = AM.getResult<TransactionAtomicInfoAnalysis>(F);
  bool Changed = runImpl(F, L);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
