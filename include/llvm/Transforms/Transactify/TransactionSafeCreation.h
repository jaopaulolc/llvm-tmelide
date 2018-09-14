#ifndef LLVM_TRANSFORMS_TRANSACTIONSAFECREATION_H
#define LLVM_TRANSFORMS_TRANSACTIONSAFECREATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct TransactionSafeCreationPass : public PassInfoMixin<TransactionSafeCreationPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  bool runImpl(Module &M);
};

}

#endif /* LLVM_TRANSFORMS_TRANSACTIONSAFECREATION_H */
