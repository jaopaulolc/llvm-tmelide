#ifndef LLVM_TRANSFORMS_TRANSACTIONSAFECREATION_H
#define LLVM_TRANSFORMS_TRANSACTIONSAFECREATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct TransactionSafeCreationPass : public PassInfoMixin<TransactionSafeCreationPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  bool runImpl(Function &F);
};

}

#endif /* LLVM_TRANSFORMS_TRANSACTIONSAFECREATION_H */
