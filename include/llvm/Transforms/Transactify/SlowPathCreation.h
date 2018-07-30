#ifndef LLVM_TRANSFORMS_SLOWPATHCREATION_H
#define LLVM_TRANSFORMS_SLOWPATHCREATION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/TransactionAtomicInfo.h"

namespace llvm {

struct SlowPathCreationPass : public PassInfoMixin<SlowPathCreationPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  bool runImpl(Function &F, TransactionAtomicInfo &TAI);
};

}

#endif /* LLVM_TRANSFORMS_SLOWPATHCREATION_H */
