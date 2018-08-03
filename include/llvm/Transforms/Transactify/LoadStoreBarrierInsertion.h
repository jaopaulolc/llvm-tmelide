#ifndef LLVM_TRANSFORMS_LOADSTOREBARRIERINSERTION_H
#define LLVM_TRANSFORMS_LOADSTOREBARRIERINSERTION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/TransactionAtomicInfo.h"

namespace llvm {

struct LoadStoreBarrierInsertionPass : public PassInfoMixin<LoadStoreBarrierInsertionPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  bool runImpl(Function &F, TransactionAtomicInfo &TAI);
};

}

#endif /* LLVM_TRANSFORMS_LOADSTOREBARRIERINSERTION_H */
