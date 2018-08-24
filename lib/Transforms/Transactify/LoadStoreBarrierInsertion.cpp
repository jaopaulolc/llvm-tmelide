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
#include "llvm/Transforms/Transactify/SlowPathCreation.h"
#include "llvm/Transforms/Transactify/TransactionSafeCreation.h"
#include "llvm/Transforms/Transactify/LoadStoreBarrierInsertion.h"

#include <set>
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "loadstorebarrierinsertion"

namespace {

struct LoadStoreBarrierInsertion : public FunctionPass {
  // Pass identification, replacement for typeid
  static char ID;

  LoadStoreBarrierInsertionPass Impl;

  LoadStoreBarrierInsertion() : FunctionPass(ID) {
    initializeLoadStoreBarrierInsertionPass(*PassRegistry::getPassRegistry());
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

struct LoadStoreBarriers {

  Module &TheModule;

#define LOAD_FUNC_DECL(SUFFIX) \
  Function* Read##SUFFIX;

  LOAD_FUNC_DECL(U1)
  LOAD_FUNC_DECL(U2)
  LOAD_FUNC_DECL(U4)
  LOAD_FUNC_DECL(U8)
  LOAD_FUNC_DECL(F)
  LOAD_FUNC_DECL(D)
//  LOAD_FUNC_DECL(E)
#undef LOAD_FUNC_DECL

#define STORE_FUNC_DECL(SUFFIX) \
  Function* Write##SUFFIX;

  STORE_FUNC_DECL(U1)
  STORE_FUNC_DECL(U2)
  STORE_FUNC_DECL(U4)
  STORE_FUNC_DECL(U8)
  STORE_FUNC_DECL(F)
  STORE_FUNC_DECL(D)
//  STORE_FUNC_DECL(E)
#undef STORE_FUNC_DECL

#define ADD_LOAD_DEF(TYPE, SUFFIX, NAME) \
  Function* addDefinitionTo##SUFFIX() { \
    LLVMContext& context = TheModule.getContext(); \
    StringRef functionName = StringRef(NAME); \
    FunctionType *functionType = \
      TypeBuilder<TYPE(const TYPE*), false>::get(context); \
    AttributeList attrList = \
      AttributeList() \
        .addAttribute(context, \
            AttributeList::FunctionIndex, Attribute::NoInline); \
    Constant *ret = TheModule.getOrInsertFunction( \
        functionName, functionType, attrList);  \
    if (isa<Function>(ret)) { \
      /*errs() <<  "successfully inserted "; \
      errs() << functionName << " definition!" << '\n'; \
      static_cast<Function*>(ret)->print(llvm::errs()); */\
      return static_cast<Function*>(ret); \
    } else { \
      errs() <<  "failed to insert "; \
      errs() << functionName << " definition!" << '\n'; \
      return NULL; \
    } \
  }

  ADD_LOAD_DEF(uint8_t    , RU1, "_ITM_RU1")
  ADD_LOAD_DEF(uint16_t   , RU2, "_ITM_RU2")
  ADD_LOAD_DEF(uint32_t   , RU4, "_ITM_RU4")
  ADD_LOAD_DEF(uint64_t   , RU8, "_ITM_RU8")
  ADD_LOAD_DEF(float      , RF , "_ITM_RF")
  ADD_LOAD_DEF(double     , RD , "_ITM_RD")
//  ADD_LOAD_DEF(long double, RE , "_ITM_RE")
#undef ADD_LOAD_DEF

#define ADD_STORE_DEF(TYPE, SUFFIX, NAME) \
  Function* addDefinitionTo##SUFFIX() { \
    LLVMContext& context = TheModule.getContext(); \
    StringRef functionName = StringRef(NAME); \
    FunctionType *functionType = \
      TypeBuilder<void(TYPE*, TYPE), false>::get(context); \
    AttributeList attrList = \
      AttributeList() \
        .addAttribute(context, \
            AttributeList::FunctionIndex, Attribute::NoInline); \
    Constant *ret = TheModule.getOrInsertFunction( \
        functionName, functionType, attrList);  \
    if (isa<Function>(ret)) { \
      /*errs() <<  "successfully inserted "; \
      errs() << functionName << " definition!" << '\n'; \
      static_cast<Function*>(ret)->print(llvm::errs());*/ \
      return static_cast<Function*>(ret); \
    } else { \
      errs() <<  "failed to insert "; \
      errs() << functionName << " definition!" << '\n'; \
      return NULL; \
    } \
  }

