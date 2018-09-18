#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/PostDominators.h"
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
          TA.slowPathEnterBB = C.getParent();
        } else if (targetName.compare("__begin_tm_fast_path") == 0) {
          TransactionAtomic &TA = listOfAtomicBlocks.back();
          TA.fastPathEnterBB = C.getParent();
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

static void
collectLocals(const Value& v,
    std::unordered_set<const Instruction*>& S) {
  std::unordered_set<const User*> users;
  for (const User* u : v.users()) {
    if (users.count(u) == 0) {
      users.insert(u);
    }
  }
  bool changed;
  do {
    changed = false;
    for (const User* user : users) {
      for (const User* u : user->users()) {
        if (users.count(u) == 0) {
          users.insert(u);
          if (isa<StoreInst>(u)) {
            const StoreInst* SI = cast<StoreInst>(u);
            const Value* ptrOperand = SI->getPointerOperand();
            if (isa<AllocaInst>(ptrOperand)) {
              for (const User* w : ptrOperand->users()){
                users.insert(w);
              }
            }
          }
          changed = true;
        }
      }
    }
  } while (changed);
  for (const User* u : users) {
    if (isa<LoadInst>(u) || isa<StoreInst>(u)) {
      const Instruction* inst = cast<Instruction>(u);
      S.insert(inst);
      //inst->print(llvm::errs(), true);
      //llvm::errs() << '\n';
    }
  }
}


struct LocalsInfoCollector : public InstVisitor<LocalsInfoCollector> {
  TransactionAtomicInfo& TAI;
  DominatorTree& DomTree;
  PostDominatorTree& PostDomTree;
  std::unordered_set<const Instruction*>& threadLocals;
  std::unordered_set<const Instruction*>& transactionLocals;
public:
  LocalsInfoCollector(TransactionAtomicInfo& TAI,
      DominatorTree &DT, PostDominatorTree &PDT) :
    TAI(TAI), DomTree(DT), PostDomTree(PDT),
    threadLocals(TAI.getThreadLocals()),
    transactionLocals(TAI.getTransactionLocals()) {}

  void visitCallInst(CallInst &C) {
    if (isa<Function>(C.getCalledValue())) {
      Function* calledFunction = static_cast<Function*>(C.getCalledValue());
      if (calledFunction->hasName()) {
        StringRef targetName = calledFunction->getName();
        if (targetName.compare("malloc") == 0 ||
            targetName.compare("calloc") == 0) {
          const BasicBlock* callBB = C.getParent();
          for (TransactionAtomic& TA : TAI.getListOfAtomicBlocks()) {
            const BasicBlock* slowPathEnterBB =
              TA.getSlowPathEnterBB();
            std::unordered_set<BasicBlock*>& Terminators =
              TA.getTransactionTerminators();
            bool Dominates = false;
            bool PostDominates = false;
            for (BasicBlock* Term : Terminators) {
              if (DomTree.dominates(callBB, Term)) {
                Dominates = true;
                break;
              } else if ((PostDomTree.dominates(callBB, slowPathEnterBB) &&
                  DomTree.dominates(callBB, Term)) ||
                  (C.getCalledFunction() && C.getCalledFunction()->hasName() &&
                  C.getCalledFunction()->getName().startswith("__transaction_clone"))) {
                PostDominates = true;
                break;
              }
            }
            if (Dominates) {
              llvm::errs() << "malloc is outside of transaction\n";
              //C.print(llvm::errs(), true);
              //llvm::errs() << '\n';
              collectLocals(C, threadLocals);
            } else if (PostDominates) {
              llvm::errs() << "malloc is inside of transaction\n";
              collectLocals(C, transactionLocals);
            }
          }
        }
      }
    }
  }
};

} // end anonymous namespace

static const char ta_name[] = "[GNU-TM] Transaction Atomic Info Collector Pass";

char TransactionAtomicInfoPass::ID = 0;
INITIALIZE_PASS_BEGIN(TransactionAtomicInfoPass, TA_NAME, ta_name, true, true);
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(TransactionAtomicInfoPass, TA_NAME, ta_name, true, true);

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
  if (!TAI.getListOfAtomicBlocks().empty() ||
      (F.hasName() && F.getName().startswith("__transaction_clone"))) {
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    PostDominatorTree &PDT =
      getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
    LocalsInfoCollector LIC(TAI, DT, PDT);
    LIC.visit(F);
  }
  return false;
}

void TransactionAtomicInfoPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<PostDominatorTreeWrapperPass>();
}

AnalysisKey TransactionAtomicInfoAnalysis::Key;

TransactionAtomicInfo TransactionAtomicInfoAnalysis::run(Function
    &F, FunctionAnalysisManager &AM) {
  TransactionAtomicInfo TAI;
  DualPathInfoCollector DPIC(TAI.getListOfAtomicBlocks());
  DPIC.visit(F);
  return TAI;
}


