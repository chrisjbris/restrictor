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

#include <memory>

using namespace llvm;

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

PreservedAnalyses Restrictor::run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &) {
                                
  bool Changed = false;
  std::vector<llvm::Function*> FunctionsToRestrict;

  for (auto &F : M){
    if(IsRestrictable(F))
      FunctionsToRestrict.push_back(&F);
  }

  for (auto &Function: FunctionsToRestrict)
  {
    Changed |= Generate(*Function); // Our transform doesn't preserve
                                    // any analyses.
  }

  return (Changed ? llvm::PreservedAnalyses::none()
                  : llvm::PreservedAnalyses::all());
}

static void SetArgumentsNoAlias(llvm::Function& F)
{
    for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i) {
        if(i->getType()->isPointerTy())
        {
          llvm::LLVMContext &context = i->getContext();
          unsigned int argumentIndex = i->getArgNo(); 
          llvm::AttributeList Attributes = F.getAttributes()
                                            .addAttribute(context, 
                                                          argumentIndex + 1, 
                                                          llvm::Attribute::NoAlias); // F itself is 0
          F.setAttributes(Attributes);
        }
    }
}

static SmallVector<llvm::Value*, 4> ArgsToVector(llvm::Function& F)
{
  SmallVector<llvm::Value*, 4> Args;
  for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i)
      Args.push_back(i);

  return Args;
}

static SmallVector<unsigned, 4> GetIndexOfPointerArgs(llvm::Function& F)
{
 SmallVector<unsigned, 4> V;
 for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i)
    if(i->getType()->isPointerTy())
      V.push_back(i->getArgNo());

  return V;
}

// Add Cast and Compare instructions for the arguments
static Value* AddCastCmpBranch( llvm::Function *F,
                                llvm::Argument *Arg0, 
                                BasicBlock* AddressCheckBlock,
                                BasicBlock* NoAliasBlock,
                                BasicBlock* AliasBlock,                                
                                unsigned argumentIndex)
{
  llvm::LLVMContext &C = F->getContext();
  llvm::Type *T = llvm::Type::getInt8PtrTy(C);  // Cast all pointer args to this.
  IRBuilder<> CastBuilder(AddressCheckBlock);
  Value *Cast0 = CastBuilder.CreatePointerCast(Arg0, T);

  llvm::Argument *arg1 = F->getArg(argumentIndex);
  Value *Cast1 = CastBuilder.CreatePointerCast(arg1, T);
  Value *Cmp = CastBuilder.CreateICmpEQ(Cast0, Cast1);
  CastBuilder.CreateCondBr(Cmp, AliasBlock, NoAliasBlock);
}

static void ModifyFunctionToBranch(llvm::Function& F, 
      llvm::Function *NoAliasFunction, llvm::Function *OriginalFunctionClone)
{
  while (F.begin() != F.end())
    F.begin()->eraseFromParent();

  llvm::LLVMContext &C = F.getContext();
  
  SmallVector<unsigned, 4> pointerArgumentIndexes = GetIndexOfPointerArgs(F);

  assert(pointerArgumentIndexes.size() > 1);

  BasicBlock* AddressCheckB = BasicBlock::Create(F.getContext(), "address_check", &F, nullptr);
  BasicBlock *NoAliasB = BasicBlock::Create(C, "no_alias", &F, nullptr);
  BasicBlock* AliasB = BasicBlock::Create(C, "alias", &F, nullptr);
  SmallVector<llvm::Value *, 4> Args = ArgsToVector(F);
  IRBuilder<> CallBuilder(AliasB);
  CallBuilder.CreateRet(CallBuilder.CreateCall(OriginalFunctionClone, ArrayRef<llvm::Value*>(Args)));
  
  for(int i = 1; i < pointerArgumentIndexes.size(); i++)
  {
    llvm::Argument *arg0 = F.getArg(pointerArgumentIndexes[i]);
    for (int inner = 0; inner < i; ++inner)
      Value *Cmp = AddCastCmpBranch(&F, arg0, AddressCheckB, AliasB, NoAliasB, pointerArgumentIndexes[inner]);
  }

  // Pointers can alias
  IRBuilder<> FB(NoAliasB);
  FB.CreateRet(FB.CreateCall(NoAliasFunction, ArrayRef<llvm::Value*>(Args)));
}

bool Restrictor::Generate(llvm::Function& F)
{
  ValueToValueMapTy VMap;
  Function *restrictedFunction = llvm::CloneFunction(&F, VMap, nullptr);
  restrictedFunction->setName(F.getName() + "_restricted");
  SetArgumentsNoAlias(*restrictedFunction);

  VMap.clear();
  Function *originalFunctionClone = llvm::CloneFunction(&F, VMap, nullptr);
  originalFunctionClone->setName(F.getName() + "_original");

  ModifyFunctionToBranch(F, restrictedFunction, originalFunctionClone);

  errs() << " Optimisable: " << F.getName() << "\n";
  errs() << " Generated: " << restrictedFunction->getName() << "\n";
  errs() << " Generated: " << originalFunctionClone->getName() << "\n";
  errs() << " Modified: " << F.getName() << "\n";

  return false; // We're making no effort to preserve any analyses
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
