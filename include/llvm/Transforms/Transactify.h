#ifndef LLVM_TRANSFORMS_TRANSACTIFY_H
#define LLVM_TRANSFORMS_TRANSACTIFY_H

namespace llvm {

class FunctionPass;

FunctionPass* createSlowPathCreationPass();

FunctionPass* createTransactionSafeCreationPass();

FunctionPass* createLoadStoreBarrierInsertionPass();

FunctionPass* createReplaceCallInsideTransactionPass();

FunctionPass* createTransactifyCleanupPass();

}

#endif /* LLVM_TRANSFORMS_TRANSACTIFY_H */
