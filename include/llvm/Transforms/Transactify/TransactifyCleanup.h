#ifndef LLVM_TRANSFORMS_TRANSACTIFYCLEANUP_H
#define LLVM_TRANSFORMS_TRANSACTIFYCLEANUP_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/TransactionAtomicInfo.h"

namespace llvm {

struct TransactifyCleanupPass :
    public PassInfoMixin<TransactifyCleanupPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  bool runImpl(Function &F, TransactionAtomicInfo &TAI);
};

}

#endif /* LLVM_TRANSFORMS_TRANSACTIFYCLEANUP_H */
