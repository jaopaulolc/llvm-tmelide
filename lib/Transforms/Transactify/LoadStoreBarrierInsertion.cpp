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
#include "llvm/IR/DerivedTypes.h"
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
  LOAD_FUNC_DECL(M128i)
  LOAD_FUNC_DECL(M128ii)
  LOAD_FUNC_DECL(M128)
  LOAD_FUNC_DECL(M128d)
  LOAD_FUNC_DECL(M256i)
  LOAD_FUNC_DECL(M256ii)
  LOAD_FUNC_DECL(M256)
  LOAD_FUNC_DECL(M256d)
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
  STORE_FUNC_DECL(M128i)
  STORE_FUNC_DECL(M128ii)
  STORE_FUNC_DECL(M128)
  STORE_FUNC_DECL(M128d)
  STORE_FUNC_DECL(M256i)
  STORE_FUNC_DECL(M256ii)
  STORE_FUNC_DECL(M256)
  STORE_FUNC_DECL(M256d)
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

#define ADD_LOAD_VECTOR_DEF(TYPE, SIZE, SUFFIX, NAME) \
  Function* addDefinitionTo##SUFFIX() { \
    LLVMContext& context = TheModule.getContext(); \
    StringRef functionName = StringRef(NAME); \
    VectorType* VT = VectorType::get(Type::get##TYPE(context), SIZE); \
    SmallVector<Type*, 1> Params; \
    Params.push_back(VT->getPointerTo()); \
    FunctionType *functionType = FunctionType::get(VT, Params, false); \
    AttributeList attrList = \
      AttributeList() \
        .addAttribute(context, \
            AttributeList::FunctionIndex, Attribute::NoInline); \
    Constant *ret = TheModule.getOrInsertFunction( \
        functionName, functionType, attrList); \
    if (isa<Function>(ret)) { \
      /*errs() <<  "successfully inserted "; \
      errs() << functionName << " definition!" << '\n'; \
      static_cast<Function*>(ret)->print(llvm::errs()); */ \
      return static_cast<Function*>(ret); \
    } else { \
      errs() <<  "failed to insert "; \
      errs() << functionName << " definition!" << '\n'; \
      return nullptr; \
    } \
  }

  ADD_LOAD_DEF(uint8_t    , RU1, "_ITM_RU1")
  ADD_LOAD_DEF(uint16_t   , RU2, "_ITM_RU2")
  ADD_LOAD_DEF(uint32_t   , RU4, "_ITM_RU4")
  ADD_LOAD_DEF(uint64_t   , RU8, "_ITM_RU8")
  ADD_LOAD_DEF(float      , RF , "_ITM_RF")
  ADD_LOAD_DEF(double     , RD , "_ITM_RD")
//  ADD_LOAD_DEF(long double, RE , "_ITM_RE")
  ADD_LOAD_VECTOR_DEF(Int32Ty , /* <4 x i32>    */4, RM128i , "_ITM_RM128i")
  ADD_LOAD_VECTOR_DEF(Int64Ty , /* <2 x i64>    */2, RM128ii, "_ITM_RM128ii")
  ADD_LOAD_VECTOR_DEF(FloatTy , /* <4 x float>  */4, RM128  , "_ITM_RM128")
  ADD_LOAD_VECTOR_DEF(DoubleTy, /* <2 x double> */2, RM128d , "_ITM_RM128d")
  ADD_LOAD_VECTOR_DEF(Int32Ty , /* <8 x i32>    */8, RM256i , "_ITM_RM256i")
  ADD_LOAD_VECTOR_DEF(Int64Ty , /* <4 x i64>    */4, RM256ii, "_ITM_RM256ii")
  ADD_LOAD_VECTOR_DEF(FloatTy , /* <8 x float>  */8, RM256  , "_ITM_RM256")
  ADD_LOAD_VECTOR_DEF(DoubleTy, /* <2 x double> */4, RM256d , "_ITM_RM256d")
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

