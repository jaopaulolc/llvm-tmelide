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
  std::queue<const User*> Q;
  for (const User* u : v.users()) {
    Q.push(u);
  }
  while ( ! Q.empty() ) {
    const User* val = Q.front(); Q.pop();
    for (const User* u : val->users()) {
      if (users.count(u) != 0) continue;
      if (const StoreInst* S = cast<StoreInst>(u)) {
        if (S->getPointerOperand() == val) {
          users.insert(u);
          Q.push(u);
        }
      }
      if (const LoadInst* L = cast<LoadInst>(u)) {
        if (L->getPointerOperand() == val) {
          users.insert(u);
          Q.push(u);
        }
      }
      if (const GetElementPtrInst* GEP = cast<GetElementPtrInst>(u)) {
        if (GEP->getPointerOperand() == val) {
          users.insert(u);
          Q.push(u);
        }
      }
      if (const BitCastInst* BCI = cast<BitCastInst>(u)) {
        if (BCI->llvm::User::getOperand(0) == val) {
          users.insert(u);
          Q.push(u);
        }
      }
    }
  }
  for (const User* u : users) {
    if (isa<LoadInst>(u) || isa<StoreInst>(u)) {
      const Instruction* inst = cast<Instruction>(u);
      S.insert(inst);
    }
  }
}

struct LocalsInfoCollector : public InstVisitor<LocalsInfoCollector> {
  TransactionAtomicInfo& TAI;
  DominatorTree& DomTree;
  PostDominatorTree& PostDomTree;
  std::unordered_set<const Instruction*>& threadLocals;
  std::unordered_set<const Instruction*>& transactionLocals;
  bool isClone;

  void localsDomAnalysis(Instruction& I, const BasicBlock* BB) {
    for (TransactionAtomic& TA : TAI.getListOfAtomicBlocks()) {
      const BasicBlock* slowPathEnterBB =
        TA.getSlowPathEnterBB();
      std::unordered_set<BasicBlock*>& Terminators =
        TA.getTransactionTerminators();
      bool Dominates = false;
      bool PostDominates = false;
      for (BasicBlock* Term : Terminators) {
        if (DomTree.dominates(BB, slowPathEnterBB)) {
          Dominates = true;
          break;
        } else if ((PostDomTree.dominates(BB, slowPathEnterBB) &&
            DomTree.dominates(BB, Term))) {
          PostDominates = true;
          break;
        }
      }
      if (Dominates) {
        // malloc is outside of transaction
        collectLocals(I, threadLocals);
      } else if (PostDominates) {
        // malloc is inside of transaction
        collectLocals(I, transactionLocals);
      }
    }
  }

public:
  LocalsInfoCollector(TransactionAtomicInfo& TAI,
      DominatorTree &DT, PostDominatorTree &PDT, bool isClone) :
    TAI(TAI), DomTree(DT), PostDomTree(PDT),
    threadLocals(TAI.getThreadLocals()),
    transactionLocals(TAI.getTransactionLocals()), isClone(isClone) {}

  void visitCallInst(CallInst &C) {
    if (isa<Function>(C.getCalledValue())) {
      Function* calledFunction = static_cast<Function*>(C.getCalledValue());
      if (calledFunction->hasName()) {
        StringRef targetName = calledFunction->getName();
        if (targetName.compare("malloc") == 0 ||
            (calledFunction->isIntrinsic() &&
              targetName.contains("malloc")) ||
            targetName.compare("calloc") == 0 ||
            (calledFunction->isIntrinsic() &&
              targetName.contains("calloc"))) {
          if (isClone) {
            collectLocals(C, transactionLocals);
          } else {
            localsDomAnalysis(C, C.getParent());
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
  bool isClone = false;
  if (!TAI.getListOfAtomicBlocks().empty() ||
      (isClone = (F.hasName() && F.getName().startswith("__transactional_clone")))) {
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    PostDominatorTree &PDT =
      getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
    LocalsInfoCollector LIC(TAI, DT, PDT, isClone);
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


