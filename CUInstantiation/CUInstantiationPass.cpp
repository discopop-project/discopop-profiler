/*
 * This file is part of the DiscoPoP software (http://www.discopop.tu-darmstadt.de)
 *
 * Copyright (c) 2019, Technische Universitaet Darmstadt, Germany
 *
 * This software may be modified and distributed under the terms of
 * a BSD-style license.  See the LICENSE file in the package base
 * directory for details.
 *
 */

#include "llvm/Transforms/Instrumentation.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/RegionIterator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/PassAnalysisSupport.h"
#include "llvm/PassSupport.h"
#include "llvm-c/Core.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/PassRegistry.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/LegacyPassManager.h"

#include "DPUtils.h"

#include <map>
#include <set>
#include <utility>
#include <iomanip>
#include <algorithm>
#include <string.h>

using namespace llvm;
using namespace std;
using namespace dputil;
// using LID = int32_t;

namespace {

    static cl::opt<std::string> InputFilename("input", cl::desc("<input file>"), cl::Optional);
    static cl::opt<std::string> FileMapping("filemapping", cl::desc("<filemapping file>"), cl::Optional);

    struct compare {
    bool operator() (const pair<string,int>& lhs, const pair<string,int>& rhs) const{
        return ((lhs.first<rhs.first) || (lhs.second < rhs.second));
    }
    };

    // Data structures for reading input
    static map<LID, vector<pair<string,int>>> callLineToFNameMap; // location of function calls to a (functio name,PIDIndex)
    static map<LID, set<pair<string,int>,compare>> readLineToVarNameMap; // location of loads to instrument with variable names in the input
    static map<LID, set<pair<string,int>,compare>> writeLineToVarNameMap; // location of stores to instrument with variable names in the input

    int32_t fileID;
    static int PIDIndex;

    struct CUInstantiation : public ModulePass {
        static char ID;

        LLVMContext* ThisModuleContext;

        //structures to get list of global variables
        Module *ThisModule;

        // used to get variable names. Originally appeared in DiscoPoP code!
        //map<string, int> signature;

         // Callbacks to run-time library
        Function *CUInstFinalize, *CUInstInitialize;
        Function *CUInstRead, *CUInstWrite;
        Function *CUInstCallBefore;
        Function *CUInstCallAfter;

        // Basic types
        Type *Void;
        IntegerType *Int32, *Int64;
        PointerType *CharPtr;

        //DiscoPoP Data structures
        map<string, MDNode*> Structs;
        map<string, Value*> VarNames;

        //DiscoPoP Functions
        string determineVariableName(Instruction* I);
        Type* pointsToStruct(PointerType* PTy);
        string getOrInsertVarName(string varName, IRBuilder<>& builder);
        string findStructMemberName(MDNode* structNode, unsigned idx, IRBuilder<>& builder);
        bool isaCallOrInvoke(Instruction* BI);
        void setupDataTypes();
        void setupCallbacks();


        string getFunctionName(Instruction *instruction);
        void readLineNumberPairs(const char* fileName);
        void insertInitializeInst(Function &F);
        void insertFinalizeInst(Instruction *inst);
        void instrumentLoadInst(Instruction *inst, int pidIndex);
        void instrumentStoreInst(Instruction *inst, int pidIndex);
        void instrumentCallInst(Instruction *inst, int index, int lastCall);


        //Output function
        //string xmlEscape(string data);
        //void secureStream();
        //string getLineNumbersString(set<int> LineNumbers);
        //void closeOutputFiles();
        vector<string> split(const string &s, const char delim);
        LID encodeLID(const string s);
        int isLastCall(LID line, string fName, int index);

        bool doInitialization(Module &M);
        virtual bool runOnModule(Module &M);
        StringRef getPassName() const;
        void getAnalysisUsage(AnalysisUsage &Info) const;

        CUInstantiation() : ModulePass(ID) {}

    }; // end of struct CUInstantiation
}  // end of anonymous namespace

/*****************************   DiscoPoP Functions  ***********************************/

