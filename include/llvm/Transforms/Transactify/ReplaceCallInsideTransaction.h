#ifndef LLVM_TRANSFORMS_REPLACECALLINSIDETRANSACTION_H
#define LLVM_TRANSFORMS_REPLACECALLINSIDETRANSACTION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/TransactionAtomicInfo.h"

namespace llvm {

struct ReplaceCallInsideTransactionPass :
    public PassInfoMixin<ReplaceCallInsideTransactionPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  bool runImpl(Function &F, TransactionAtomicInfo &TAI);
};

}

#endif /* LLVM_TRANSFORMS_REPLACECALLINSIDETRANSACTION_H */
