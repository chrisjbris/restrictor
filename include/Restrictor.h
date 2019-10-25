
#ifndef RESTRICTOR_H
#define RESTRICTOR_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

struct Restrictor : public llvm::PassInfoMixin<Restrictor> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &);
  bool do_comparison_stuff(llvm::Function &F);
};

#endif