  ADD_STORE_DEF(uint8_t    , WU1, "_ITM_WU1")
  ADD_STORE_DEF(uint16_t   , WU2, "_ITM_WU2")
  ADD_STORE_DEF(uint32_t   , WU4, "_ITM_WU4")
  ADD_STORE_DEF(uint64_t   , WU8, "_ITM_WU8")
  ADD_STORE_DEF(float      , WF , "_ITM_WF")
  ADD_STORE_DEF(double     , WD , "_ITM_WD")
//  ADD_STORE_DEF(long double, WE , "_ITM_WE")
#undef ADD_STORE_DEF

#define INIT_LOAD_FUNC_FIELD(SUFFIX) \
  Read##SUFFIX = addDefinitionToR##SUFFIX();
#define INIT_STORE_FUNC_FIELD(SUFFIX) \
  Write##SUFFIX = addDefinitionToW##SUFFIX();

public:
  LoadStoreBarriers(Module &M) : TheModule(M) {
    INIT_LOAD_FUNC_FIELD(U1)
    INIT_LOAD_FUNC_FIELD(U2)
    INIT_LOAD_FUNC_FIELD(U4)
    INIT_LOAD_FUNC_FIELD(U8)
    INIT_LOAD_FUNC_FIELD(F)
    INIT_LOAD_FUNC_FIELD(D)
//    INIT_LOAD_FUNC_FIELD(E)
#undef INIT_LOAD_FUNC_FIELD
    INIT_STORE_FUNC_FIELD(U1)
    INIT_STORE_FUNC_FIELD(U2)
    INIT_STORE_FUNC_FIELD(U4)
    INIT_STORE_FUNC_FIELD(U8)
    INIT_STORE_FUNC_FIELD(F)
    INIT_STORE_FUNC_FIELD(D)
//    INIT_STORE_FUNC_FIELD(E)
#undef INIT_STORE_FUNC_FIELD
  }

#define LOAD_GETTER(SUFFIX) \
  Function* getLoad##SUFFIX() { \
    return Read##SUFFIX; \
  }

  LOAD_GETTER(U1)
  LOAD_GETTER(U2)
  LOAD_GETTER(U4)
  LOAD_GETTER(U8)
  LOAD_GETTER(F)
  LOAD_GETTER(D)
//  LOAD_GETTER(E)
#undef LOAD_GETTER

#define STORE_GETTER(SUFFIX) \
  Function* getStore##SUFFIX() { \
    return Write##SUFFIX; \
  }

  STORE_GETTER(U1)
  STORE_GETTER(U2)
  STORE_GETTER(U4)
  STORE_GETTER(U8)
  STORE_GETTER(F)
  STORE_GETTER(D)
//  STORE_GETTER(E)
#undef STORE_GETTER
};

struct LogBarriers {

  Module &TheModule;

#define LOG_FUNC_DECL(SUFFIX) \
  Function* Log##SUFFIX;

