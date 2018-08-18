#ifndef LLVM_ANALYSIS_TRANSACTIONATOMICINFO_H
#define LLVM_ANALYSIS_TRANSACTIONATOMICINFO_H

#include "llvm/IR/PassManager.h"

#include <unordered_set>

namespace llvm {

class DominatorTree;

struct TransactionAtomic {

  BasicBlock* fastPathEnterBB;
  BasicBlock* fastPathExitBB;
  BasicBlock::iterator beginSlowPathCall;
  BasicBlock::iterator endSlowPathCall;
  BasicBlock* transactionEntryBB;
  std::unordered_set<BasicBlock*> transactionTerminators;

public:
  TransactionAtomic() {
    fastPathEnterBB = nullptr;
    fastPathExitBB = nullptr;
    transactionEntryBB = nullptr;
  }
  BasicBlock* getFastPathEnterBB() {
    return fastPathEnterBB;
  }
  BasicBlock* getFastPathExitBB() {
    return fastPathExitBB;
  }
  BasicBlock::iterator getBeginSlowPathCall() {
    return beginSlowPathCall;
  }
  BasicBlock::iterator getEndSlowPathCall() {
    return endSlowPathCall;
  }
  BasicBlock* getTransactionEntry() {
    return transactionEntryBB;
  }
  void insertTransactionTerminator(BasicBlock* TerminatorBB){
    transactionTerminators.insert(TerminatorBB);
  }
  std::unordered_set<BasicBlock*>& getTransactionTerminators() {
    return transactionTerminators;
  }
};

struct TransactionAtomicInfo {

  std::list<TransactionAtomic> listOfAtomicBlocks;
  std::unordered_set<const Instruction*> threadLocals;
  std::unordered_set<const Instruction*> transactionLocals;
public:
  bool isListOfAtomicBlocksEmpty() {
    return listOfAtomicBlocks.empty();
  }
  std::list<TransactionAtomic>& getListOfAtomicBlocks() {
    return listOfAtomicBlocks;
  }
  std::unordered_set<const Instruction*>& getThreadLocals() {
    return threadLocals;
  }
  std::unordered_set<const Instruction*>& getTransactionLocals() {
    return transactionLocals;
  }
};

struct TransactionAtomicInfoPass : public FunctionPass {
  // Pass identification, replacement for typeid
  static char ID;

  TransactionAtomicInfo TAI;

public:
  TransactionAtomicInfoPass() : FunctionPass(ID) {
    initializeTransactionAtomicInfoPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  TransactionAtomicInfo& getTransactionAtomicInfo() {
    return TAI;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

};

FunctionPass* createTransactionAtomicInfoPass();

class TransactionAtomicInfoAnalysis : public AnalysisInfoMixin<TransactionAtomicInfoAnalysis> {
  friend AnalysisInfoMixin<TransactionAtomicInfoAnalysis>;
  static AnalysisKey Key;

public:
  typedef TransactionAtomicInfo Result;

  Result run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif /* LLVM_ANALYSIS_TRANSACTIONATOMICINFO_H */