string CUInstantiation::determineVariableName(Instruction* I) {

    assert(I && "Instruction cannot be NULL \n");
    int index = isa<StoreInst>(I) ? 1 : 0;
    Value* operand = I->getOperand(index);

    IRBuilder<> builder(I);

    if (operand == NULL) {
        return getOrInsertVarName("", builder);
    }

    if (operand->hasName()) {
        //// we've found a global variable
        if (isa<GlobalVariable>(*operand)) {
            //MOHAMMAD ADDED THIS FOR CHECKING
            return string(operand->getName());
        }
        if (isa<GetElementPtrInst>(*operand)) {
            GetElementPtrInst* gep = cast<GetElementPtrInst>(operand);
            Value* ptrOperand = gep->getPointerOperand();
            PointerType *PTy = cast<PointerType>(ptrOperand->getType());

            // we've found a struct/class
            Type* structType = pointsToStruct(PTy);
            if (structType && gep->getNumOperands() > 2) {
                Value* constValue = gep->getOperand(2);
                if (constValue && isa<ConstantInt>(*constValue)) {
                    ConstantInt* idxPtr = cast<ConstantInt>(gep->getOperand(2));
                    uint64_t memberIdx = *(idxPtr->getValue().getRawData());

                    string strName(structType->getStructName().data());
                    map<string, MDNode*>::iterator it = Structs.find(strName);
                    if (it != Structs.end()) {
                        std::string ret = findStructMemberName(it->second, memberIdx, builder);
                        if (ret.size() > 0)
                            return ret;
                        else
                            return getOrInsertVarName("", builder);
                        //return ret;

                    }
                }
            }

            // we've found an array
            if (PTy->getElementType()->getTypeID() == Type::ArrayTyID && isa<GetElementPtrInst>(*ptrOperand)) {
                return determineVariableName((Instruction*)ptrOperand);
            }
            return determineVariableName((Instruction*)gep);
        }
        return string(operand->getName().data());
        //return getOrInsertVarName(string(operand->getName().data()), builder);
    }

    if (isa<LoadInst>(*operand) || isa<StoreInst>(*operand)) {
        return determineVariableName((Instruction*)(operand));
    }
    // if we cannot determine the name, then return *
    return "";//getOrInsertVarName("*", builder);
}

Type* CUInstantiation::pointsToStruct(PointerType* PTy) {
    assert(PTy);
    Type* structType = PTy;
    if (PTy->getTypeID() == Type::PointerTyID) {
        while (structType->getTypeID() == Type::PointerTyID) {
            structType = cast<PointerType>(structType)->getElementType();
        }
    }
    return structType->getTypeID() == Type::StructTyID ? structType : NULL;
}

string CUInstantiation::getOrInsertVarName(string varName, IRBuilder<>& builder) {
    Value* valName = NULL;
    std::string vName = varName;
    map<string, Value*>::iterator pair = VarNames.find(varName);
    if (pair == VarNames.end()) {
        valName = builder.CreateGlobalStringPtr(StringRef(varName.c_str()), ".str");

        VarNames[varName] = valName;
    }
    else {
        vName = pair->first;
    }

    return vName;
}

string CUInstantiation::findStructMemberName(MDNode* structNode, unsigned idx, IRBuilder<>& builder) {
    assert(structNode);
    assert(structNode->getOperand(10));
    MDNode* memberListNodes = cast<MDNode>(structNode->getOperand(10));
    if (idx < memberListNodes->getNumOperands()) {
        assert(memberListNodes->getOperand(idx));
        MDNode* member = cast<MDNode>(memberListNodes->getOperand(idx));
        if (member->getOperand(3)) {
            //getOrInsertVarName(string(member->getOperand(3)->getName().data()), builder);
            //return string(member->getOperand(3)->getName().data());
            getOrInsertVarName(dyn_cast<MDString>(member->getOperand(3))->getString(), builder);
            return dyn_cast<MDString>(member->getOperand(3))->getString();
        }
    }
    return NULL;
}

bool CUInstantiation::isaCallOrInvoke(Instruction* BI) {
    return (BI != NULL) && ((isa<CallInst>(BI) && (!isa<DbgDeclareInst>(BI))) || isa<InvokeInst>(BI));
}

string CUInstantiation::getFunctionName(Instruction *instruction){

    string name;

    Function* f;
    Value* v;

    if(isa<CallInst>(instruction)){
        f = (cast<CallInst>(instruction))->getCalledFunction();
        v = (cast<CallInst>(instruction))->getCalledValue();
    }
    else {
        f = (cast<InvokeInst>(instruction))->getCalledFunction();
        v = (cast<InvokeInst>(instruction))->getCalledValue();
    }

    // For ordinary function calls, F has a name.
    // However, sometimes the function being called
    // in IR is encapsulated by "bitcast()" due to
    // the way of compiling and linking. In this way,
    // getCalledFunction() method returns NULL.
    // Also, getName() returns NULL if this is an indirect function call.
    if(f){
        name = f->getName();
    }
    else{ // get name of the indirect function which is called
        Value* sv = v->stripPointerCasts();
        StringRef  fname = sv->getName();
        name = fname;
    }
    return name;
}

