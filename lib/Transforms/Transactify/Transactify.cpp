#include "llvm/Transforms/Transactify.h"
//#include "llvm-c/Initialization.h"
//#include "llvm-c/Transforms/Vectorize.h"
#include "llvm/Analysis/Passes.h"
//#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

/// Initialize all passes linked into the Transactify library.
void llvm::initializeTransactify(PassRegistry &Registry) {
  initializeTransactionSafeCreationPass(Registry);
  initializeSlowPathCreationPass(Registry);
  initializeLoadStoreBarrierInsertionPass(Registry);
  initializeReplaceCallInsideTransactionPass(Registry);
}

//void LLVMInitializeVectorization(LLVMPassRegistryRef R) {
//  initializeVectorization(*unwrap(R));
//}

// DEPRECATED: Remove after the LLVM 5 release.
//void LLVMAddBBVectorizePass(LLVMPassManagerRef PM) {
//}

//void LLVMAddLoopVectorizePass(LLVMPassManagerRef PM) {
//  unwrap(PM)->add(createLoopVectorizePass());
//}

//void LLVMAddSLPVectorizePass(LLVMPassManagerRef PM) {
//  unwrap(PM)->add(createSLPVectorizerPass());
//}