#define ADD_STORE_VECTOR_DEF(TYPE, SIZE, SUFFIX, NAME) \
  Function* addDefinitionTo##SUFFIX() { \
    LLVMContext& context = TheModule.getContext(); \
    StringRef functionName = StringRef(NAME); \
    VectorType* VT = VectorType::get(Type::get##TYPE(context), SIZE); \
    SmallVector<Type*, 2> Params; \
    Params.push_back(VT->getPointerTo()); \
    Params.push_back(VT); \
    FunctionType *functionType = \
      FunctionType::get(Type::getVoidTy(context), Params, false); \
    AttributeList attrList = \
      AttributeList() \
        .addAttribute(context, \
            AttributeList::FunctionIndex, Attribute::NoInline); \
    Constant *ret = TheModule.getOrInsertFunction( \
        functionName, functionType, attrList); \
    if (isa<Function>(ret)) { \
      /*errs() <<  "successfully inserted "; \
      errs() << functionName << " definition!" << '\n'; \
      static_cast<Function*>(ret)->print(llvm::errs()); */ \
      return static_cast<Function*>(ret); \
    } else { \
      errs() <<  "failed to insert "; \
      errs() << functionName << " definition!" << '\n'; \
      return nullptr; \
    } \
  }

  ADD_STORE_DEF(uint8_t    , WU1, "_ITM_WU1")
  ADD_STORE_DEF(uint16_t   , WU2, "_ITM_WU2")
  ADD_STORE_DEF(uint32_t   , WU4, "_ITM_WU4")
  ADD_STORE_DEF(uint64_t   , WU8, "_ITM_WU8")
  ADD_STORE_DEF(float      , WF , "_ITM_WF")
  ADD_STORE_DEF(double     , WD , "_ITM_WD")
//  ADD_STORE_DEF(long double, WE , "_ITM_WE")
  ADD_STORE_VECTOR_DEF(Int32Ty , /* <4 x i32>    */4, WM128i , "_ITM_WM128i")
  ADD_STORE_VECTOR_DEF(Int64Ty , /* <2 x i64>    */2, WM128ii, "_ITM_WM128ii")
  ADD_STORE_VECTOR_DEF(FloatTy , /* <4 x float>  */4, WM128  , "_ITM_WM128")
  ADD_STORE_VECTOR_DEF(DoubleTy, /* <2 x double> */2, WM128d , "_ITM_WM128d")
  ADD_STORE_VECTOR_DEF(Int32Ty , /* <8 x i32>    */8, WM256i , "_ITM_WM256i")
  ADD_STORE_VECTOR_DEF(Int64Ty , /* <4 x i64>    */4, WM256ii, "_ITM_WM256ii")
  ADD_STORE_VECTOR_DEF(FloatTy , /* <8 x float>  */8, WM256  , "_ITM_WM256")
  ADD_STORE_VECTOR_DEF(DoubleTy, /* <2 x double> */4, WM256d , "_ITM_WM256d")
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
    INIT_LOAD_FUNC_FIELD(M128i)
    INIT_LOAD_FUNC_FIELD(M128ii)
    INIT_LOAD_FUNC_FIELD(M128)
    INIT_LOAD_FUNC_FIELD(M128d)
    INIT_LOAD_FUNC_FIELD(M256i)
    INIT_LOAD_FUNC_FIELD(M256ii)
    INIT_LOAD_FUNC_FIELD(M256)
    INIT_LOAD_FUNC_FIELD(M256d)
#undef INIT_LOAD_FUNC_FIELD
    INIT_STORE_FUNC_FIELD(U1)
    INIT_STORE_FUNC_FIELD(U2)
    INIT_STORE_FUNC_FIELD(U4)
    INIT_STORE_FUNC_FIELD(U8)
    INIT_STORE_FUNC_FIELD(F)
    INIT_STORE_FUNC_FIELD(D)