void CUInstantiation::setupDataTypes() {
    Void = const_cast<Type*>(Type::getVoidTy(*ThisModuleContext));
    Int32 = const_cast<IntegerType*>(IntegerType::getInt32Ty(*ThisModuleContext));
    Int64 = const_cast<IntegerType*>(IntegerType::getInt64Ty(*ThisModuleContext));
    CharPtr = const_cast<PointerType*>(Type::getInt8PtrTy(*ThisModuleContext));
}

void CUInstantiation::setupCallbacks() {
    /* function name
     * return value type
     * arg types
     * NULL
     */

    CUInstInitialize = cast<Function>(ThisModule->getOrInsertFunction("__CUInstantiationInitialize",
        Void,
        CharPtr));

    CUInstFinalize = cast<Function>(ThisModule->getOrInsertFunction("__CUInstantiationFinalize",
        Void));

    CUInstRead = cast<Function>(ThisModule->getOrInsertFunction("__CUInstantiationRead",
            Void,
            Int32,
            Int32,
            Int64,
            CharPtr,
            CharPtr));

    CUInstWrite = cast<Function>(ThisModule->getOrInsertFunction("__CUInstantiationWrite",
            Void,
            Int32,
            Int32,
            Int64,
            CharPtr,
            CharPtr));

    CUInstCallBefore = cast<Function>(ThisModule->getOrInsertFunction("__CUInstantiationCallBefore",
            Void,
            Int32));

    CUInstCallAfter = cast<Function>(ThisModule->getOrInsertFunction("__CUInstantiationCallAfter",
            Void,
            Int32,
            Int32));
}

bool CUInstantiation::doInitialization(Module &M) {

    // Export M to the outside
    ThisModule = &M;
    ThisModuleContext = &(M.getContext());
    PIDIndex = 0;
    setupDataTypes();
    setupCallbacks();

    return true;
}


/*********************************** End of DiscoPoP Functions ***********************************/

// string split
vector<string> CUInstantiation::split(const string &s, const char delim) {
    stringstream ss(s);
    string tmp;
    vector<string> out;
    while (getline(ss, tmp, delim)) {
        out.emplace_back(tmp);
    }
    return out;
}


/* *********************** Helper functions ************************ */
void CUInstantiation::readLineNumberPairs(const char* fileName)
{
    ifstream inputFileStream;
    inputFileStream.open(fileName);
    if(!inputFileStream.is_open()){
        errs() << "Unable to open the input file\n";
        exit(0);
    }
    string line;

    while (std::getline(inputFileStream, line))
    {
        istringstream iss(line);
        string FName;
        string callLines;
        string deps;

        iss >> FName;
        iss >> callLines;
        iss >> deps;

        for(auto i:split(callLines, ',')){
            callLineToFNameMap[encodeLID(i)].push_back(pair<string,int>(FName,PIDIndex));
        }

        for(auto dep:split(deps, ',')){

            vector<string> i = split(dep, '|');

                pair<string,int> p(i.back(),PIDIndex);
                if(i[1] == "RAW"){
                    readLineToVarNameMap[encodeLID(i.front())].insert(p); // i.front is the read location and i.back variable name
                    writeLineToVarNameMap[encodeLID(i[2])].insert(p); // i[2] is the write location and i.back variable name
                } else if(i[1] == "WAR"){
                    writeLineToVarNameMap[encodeLID(i.front())].insert(p); // i.front is the write location and i.back variable name
                    readLineToVarNameMap[encodeLID(i[2])].insert(p); // i[2] is the read location and i.back variable name
                } else if(i[1] == "WAW"){
                    writeLineToVarNameMap[encodeLID(i.front())].insert(p); // i.front is the write location and i.back variable name
                    writeLineToVarNameMap[encodeLID(i[2])].insert(p); // i[2] is the write location and i.back variable name
                }

        }
        PIDIndex++;

    }
}


