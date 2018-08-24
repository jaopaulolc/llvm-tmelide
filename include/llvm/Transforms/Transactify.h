#ifndef LLVM_TRANSFORMS_TRANSACTIFY_H
#define LLVM_TRANSFORMS_TRANSACTIFY_H

namespace llvm {

class FunctionPass;

FunctionPass* createSlowPathCreationPass();

FunctionPass* createTransactionSafeCreationPass();

FunctionPass* createLoadStoreBarrierInsertionPass();

FunctionPass* createReplaceCallInsideTransactionPass();

}

#endif /* LLVM_TRANSFORMS_TRANSACTIFY_H */