  LOG_FUNC_DECL(U1)
  LOG_FUNC_DECL(U2)
  LOG_FUNC_DECL(U4)
  LOG_FUNC_DECL(U8)
  LOG_FUNC_DECL(F)
  LOG_FUNC_DECL(D)
//  LOG_FUNC_DECL(E)
#undef LOG_FUNC_DECL
  Function* LogB;

#define ADD_LOG_DEF(TYPE, SUFFIX, NAME) \
  Function* addDefinitionTo##SUFFIX() { \
    LLVMContext& context = TheModule.getContext(); \
    StringRef functionName = StringRef(NAME); \
    FunctionType *functionType = \
      TypeBuilder<void(const TYPE*), false>::get(context); \
    AttributeList attrList = \
      AttributeList() \
        .addAttribute(context, \
            AttributeList::FunctionIndex, Attribute::NoInline); \
    Constant *ret = TheModule.getOrInsertFunction( \
        functionName, functionType, attrList);  \
    if (isa<Function>(ret)) { \
      /*errs() <<  "successfully inserted "; \
      errs() << functionName << " definition!" << '\n'; \
      static_cast<Function*>(ret)->print(llvm::errs()); */\
      return static_cast<Function*>(ret); \
    } else { \
      errs() <<  "failed to insert "; \
      errs() << functionName << " definition!" << '\n'; \
      return NULL; \
    } \
  }

  Function* addDefinitionToLB() {
    LLVMContext& context = TheModule.getContext();
    StringRef functionName = StringRef("_ITM_LB");
    FunctionType *functionType =
      TypeBuilder<void(const void*, size_t), false>::get(context);
    AttributeList attrList =
      AttributeList()
        .addAttribute(context,
            AttributeList::FunctionIndex, Attribute::NoInline);
    Constant *ret = TheModule.getOrInsertFunction(
        functionName, functionType, attrList);
    if (isa<Function>(ret)) {
      /*errs() <<  "successfully inserted ";
      errs() << functionName << " definition!" << '\n';
      static_cast<Function*>(ret)->print(llvm::errs()); */
      return static_cast<Function*>(ret);
    } else {
      errs() <<  "failed to insert ";
      errs() << functionName << " definition!" << '\n';
      return NULL;
    }
  }

  ADD_LOG_DEF(uint8_t    , LU1, "_ITM_LU1")
  ADD_LOG_DEF(uint16_t   , LU2, "_ITM_LU2")
  ADD_LOG_DEF(uint32_t   , LU4, "_ITM_LU4")
  ADD_LOG_DEF(uint64_t   , LU8, "_ITM_LU8")
  ADD_LOG_DEF(float      , LF , "_ITM_LF")
  ADD_LOG_DEF(double     , LD , "_ITM_LD")
//  ADD_LOG_DEF(long double, LE , "_ITM_LE")
#undef ADD_LOG_DEF

#define INIT_LOG_FUNC_FIELD(SUFFIX) \
  Log##SUFFIX = addDefinitionToL##SUFFIX();

public:
  LogBarriers(Module &M) : TheModule(M) {
    INIT_LOG_FUNC_FIELD(U1)
    INIT_LOG_FUNC_FIELD(U2)
    INIT_LOG_FUNC_FIELD(U4)
    INIT_LOG_FUNC_FIELD(U8)
    INIT_LOG_FUNC_FIELD(F)
    INIT_LOG_FUNC_FIELD(D)
    LogB = addDefinitionToLB();
//    INIT_LOG_FUNC_FIELD(E)
#undef INIT_LOG_FUNC_FIELD
  }

#define LOG_GETTER(SUFFIX) \
  Function* getLog##SUFFIX() { \
    return Log##SUFFIX; \
  }

  LOG_GETTER(U1)
  LOG_GETTER(U2)
  LOG_GETTER(U4)
  LOG_GETTER(U8)
  LOG_GETTER(F)
  LOG_GETTER(D)
//  LOG_GETTER(E)
#undef LOG_GETTER
  Function* getLogB() {
    return LogB;
  }
};

} // end anonymous namespace

char LoadStoreBarrierInsertion::ID = 0;
INITIALIZE_PASS_BEGIN(LoadStoreBarrierInsertion, "loadstorebarrierinsertion",
    "[GNU-TM] Load/Store Barriers Insertion Pass", false, false);
INITIALIZE_PASS_DEPENDENCY(TransactionAtomicInfoPass)
INITIALIZE_PASS_DEPENDENCY(SlowPathCreation)
INITIALIZE_PASS_DEPENDENCY(TransactionSafeCreation)
INITIALIZE_PASS_END(LoadStoreBarrierInsertion, "loadstorebarrierinsertion",
    "[GNU-TM] Load/Store Barriers Insertion Pass", false, false);

namespace llvm {

FunctionPass* createLoadStoreBarrierInsertionPass() {
  return new LoadStoreBarrierInsertion();
}

} // end namespace llvm