// encode string to LID
LID CUInstantiation::encodeLID(const string s) {
    vector<string> tmp = split(s, ':');
    return (static_cast<LID>(stoi(tmp[0])) << LIDSIZE) + static_cast<LID>(stoi(tmp[1]));
}


/* *********************** End of helper functions ************************ */
void CUInstantiation::insertInitializeInst(Function &F){

    string allFunctionIndices = "";
    for(auto i:callLineToFNameMap){
        for(auto j:i.second){
            allFunctionIndices += to_string(j.second) + " ";
        }
    }
    BasicBlock &entryBB = F.getEntryBlock();
    int32_t lid = 0;
    vector<Value*> args;

    IRBuilder<> builder(&*entryBB.begin());
    Value *vName = builder.CreateGlobalStringPtr(StringRef(allFunctionIndices.c_str()), ".str");
    args.push_back(vName);
    // We always want to insert __CUInstInitialize at the beginning
    // of the main function to initialize our data structures in the instrumentation library, but we need the first valid LID to
    // get the entry line of the function.
    for (BasicBlock::iterator BI = entryBB.begin(), EI = entryBB.end(); BI != EI; ++BI) {
        lid = getLID(&*BI, fileID);
        if (lid > 0 && !isa<PHINode>(&*BI)) {
            //IRBuilder<> IRB(entryBB.begin());
            //CallInst::Create(CUInstInitialize, args, "")->insertAfter(BI);
            CallInst::Create(CUInstInitialize, args, "", &*BI);

            break;
        }
    }
    assert((lid > 0) && "Function entry is not instrumented because LID are all invalid for the entry block.");
}

void CUInstantiation::insertFinalizeInst(Instruction *before){
    CallInst::Create(CUInstFinalize, "", before);
}

void CUInstantiation::instrumentLoadInst(Instruction *toInstrument, int pidIndex){
    vector<Value*> args;


    int32_t lid = getLID(toInstrument, fileID);
    if (lid == 0) return;

    args.push_back(ConstantInt::get(Int32, lid));
    args.push_back(ConstantInt::get(Int32, pidIndex));

    Value* memAddr = PtrToIntInst::CreatePointerCast(cast<LoadInst>(toInstrument)->getPointerOperand(),
            Int64, "", toInstrument);
    args.push_back(memAddr);

    IRBuilder<> builder(toInstrument);
    Value *fName;

    if(toInstrument->getParent()->getParent()->hasName()){
        fName = builder.CreateGlobalStringPtr(string(toInstrument->getParent()->getParent()->getName().data()).c_str(), ".str");
        args.push_back(fName);
    } else {
        fName = builder.CreateGlobalStringPtr(string("NULL").c_str(), ".str");
        args.push_back(fName);
    }

    string varName = determineVariableName(toInstrument);
    //if (varName.find(".addr") != varName.npos)
    //  varName.erase(varName.find(".addr"), 5);
    Value *vName = builder.CreateGlobalStringPtr(varName.c_str(), ".str");
    args.push_back(vName);

    CallInst::Create(CUInstRead, args, "", toInstrument);
}


void CUInstantiation::instrumentStoreInst(Instruction *toInstrument, int pidIndex){
    vector<Value*> args;

    int32_t lid = getLID(toInstrument, fileID);
    if (lid == 0) return;

    args.push_back(ConstantInt::get(Int32, lid));

    args.push_back(ConstantInt::get(Int32, pidIndex));

    Value* memAddr = PtrToIntInst::CreatePointerCast(cast<StoreInst>(toInstrument)->getPointerOperand(),
            Int64, "", toInstrument);
    args.push_back(memAddr);

    IRBuilder<> builder(toInstrument);
    Value *fName;

    if(toInstrument->getParent()->getParent()->hasName()){
        fName = builder.CreateGlobalStringPtr(string(toInstrument->getParent()->getParent()->getName().data()).c_str(), ".str");
        args.push_back(fName);
    } else {
        fName = builder.CreateGlobalStringPtr(string("NULL").c_str(), ".str");
        args.push_back(fName);
    }

    string varName = determineVariableName(toInstrument);
    //if (varName.find(".addr") != varName.npos)
    //  varName.erase(varName.find(".addr"), 5);
    Value *vName = builder.CreateGlobalStringPtr(varName.c_str(), ".str");
    args.push_back(vName);

    CallInst::Create(CUInstWrite, args, "", toInstrument);
}