//    INIT_STORE_FUNC_FIELD(E)
    INIT_STORE_FUNC_FIELD(M128i)
    INIT_STORE_FUNC_FIELD(M128ii)
    INIT_STORE_FUNC_FIELD(M128)
    INIT_STORE_FUNC_FIELD(M128d)
    INIT_STORE_FUNC_FIELD(M256i)
    INIT_STORE_FUNC_FIELD(M256ii)
    INIT_STORE_FUNC_FIELD(M256)
    INIT_STORE_FUNC_FIELD(M256d)
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
  LOAD_GETTER(M128i)
  LOAD_GETTER(M128ii)
  LOAD_GETTER(M128)
  LOAD_GETTER(M128d)
  LOAD_GETTER(M256i)
  LOAD_GETTER(M256ii)
  LOAD_GETTER(M256)
  LOAD_GETTER(M256d)
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
  STORE_GETTER(M128i)
  STORE_GETTER(M128ii)
  STORE_GETTER(M128)
  STORE_GETTER(M128d)
  STORE_GETTER(M256i)
  STORE_GETTER(M256ii)
  STORE_GETTER(M256)
  STORE_GETTER(M256d)
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
  LOG_FUNC_DECL(M128i)
  LOG_FUNC_DECL(M128ii)
  LOG_FUNC_DECL(M128)
  LOG_FUNC_DECL(M128d)
  LOG_FUNC_DECL(M256i)
  LOG_FUNC_DECL(M256ii)
  LOG_FUNC_DECL(M256)
  LOG_FUNC_DECL(M256d)
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