static void insertLoadBarrier(LoadStoreBarriers &LSBarriers,
    Instruction &I, std::unordered_set<Instruction*> &InstructionsToDelete) {
  LoadInst &Load = cast<LoadInst>(I);
  Type *LoadType = Load.getType();
  Function* Callee = nullptr;
  std::vector<Value*> Args(1);
  unsigned TypeSize = LoadType->getPrimitiveSizeInBits();
  Args[0] = Load.getPointerOperand();
  if (LoadType->isIntegerTy() ||
      (TypeSize &&
       !LoadType->isFloatTy() && !LoadType->isDoubleTy()) ) {
    switch(TypeSize) {
      case 8:
        Callee = LSBarriers.getLoadU1();
        break;
      case 16:
        Callee = LSBarriers.getLoadU2();
        break;
      case 32:
        Callee = LSBarriers.getLoadU4();
        break;
      case 64:
        Callee = LSBarriers.getLoadU8();
        break;
      default:
        errs() << "unsupported int size '" << TypeSize << "'-bit\n";
        break;
    }
  } else if (LoadType->isFloatTy()) {
    switch(TypeSize) {
      case 32:
        Callee = LSBarriers.getLoadF();
        break;
      default:
        errs() << "unsupported float size '" << TypeSize << "'-bit\n";
        break;
    }
  } else if (LoadType->isDoubleTy()) {
    switch(TypeSize) {
      case 64:
        Callee = LSBarriers.getLoadD();
        break;
      default:
        errs() << "unsupported double size '" << TypeSize << "'-bit\n";
        break;
    }
  }
  if (Callee != nullptr) {
    CallInst *C = CallInst::Create(Callee, Args,/*name*/"", &Load);
    Load.replaceAllUsesWith(C);
    InstructionsToDelete.insert(&Load);
  }
}

static void insertStoreBarrier(LoadStoreBarriers &LSBarriers,
    Instruction &I, std::unordered_set<Instruction*> &InstructionsToDelete) {
  StoreInst &Store = cast<StoreInst>(I);
  Type *StoreType = Store.getValueOperand()->getType();
  Function* Callee = nullptr;
  std::vector<Value*> Args(2);
  Args[0] = Store.getPointerOperand();
  Args[1] = Store.getValueOperand();
  unsigned TypeSize = StoreType->getPrimitiveSizeInBits();
  if (StoreType->isIntegerTy() ||
      (TypeSize &&
       !StoreType->isFloatTy() && !StoreType->isDoubleTy()) ) {
    switch(TypeSize) {
      case 8:
        Callee = LSBarriers.getStoreU1();
        break;
      case 16:
        Callee = LSBarriers.getStoreU2();
        break;
      case 32:
        Callee = LSBarriers.getStoreU4();
        break;
      case 64:
        Callee = LSBarriers.getStoreU8();
        break;
      default:
        errs() << "unsupported int size '" << TypeSize << "'-bit\n";
        break;
    }
  } else if (StoreType->isFloatTy()) {
    switch(TypeSize) {
      case 32:
        Callee = LSBarriers.getStoreF();
        break;
      default:
        errs() << "unsupported float size '" << TypeSize << "'-bit\n";
        break;
    }
  } else if (StoreType->isDoubleTy()) {
    switch(TypeSize) {
      case 64:
        Callee = LSBarriers.getStoreD();
        break;
      default:
        errs() << "unsupported double size '" << TypeSize << "'-bit\n";
        break;
    }
  }
  if (Callee != nullptr) {
    CallInst::Create(Callee, Args,/*name*/"", &Store);
    InstructionsToDelete.insert(&Store);
  }
}

