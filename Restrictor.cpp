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


static bool IsRestrictable(llvm::Function &F)
{
  if(F.arg_size() < 2)
    return false;

  int restrictableArguments = 0;

  for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i) {
    if(i->getType()->isPointerTy())
      restrictableArguments++;

    if(restrictableArguments == 2)
`     return true;
  }
  
  return false;
}

PreservedAnalyses Restrictor::run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &) {
  bool Changed = false;

  for (auto &Function : M)
    if(IsRestrictable(Function))
      FunctionsToRestrict.push_back(&Function);

  for (auto &Function: FunctionsToRestrict)
    do_comparison_stuff(*Function);

  return (Changed ? llvm::PreservedAnalyses::none()
                  : llvm::PreservedAnalyses::all());
}

static void SetPointerArgumentsRestricted(llvm::Function& F)
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

static void ModifyFunctionToBranch(llvm::Function& F, 
      llvm::Function *restricted, llvm::Function *originalClone)
{

  while (F.begin() != F.end())
    F.begin()->eraseFromParent();
  //for(auto &BB: F)
   // BB.eraseFromParent();

  llvm::LLVMContext &C = F.getContext();
  llvm::Type *T = llvm::Type::getInt8PtrTy(C);

  SmallVector<unsigned, 4> pointerArgumentIndexes;
  for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i)
    if(i->getType()->isPointerTy())
      pointerArgumentIndexes.push_back(i->getArgNo());

  assert(pointerArgumentIndexes.size() > 1);

  BasicBlock* entry = BasicBlock::Create(C, "entry", nullptr, nullptr);
  BasicBlock* CastBlock = BasicBlock::Create(F.getContext(), "check_alias", &F, nullptr);
  for(int i = 1; i < pointerArgumentIndexes.size(); i++)
  {
    llvm::Argument *arg0 = F.getArg(pointerArgumentIndexes[i-1]);
    llvm::Argument *arg1 = F.getArg(pointerArgumentIndexes[i]);

    IRBuilder<> CastBuilder(CastBlock);
    CastBlock = BasicBlock::Create(F.getContext(), "check_alias", &F, nullptr);
    BasicBlock* trueBlock = BasicBlock::Create(F.getContext(), "true", &F, nullptr);
    Value *Cast0 = CastBuilder.CreatePointerCast(arg0, T);
    Value *Cast1 = CastBuilder.CreatePointerCast(arg1, T);
    Value *Cmp = CastBuilder.CreateICmpEQ(Cast0, Cast1);
    Value *Br = CastBuilder.CreateCondBr(Cmp, trueBlock, CastBlock);
    
    SmallVector<llvm::Value *, 4> args;
    for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i)
      args.push_back(i);
    IRBuilder<> B(trueBlock);
    B.CreateRet(B.CreateCall(originalClone, ArrayRef<llvm::Value*>(args)));
  }

  IRBuilder<> FB(CastBlock);
  SmallVector<llvm::Value *, 4> args;
    for (auto i = F.arg_begin(), e = F.arg_end(); i != e; ++i)
      args.push_back(i);

  FB.CreateRet(FB.CreateCall(restricted, ArrayRef<llvm::Value*>(args)));
}

bool Restrictor::do_comparison_stuff(llvm::Function& F)
{
  if(!IsRestrictable(F))
    return true;

  ValueToValueMapTy VMap;
  Function *restrictedFunction = llvm::CloneFunction(&F, VMap, nullptr);
  restrictedFunction->setName(F.getName() + "_restricted");
  SetPointerArgumentsRestricted(*restrictedFunction);
  FunctionsToRestrict.push_back(restrictedFunction);

  VMap.clear();
  Function *originalFunctionClone = llvm::CloneFunction(&F, VMap, nullptr);
  originalFunctionClone->setName(F.getName() + "_original");
  FunctionsToRestrict.push_back(originalFunctionClone);

  ModifyFunctionToBranch(F, restrictedFunction, originalFunctionClone);

  errs() << "Selected: " << F.getName();
  errs() << " Generated: " << restrictedFunction->getName() << "\n";
  errs() << " Generated: " << originalFunctionClone->getName() << "\n";
  errs() << " Modified: " << F.getName() << "\n";

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