#define ADD_LOG_VECTOR_DEF(TYPE, SIZE, SUFFIX, NAME) \
  Function* addDefinitionTo##SUFFIX() { \
    LLVMContext& context = TheModule.getContext(); \
    StringRef functionName = StringRef(NAME); \
    VectorType* VT = VectorType::get(Type::get##TYPE(context), SIZE); \
    SmallVector<Type*, 1> Params; \
    Params.push_back(VT->getPointerTo()); \
    FunctionType *functionType = \
      FunctionType::get(Type::getVoidTy(context), Params, false); \
    AttributeList attrList = \
      AttributeList() \
        .addAttribute(context, \
            AttributeList::FunctionIndex, Attribute::NoInline); \
    Constant *ret = TheModule.getOrInsertFunction( \
        functionName, functionType, attrList); \
    if (isa<Function>(ret)) { \
      /*errs() <<  "successfully inserted "; \
      errs() << functionName << " definition!" << '\n'; \
      static_cast<Function*>(ret)->print(llvm::errs()); */ \
      return static_cast<Function*>(ret); \
    } else { \
      errs() <<  "failed to insert "; \
      errs() << functionName << " definition!" << '\n'; \
      return nullptr; \
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
  ADD_LOG_VECTOR_DEF(Int32Ty , /* <4 x i32>    */4, LM128i , "_ITM_LM128i")
  ADD_LOG_VECTOR_DEF(Int64Ty , /* <2 x i64>    */2, LM128ii, "_ITM_LM128ii")
  ADD_LOG_VECTOR_DEF(FloatTy , /* <4 x float>  */4, LM128  , "_ITM_LM128")
  ADD_LOG_VECTOR_DEF(DoubleTy, /* <2 x double> */2, LM128d , "_ITM_LM128d")
  ADD_LOG_VECTOR_DEF(Int32Ty , /* <8 x i32>    */8, LM256i , "_ITM_LM256i")
  ADD_LOG_VECTOR_DEF(Int64Ty , /* <4 x i64>    */4, LM256ii, "_ITM_LM256ii")
  ADD_LOG_VECTOR_DEF(FloatTy , /* <8 x float>  */8, LM256  , "_ITM_LM256")
  ADD_LOG_VECTOR_DEF(DoubleTy, /* <2 x double> */4, LM256d , "_ITM_LM256d")
#undef ADD_LOG_DEF
#undef ADD_LOG_VECTOR_DEF

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
//    INIT_LOG_FUNC_FIELD(E)
    INIT_LOG_FUNC_FIELD(M128i)
    INIT_LOG_FUNC_FIELD(M128ii)
    INIT_LOG_FUNC_FIELD(M128)
    INIT_LOG_FUNC_FIELD(M128d)
    INIT_LOG_FUNC_FIELD(M256i)
    INIT_LOG_FUNC_FIELD(M256ii)
    INIT_LOG_FUNC_FIELD(M256)
    INIT_LOG_FUNC_FIELD(M256d)
    LogB = addDefinitionToLB();
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
  LOG_GETTER(M128i)
  LOG_GETTER(M128ii)
  LOG_GETTER(M128)
  LOG_GETTER(M128d)
  LOG_GETTER(M256i)
  LOG_GETTER(M256ii)
  LOG_GETTER(M256)
  LOG_GETTER(M256d)
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
//INITIALIZE_PASS_DEPENDENCY(SlowPathCreation)
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
  if (LoadType->isX86_MMXTy()) {
    errs() << "MMX/AVX instruction FOUND!!!!!\n";
  } else if (LoadType->isVectorTy()) {
    Type* ElementType =  LoadType->getVectorElementType();
    switch(TypeSize) {
      case 128:
        if (isa<IntegerType>(ElementType)) {
          unsigned ElementTypeSize = ElementType->getPrimitiveSizeInBits();
          switch(ElementTypeSize) {
            case 32:
                Callee = LSBarriers.getLoadM128i();
              break;
            case 64:
                Callee = LSBarriers.getLoadM128ii();
              break;
            default:
              errs() << "unsupported integer size for vector element \n";
              Load.print(errs(), true);
              errs() << '\n';
              break;
          }
        } else if (ElementType->isFloatTy()) {
          Callee = LSBarriers.getLoadM128();
        } else if (ElementType->isDoubleTy()) {
          Callee = LSBarriers.getLoadM128d();
        } else {
          errs() << "unsupported vector type \n";
          Load.print(errs(), true);
          errs() << '\n';
        }
        break;
      case 256:
        if (isa<IntegerType>(ElementType)) {
          unsigned ElementTypeSize = ElementType->getPrimitiveSizeInBits();
          switch(ElementTypeSize) {
            case 32:
                Callee = LSBarriers.getLoadM256i();
              break;
            case 64:
                Callee = LSBarriers.getLoadM256ii();
              break;
            default:
              errs() << "unsupported integer size for vector element \n";
              Load.print(errs(), true);
              errs() << '\n';
              break;
          }
        } else if (ElementType->isFloatTy()) {
          Callee = LSBarriers.getLoadM256();
        } else if (ElementType->isDoubleTy()) {
          Callee = LSBarriers.getLoadM256d();
        } else {
          errs() << "unsupported vector type \n";
          Load.print(errs(), true);
          errs() << '\n';
        }
        break;
      default:
        errs() << "unsupported vector size '" << TypeSize << "'-bit\n";
        Load.print(errs(), true);
        errs() << '\n';
        break;
    }
  } else if (LoadType->isArrayTy()) {
    errs() << "ARRAY TYPE\n";
  } else if (LoadType->isIntegerTy()) {
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
  } else if (LoadType->isPointerTy()) {
    //const PointerType * ptrType = cast<PointerType>(LoadType);
    Callee = LSBarriers.getLoadU8();
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
  if (StoreType->isX86_MMXTy()) {
    errs() << "MMX/AVX instruction FOUND!!!!!\n";
  } else if (StoreType->isVectorTy()) {
    Type* ElementType =  StoreType->getVectorElementType();
    switch(TypeSize) {
      case 128:
        if (isa<IntegerType>(ElementType)) {
          unsigned ElementTypeSize = ElementType->getPrimitiveSizeInBits();
          switch(ElementTypeSize) {
            case 32:
                Callee = LSBarriers.getStoreM128i();
              break;
            case 64:
                Callee = LSBarriers.getStoreM128ii();
              break;
            default:
              errs() << "unsupported integer size for vector element \n";
              Store.print(errs(), true);
              errs() << '\n';
              break;
          }
        } else if (ElementType->isFloatTy()) {
          Callee = LSBarriers.getStoreM128();
        } else if (ElementType->isDoubleTy()) {
          Callee = LSBarriers.getStoreM128d();
        } else {
          errs() << "unsupported vector type \n";
          Store.print(errs(), true);
          errs() << '\n';
        }
        break;
      case 256:
        if (isa<IntegerType>(ElementType)) {
          unsigned ElementTypeSize = ElementType->getPrimitiveSizeInBits();
          switch(ElementTypeSize) {
            case 32:
                Callee = LSBarriers.getStoreM256i();
              break;
            case 64:
                Callee = LSBarriers.getStoreM256ii();
              break;
            default:
              errs() << "unsupported integer size for vector element \n";
              Store.print(errs(), true);
              errs() << '\n';
              break;
          }
        } else if (ElementType->isFloatTy()) {
          Callee = LSBarriers.getStoreM256();
        } else if (ElementType->isDoubleTy()) {
          Callee = LSBarriers.getStoreM256d();
        } else {
          errs() << "unsupported vector type \n";
          Store.print(errs(), true);
          errs() << '\n';
        }
        break;
      default:
        errs() << "unsupported vector size '" << TypeSize << "'-bit\n";
        Store.print(errs(), true);
        errs() << '\n';
        break;
    }
  } else if (StoreType->isArrayTy()) {
    errs() << "ARRAY TYPE\n";
  } else if (StoreType->isIntegerTy()) {
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
  } else if (StoreType->isPointerTy()) {
    //const PointerType * ptrType = cast<PointerType>(LoadType);
    Callee = LSBarriers.getStoreU8();
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
  if (StoreType->isX86_MMXTy()) {
    errs() << "MMX/AVX instruction FOUND!!!!!\n";
  } else if (StoreType->isVectorTy()) {
    Type* ElementType =  StoreType->getVectorElementType();
    switch(TypeSize) {
      case 128:
        if (isa<IntegerType>(ElementType)) {
          unsigned ElementTypeSize = ElementType->getPrimitiveSizeInBits();
          switch(ElementTypeSize) {
            case 32:
                Callee = LBarriers.getLogM128i();
              break;
            case 64:
                Callee = LBarriers.getLogM128ii();
              break;
            default:
              errs() << "unsupported integer size for vector element \n";
              Store.print(errs(), true);
              errs() << '\n';
              break;
          }
        } else if (ElementType->isFloatTy()) {
          Callee = LBarriers.getLogM128();
        } else if (ElementType->isDoubleTy()) {
          Callee = LBarriers.getLogM128d();
        } else {
          errs() << "unsupported vector type \n";
          Store.print(errs(), true);
          errs() << '\n';
        }
        break;
      case 256:
        if (isa<IntegerType>(ElementType)) {
          unsigned ElementTypeSize = ElementType->getPrimitiveSizeInBits();
          switch(ElementTypeSize) {
            case 32:
                Callee = LBarriers.getLogM256i();
              break;
            case 64:
                Callee = LBarriers.getLogM256ii();
              break;
            default:
              errs() << "unsupported integer size for vector element \n";
              Store.print(errs(), true);
              errs() << '\n';
              break;
          }
        } else if (ElementType->isFloatTy()) {
          Callee = LBarriers.getLogM256();
        } else if (ElementType->isDoubleTy()) {
          Callee = LBarriers.getLogM256d();
        } else {
          errs() << "unsupported vector type \n";
          Store.print(errs(), true);
          errs() << '\n';
        }
        break;
      default:
        errs() << "unsupported vector size '" << TypeSize << "'-bit\n";
        Store.print(errs(), true);
        errs() << '\n';
        break;
    }
  } else if (StoreType->isArrayTy()) {
    errs() << "ARRAY TYPE\n";
  } else if (StoreType->isIntegerTy()) {
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
  std::unordered_set<Instruction*> ReplacedInst;

  if ( functionName.startswith("__transactional_clone") ) {
    for (BasicBlock &BB : F.getBasicBlockList()) {
      for (Instruction &I : BB.getInstList()) {
        if (ReplacedInst.count(&I) != 0) {
          continue;
        }
        if (TAI.getTransactionLocals().count(&I) != 0) {
          continue;
        }
        if (isa<LoadInst>(I)) {
          insertLoadBarrier(LSBarriers, I, InstructionsToDelete);
          ReplacedInst.insert(&I);
        } else if ( isa<StoreInst>(I) ) {
          insertStoreBarrier(LSBarriers, I, InstructionsToDelete);
          ReplacedInst.insert(&I);
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
      //currBB->print(errs(), true);
      for (Instruction &I : currBB->getInstList()) {
        if (ReplacedInst.count(&I) != 0) {
          continue;
        }
        if (TAI.getTransactionLocals().count(&I) != 0) {
          continue;
        }
        if ( isa<LoadInst>(I) ) {
          // If not a thread-local
          if (TAI.getThreadLocals().count(&I) == 0) {
            insertLoadBarrier(LSBarriers, I, InstructionsToDelete);
            ReplacedInst.insert(&I);
          }
        } else if ( isa<StoreInst>(I) ) {
          if (TAI.getThreadLocals().count(&I) != 0) {
            insertLogBarrier(LBarriers, I);
            ReplacedInst.insert(&I);
            continue;
          } else {
            insertStoreBarrier(LSBarriers, I, InstructionsToDelete);
            ReplacedInst.insert(&I);
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