void CUInstantiation::instrumentCallInst(Instruction *toInstrument, int index, int lastCall){
    vector<Value*> args;
    args.push_back(ConstantInt::get(Int32, index));

    //IRBuilder<> builder(toInstrument);
    //Value *vName = builder.CreateGlobalStringPtr(StringRef(fName.c_str()), ".str");
    //args.push_back(vName);

    //it creates a call "CUInstCallBefore" and inserts it before the toInstrument instruction
    CallInst::Create(CUInstCallBefore, args, "", toInstrument);

    if(lastCall == 1)
        args.push_back(ConstantInt::get(Int32, 1));
    else
        args.push_back(ConstantInt::get(Int32, 0));

    //it first creates a call "CUInstCallAfter" and then inserts it after the toInstrument instruction
    CallInst::Create(CUInstCallAfter, args, "")->insertAfter(toInstrument);

}

void CUInstantiation::getAnalysisUsage(AnalysisUsage &Info) const {
    Info.addRequired<LoopInfoWrapperPass>();
}

int CUInstantiation::isLastCall(LID line, string fName, int index){
    for(auto j:callLineToFNameMap)
        for(auto i:j.second){
            if(i.first == fName && i.second == index){
                return 0;
            }
        }
    return 1;
}

bool CUInstantiation::runOnModule(Module &M){

    StringRef inputFileName = InputFilename;
    if (inputFileName.empty()) //input is not given
    {
        errs() << "Input file name empty\n";
        exit(0);
    }
    const char* fileName = inputFileName.data();
    readLineNumberPairs(fileName);

    for (Module::iterator func = ThisModule->begin(), E = ThisModule->end(); func != E; ++func)
    {
        determineFileID(*func, fileID);

        if (func->hasName() && func->getName().equals("main")){
            insertInitializeInst(*func);
        }
        for (inst_iterator i = inst_begin(*func); i!=inst_end(*func); ++i)
        {
            Instruction *inst = &*i;
            LID line = getLID(inst, fileID);
            if(isa<LoadInst>(inst)){
                if(readLineToVarNameMap.find(line) != readLineToVarNameMap.end()){
                    string varName = determineVariableName(inst);
                    for(auto i:readLineToVarNameMap[line]){
                        if(i.first == varName){
                            instrumentLoadInst(inst, i.second);
                        }
                    }
                }
            } else if(isa<StoreInst>(inst)){
                if(writeLineToVarNameMap.find(line) != writeLineToVarNameMap.end()){
                    string varName = determineVariableName(inst);
                    for(auto i:writeLineToVarNameMap[line]){
                        if(i.first == varName){
                            instrumentStoreInst(inst, i.second);
                        }
                    }
                }
            } else if(isaCallOrInvoke(inst)){
                if(callLineToFNameMap.find(line) != callLineToFNameMap.end()){
                    string fName = getFunctionName(inst);
                    int ind = 0;
                    for(auto i:callLineToFNameMap[line]){
                        if(i.first == fName){
                            int temp = i.second;
                            callLineToFNameMap[line].erase(callLineToFNameMap[line].begin() + ind);
                            int isLast = isLastCall(line, fName, temp);
                            instrumentCallInst(inst, temp, isLast);
                            break;
                        }
                        ind++;
                    }
                }
            }
            else if (isa<ReturnInst>(inst)) {
                if (func->getName().equals("main")) {   // returning from main
                insertFinalizeInst(inst);
                }
            }

        }
    }
    return false;
}

char CUInstantiation::ID = 0;


static RegisterPass<CUInstantiation> X("CUInstantiation", "CUInstantiation: determine computation units instances that can run in parallel.", false, false);

static void loadPass(const PassManagerBuilder &Builder, legacy::PassManagerBase &PM)
{
    PM.add(new LoopInfoWrapperPass());
    PM.add(new CUInstantiation());
}

static RegisterStandardPasses CUGenerationLoader_Ox(PassManagerBuilder::EP_OptimizerLast, loadPass);
static RegisterStandardPasses CUGenerationLoader_O0(PassManagerBuilder::EP_EnabledOnOptLevel0, loadPass);


ModulePass *createCUInstantiationPass()
{

    initializeLoopInfoWrapperPassPass(*PassRegistry::getPassRegistry());
    initializeRegionInfoPassPass(*PassRegistry::getPassRegistry());
    return new CUInstantiation();
}

StringRef CUInstantiation::getPassName() const
{
    return "CUInstantiation";
}