static void insertLogBarrier(LogBarriers &LBarriers, Instruction &I) {
  StoreInst &Store = cast<StoreInst>(I);
  Type *StoreType = Store.getValueOperand()->getType();
  Function* Callee = nullptr;
  std::vector<Value*> Args(1);
  Args[0] = Store.getPointerOperand();
  unsigned TypeSize = StoreType->getPrimitiveSizeInBits();
  if (StoreType->isIntegerTy() ||
      (TypeSize &&
       !StoreType->isFloatTy() && !StoreType->isDoubleTy()) ) {
    switch(TypeSize) {
      case 8:
        Callee = LBarriers.getLogU1();
        break;
      case 16:
        Callee = LBarriers.getLogU2();
        break;
      case 32:
        Callee = LBarriers.getLogU4();
        break;
      case 64:
        Callee = LBarriers.getLogU8();
        break;
      default:
        errs() << "unsupported int size '" << TypeSize << "'-bit\n";
        break;
    }
  } else if (StoreType->isFloatTy()) {
    switch(TypeSize) {
      case 32:
        Callee = LBarriers.getLogF();
        break;
      default:
        errs() << "unsupported float size '" << TypeSize << "'-bit\n";
        break;
    }
  } else if (StoreType->isDoubleTy()) {
    switch(TypeSize) {
      case 64:
        Callee = LBarriers.getLogD();
        break;
      default:
        errs() << "unsupported double size '" << TypeSize << "'-bit\n";
        break;
    }
  }
  if (Callee != nullptr) {
    CallInst::Create(Callee, Args,/*name*/"", &Store);
  }
}

bool LoadStoreBarrierInsertionPass::runImpl(Function &F,
    TransactionAtomicInfo &TAI) {
  errs() << "[LoadStoreBarrierInsertion] Hello from " << F.getName() << '\n';

  StringRef functionName = F.getName();
  if (TAI.isListOfAtomicBlocksEmpty() &&
      !functionName.startswith("__transactional_clone")) {
    // If function does not contain __transaction_atomic blocks
    // or is not a transactional_clone, then do nothing.
    return false;
  }

  LoadStoreBarriers LSBarriers(*F.getParent());
  LogBarriers LBarriers(*F.getParent());

  std::unordered_set<Instruction*> InstructionsToDelete;

  if ( functionName.startswith("__transactional_clone") ) {
    for (BasicBlock &BB : F.getBasicBlockList()) {
      for (Instruction &I : BB.getInstList()) {
        if (TAI.getTransactionLocals().count(&I) != 0) {
          continue;
        }
        if (isa<LoadInst>(I)) {
          insertLoadBarrier(LSBarriers, I, InstructionsToDelete);
        } else if ( isa<StoreInst>(I) ) {
          insertStoreBarrier(LSBarriers, I, InstructionsToDelete);
        }
      }
    }
  }

  for (TransactionAtomic &TA : TAI.getListOfAtomicBlocks()) {

    BasicBlock* slowPathEnterBB = TA.getSlowPathEnterBB();

    std::unordered_set<BasicBlock*> &transactionTerminators =
      TA.getTransactionTerminators();

    std::set<BasicBlock*> VisitedBBs;
    std::queue<BasicBlock*> WorkQueue;
    WorkQueue.push(slowPathEnterBB);
    while ( ! WorkQueue.empty() ) {
      BasicBlock* currBB = WorkQueue.front();
      WorkQueue.pop();

      VisitedBBs.insert(currBB);
      currBB->print(errs(), true);
      for (Instruction &I : currBB->getInstList()) {
        if (TAI.getTransactionLocals().count(&I) != 0) {
          continue;
        }
        if ( isa<LoadInst>(I) ) {
          // If not a thread-local
          if (TAI.getThreadLocals().count(&I) == 0) {
            insertLoadBarrier(LSBarriers, I, InstructionsToDelete);
          }
        } else if ( isa<StoreInst>(I) ) {
          if (TAI.getThreadLocals().count(&I) != 0) {
            llvm::errs() << "LOG THIS: ";
            I.print(llvm::errs(), true);
            llvm::errs() << '\n';
            insertLogBarrier(LBarriers, I);
            continue;
          } else {
            insertStoreBarrier(LSBarriers, I, InstructionsToDelete);
          }
        }
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
  }

  for (Instruction* I : InstructionsToDelete) {
    I->eraseFromParent();
  }

  return true;
}

PreservedAnalyses LoadStoreBarrierInsertionPass::run(Function &F,
    FunctionAnalysisManager &AM) {
  auto &L = AM.getResult<TransactionAtomicInfoAnalysis>(F);
  bool Changed = runImpl(F, L);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
