#ifndef LLVM_TRANSFORMS_TRANSACTIFY_H
#define LLVM_TRANSFORMS_TRANSACTIFY_H

namespace llvm {

class FunctionPass;

FunctionPass* createSlowPathCreationPass();

}

#endif /* LLVM_TRANSFORMS_TRANSACTIFY_H */
