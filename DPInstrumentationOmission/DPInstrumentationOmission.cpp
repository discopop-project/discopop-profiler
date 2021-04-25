#include "DPInstrumentationOmission.h"

#define DEBUG_TYPE "dp-omissions"

#define DP_DEBUG true

STATISTIC(totalInstrumentations, "Total DP-Instrumentations");
STATISTIC(removedInstrumentations, "Disregarded DP-Instructions");

static cl::opt<bool> DumpToDot(
  "dp-omissions-dump-dot", cl::init(false),
  cl::desc("Generate a .dot representation of the CFG and DG"), cl::Hidden
);

StringRef DPInstrumentationOmission::getPassName() const{
  return "DPInstrumentationOmission";
}

void DPInstrumentationOmission::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
}

bool DPInstrumentationOmission::runOnModule(Module &M) {
  int bbDepCount = 0;
  string bbDepString;

  for(Function &F: M){
    if(F.getInstructionCount() == 0) continue;
    if(DP_DEBUG) errs() << "\n---------- Omission Analysis on " << F.getName() << " ----------\n";

    DebugLoc dl;
    Value *V;

    set<Instruction*> omittableInstructions;

    set<Value*> staticallyPredictableValues;
    set<Value*> tempToBeErased;

    // Get local values (variables)
    for (Instruction& I: F.getEntryBlock()) {
      if (AllocaInst* AI = dyn_cast<AllocaInst>(&I)) {
        staticallyPredictableValues.insert(AI);
      }
    }
    for (BasicBlock& BB: F){ for(Instruction& I: BB){
      // Remove from staticallyPredictableValues those which are passed to other functions (by ref/ptr)
      if(CallInst* call_inst = dyn_cast<CallInst>(&I)){
        if(Function *Fun = call_inst->getCalledFunction()){
          if(Fun->getName() == "__dp_write" || Fun->getName() == "__dp_read" || Fun->getName() == "__dp_alloca"){
            ++totalInstrumentations;
          }
          for(uint i = 0; i < call_inst->getNumOperands() - 1; ++i){
            V = call_inst->getArgOperand(i);
            tempToBeErased.clear();
            for(Value *w: staticallyPredictableValues){
              if(w == V){
                tempToBeErased.insert(V);
              }
            }
            for(Value* v: tempToBeErased){
              staticallyPredictableValues.erase(v);
            }
          }
        }
      }
      // Remove values from locals if dereferenced
      if(isa<StoreInst>(I)){
        V = I.getOperand(0);
        tempToBeErased.clear();
        for(Value *w: staticallyPredictableValues){
          if(w == V){
            tempToBeErased.insert(V);
          }
        }
        for(Value* v: tempToBeErased){
          staticallyPredictableValues.erase(v);
        }
      }
    }}

    if(DP_DEBUG){
      errs() << "--- Local Values ---\n";
      for(auto V: staticallyPredictableValues){
        errs() << VNF->getVarName(V) << "\n";
      }
    }

    // Perform the SPA dependence analysis
    int32_t fid;
    determineFileID(F, fid);
    map<BasicBlock*, set<string>> conditionalBBDepMap;
    map<BasicBlock*, map<BasicBlock*, set<string>>> conditionalBBPairDepMap;

    auto &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    InstructionCFG CFG(VNF, F);
    InstructionDG DG(VNF, &CFG, fid);

    for(auto edge : DG.getEdges()){
      Instruction* Src = edge->getSrc()->getItem();
      Instruction* Dst = edge->getDst()->getItem();

      V = Src->getOperand(isa<StoreInst>(Src) ? 1 : 0);
      if(isa<AllocaInst>(Dst)) V = dyn_cast<Value>(Dst);

      if(staticallyPredictableValues.find(V) == staticallyPredictableValues.end()) continue;

      if(Src != Dst && DT.dominates(Dst, Src)){
        if(!conditionalBBDepMap.count(Src->getParent())){
          set<string> tmp;
          conditionalBBDepMap[Src->getParent()] = tmp;
        }
        conditionalBBDepMap[Src->getParent()].insert(DG.edgeToDPDep(edge));
      }
      else{
        if(!conditionalBBPairDepMap.count(Dst->getParent())){
          map<BasicBlock*, set<string>> tmp;
          conditionalBBPairDepMap[Dst->getParent()] = tmp;
        }
        if(!conditionalBBPairDepMap[Dst->getParent()].count(Src->getParent())){
          set<string> tmp;
          conditionalBBPairDepMap[Dst->getParent()][Src->getParent()] = tmp;
        }
        conditionalBBPairDepMap[Dst->getParent()][Src->getParent()].insert(DG.edgeToDPDep(edge));
      }
      omittableInstructions.insert(Src);
      omittableInstructions.insert(Dst);
    }

    // Omit SPA instructions with no dependences
    for(auto node: DG.getNodes()){
      if(!isa<StoreInst>(node->getItem()) && !!isa<LoadInst>(node->getItem())) continue;
      V = node->getItem()->getOperand(isa<StoreInst>(node->getItem()) ? 1 : 0);
      if(!DG.getInEdges(node).size() && !DG.getOutEdges(node).size() && staticallyPredictableValues.find(V) != staticallyPredictableValues.end())
        omittableInstructions.insert(node->getItem());
    }

    // Add observation of execution of single basic blocks
    for(auto pair : conditionalBBDepMap){
      // Insert call to reportbb
      Instruction* insertionPoint = pair.first->getTerminator();
      if(isa<ReturnInst>(pair.first->getTerminator())){
        insertionPoint = insertionPoint->getPrevNonDebugInstruction();
      }
      auto CI = CallInst::Create(
        ReportBB,
        ConstantInt::get(Int32, bbDepCount),
        "",
        insertionPoint
      );

      // ---- Insert deps into string ----
      if(bbDepCount) bbDepString += "/";
      bool first = true;
      bbDepString += to_string(bbDepCount) + "=";
      for(auto dep: pair.second){
        if(!first) bbDepString += ",";
        bbDepString += dep;
        first=false;
      }
      // ---------------------------------
      ++bbDepCount;
    }

    // Add observation of in-order execution of pairs of basic blocks
    for(auto pair1 : conditionalBBPairDepMap){
      // Alloca and init semaphore var for BB
      auto AI = new AllocaInst(Int32, 0, "__dp_bb", F.getEntryBlock().getFirstNonPHI()->getNextNonDebugInstruction());
      new StoreInst(ConstantInt::get(Int32, 0), AI, false, AI->getNextNonDebugInstruction());

      for(auto pair2: pair1.second){
        // Insert check for semaphore
        Instruction* insertionPoint = pair2.first->getTerminator();
        if(isa<ReturnInst>(pair2.first->getTerminator()))
          insertionPoint = insertionPoint->getPrevNonDebugInstruction();

        auto LI = new LoadInst(AI, Twine(""), false, insertionPoint);
        ArrayRef< Value * > arguments({LI, ConstantInt::get(Int32, bbDepCount)});
        CallInst::Create(
          ReportBBPair,
          arguments,
          "",
          insertionPoint
        );

        // ---- Insert deps into string ----
        if(bbDepCount) bbDepString += "/";
        bbDepString += to_string(bbDepCount);
        bbDepString += "=";
        bool first = true;
        for(auto dep: pair2.second){
          if(!first) bbDepString += ",";
          bbDepString += dep;
          first=false;
        }
        // ----------------------------------
        ++bbDepCount;
      }
      // Insert semaphore update to true
      new StoreInst(ConstantInt::get(Int32, 1), AI, false, pair1.first->getTerminator());
    }

    if(DumpToDot){
      CFG.dumpToDot(fileName + "_" + string(F.getName()) + ".CFG.dot");
      DG.dumpToDot(fileName + "_" + string(F.getName()) + ".DG.dot");
    }

    if(DP_DEBUG){
      errs() << "--- Conditional BB Dependences:\n";
      for(auto pair : conditionalBBDepMap){
        errs() << pair.first->getName() << ":\n";
        for(auto s: pair.second){
          errs() << "\t" << s << "\n";
        }
      }

      errs() << "--- Conditional BB-Pair Dependences:\n";
      for(auto pair1 : conditionalBBPairDepMap){
        for(auto pair2: pair1.second){
          errs() << pair1.first->getName() << "-";
          errs() << pair2.first->getName() << ":\n";
          for(auto s: pair2.second)
            errs() << "\t" << s << "\n";
        }
      }
    }

    if(DP_DEBUG){
      errs() << "--- Program Instructions:\n";
      for (BasicBlock& BB: F){ for(Instruction& I: BB){
        if(!isa<StoreInst>(I) && !isa<LoadInst>(I) && !isa<AllocaInst>(I)) continue;
        errs() << "\t" << (isa<StoreInst>(I) ? "Write " : (isa<AllocaInst>(I) ? "Alloca " : "Read ")) << " | ";
        if(dl = I.getDebugLoc()) errs() << dl.getLine() << "," << dl.getCol();
        else errs() << F.getSubprogram()->getLine() << ",*";
        errs() << " | ";
        V = I.getOperand(isa<StoreInst>(I) ? 1 : 0);
        if(isa<AllocaInst>(I)) V = dyn_cast<Value>(&I);
        errs() << VNF->getVarName(V);

        if(omittableInstructions.find(&I) != omittableInstructions.end())
          errs() << " | OMITTED";

        errs() << "\n";
      }}
    }

    // Remove omittable instructions from profiling
    Instruction* DP_Instrumentation;
    for(Instruction* I : omittableInstructions){
      if(isa<AllocaInst>(I)) DP_Instrumentation = I->getNextNode()->getNextNode();
      else DP_Instrumentation = I->getPrevNode();

      if(!DP_Instrumentation) continue;
      if(CallInst* call_inst = dyn_cast<CallInst>(DP_Instrumentation)){
        if(Function* Fun = call_inst->getCalledFunction()){
          string fn = Fun->getName();
          if(fn == "__dp_write" || fn == "__dp_read" || fn == "__dp_alloca"){
            DP_Instrumentation->eraseFromParent();
            ++removedInstrumentations;
          }
        }
      }
    }
    if(DP_DEBUG) errs() << "Done with function " << F.getName() << ":\n";
  }

  // Find __dp_finalize call and add a call to __dp_add_bb_deps before it
  for(Function &F : M){
    if(!F.hasName() || F.getName() != "main") continue;
    for(BasicBlock &BB : F){
      for(Instruction &I: BB){
        if(CallInst* call_inst = dyn_cast<CallInst>(&I)){
          if(Function *Fun = call_inst->getCalledFunction()){
            if(Fun->getName() == "__dp_finalize"){
              IRBuilder<> builder(call_inst);
              Value *V = builder.CreateGlobalStringPtr(StringRef(bbDepString), ".dp_bb_deps");
              CallInst::Create(
                cast<Function>(F.getParent()->getOrInsertFunction("__dp_add_bb_deps", Void, CharPtr)),
                V, "", call_inst
              );
            }
          }
        }
      }
    }
  }
  return true;
}

bool DPInstrumentationOmission::doInitialization(Module &M){
  Void = const_cast<Type *>(Type::getVoidTy(M.getContext()));
  Int32 = const_cast<IntegerType *>(IntegerType::getInt32Ty(M.getContext()));
  CharPtr = const_cast<PointerType *>(Type::getInt8PtrTy(M.getContext()));
  ReportBB = cast<Function>(M.getOrInsertFunction(
      "__dp_report_bb",
      Void,
      Int32
    )
  );
  ReportBBPair = cast<Function>(
    M.getOrInsertFunction(
      "__dp_report_bb_pair",
      Void,
      Int32,
      Int32
    )
  );
  VNF = new dputil::VariableNameFinder(M);
}

char DPInstrumentationOmission::ID = 0;

static RegisterPass<DPInstrumentationOmission> X("dp-instrumentation-omission", "Run the discopop instrumentation omission analysis. Removes omittable store/load instrumentation calls", false, false);
