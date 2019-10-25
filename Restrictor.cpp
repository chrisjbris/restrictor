// # Define your pass pipeline via the '-passes' flag
//  opt -load-pass-plugin=restrictor.dylib -passes="restrictor" 
// -disable-output <input-llvm-file>
//
//=============================================================================
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "Restrictor.h"


using namespace llvm;

PreservedAnalyses Restrictor::run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &) {
  bool Changed = false;

  for (auto &Function : M)
    Changed |= do_comparison_stuff(Function);

  M.getFunctionList().insert(M.end(), NewFunctions.begin(), NewFunctions.end());

  return (Changed ? llvm::PreservedAnalyses::none()
                  : llvm::PreservedAnalyses::all());
}

static bool IsRestrictable(llvm::Function &F)
{
  if(F.arg_size() < 2)
    return false;

  int restrictableArguments = 0;

  for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i) {
    if(i->getType()->isPointerTy())
      restrictableArguments++;

    if(restrictableArguments == 2)
      return true;
  }

  return false;
}

static void SetPointerArgumentsRestricted(llvm::Function &F)
{
    for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i) {
        if(i->getType()->isPointerTy())
        {
          llvm::LLVMContext &context = i->getContext();
          unsigned int argumentIndex = i->getArgNo(); 
          llvm::AttributeList Attributes = F.getAttributes()
                                            .addAttribute(context, 
                                                          argumentIndex, 
                                                          llvm::Attribute::NoAlias); // F itself is 0
          F.setAttributes(Attributes);
        }
    }
}

bool Restrictor::do_comparison_stuff(llvm::Function &F)
{
  if(!IsRestrictable(F))
    return true;

  ValueToValueMapTy VMap;
  // build map from old to new arguments

  Function *restrictedFunction = llvm::CloneFunction(&F, VMap, nullptr);
  restrictedFunction->setName(F.getName() + "_restricted");
  NewFunctions.push_back(restrictedFunction);

  VMap.clear();
  Function *originalFunctionClone = llvm::CloneFunction(&F, VMap, nullptr);
  originalFunctionClone->setName(F.getName() + "_original");
  NewFunctions.push_back(originalFunctionClone);

  errs() << "Visiting: " << F.getName();
  errs() << " Generated: " << restrictedFunction->getName() << "\n";


  return false; // analyses preserved?
}

// Register pass
llvm::PassPluginLibraryInfo getRestrictorPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Restrictor", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "restrictor") {
                    MPM.addPass(Restrictor());
                    return true;
                  }
                  return false;
                });
          }};
}

// interface for pass plugins
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getRestrictorPluginInfo();
}

