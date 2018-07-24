#include "llvm/Transforms/Transactify/SlowPathCreation.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "slowpathcreation"

namespace {
  struct SlowPathCreation : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid

    SlowPathCreationPass Impl;

    SlowPathCreation() : FunctionPass(ID) {
      initializeSlowPathCreationPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override {
      return Impl.runImpl(F);
    }

  };
}

char SlowPathCreation::ID = 0;
INITIALIZE_PASS(SlowPathCreation, "slowpathcreation",
    "[GNU-TM] SlowPath Creation Pass", false, false);

namespace llvm {

FunctionPass* createSlowPathCreationPass() {
  return new SlowPathCreation();
}

} // end namespace llvm

bool SlowPathCreationPass::runImpl(Function &F) {
  errs() << "Hello from " <<
    F.getName() << '\n';
  return true;
}

PreservedAnalyses SlowPathCreationPass::run(Function &F,
    FunctionAnalysisManager &AM) {
  bool Changed = runImpl(F);
  if (!Changed) {
    return PreservedAnalyses::all();
  }
  return PreservedAnalyses::none();
}
