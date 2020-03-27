#include "llvm/Support/CommandLine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"

#include "llvm/PassAnalysisSupport.h"
#include "llvm/PassSupport.h"
#include "llvm/IR/DerivedTypes.h"

#include "DPUtils.h"

using namespace llvm;

namespace {
struct Hello : public FunctionPass {
  dputil::VariableNameFinder *VNF;
  static char ID;
  Hello() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    errs() << "TestPass on " << F.getName() << "\n";
    for(BasicBlock& BB: F){
        for(Instruction& I: BB){
            if(isa<LoadInst>(I) || isa<StoreInst>(I)){
                string n1 = VNF->getVarName(&I);
                string n2 = determineVarName(&I);
                std::size_t found = n2.find(".addr");
                if(found != string::npos){
                    n2 = n2.erase(found);
                }
                string type = (isa<LoadInst>(I) ? "Read" : "Write");
                string pos;
                if(DebugLoc dl = I.getDebugLoc()){
                  pos = to_string(dl->getLine());
                  pos += "|" + to_string(dl->getColumn());
                }else{
                  pos = "INIT";
                }
                if(n1 != n2)
                  errs() << type << ";" << pos << ";" << n1 << ";" << n2 << "\n";
            }
        }
    }
  }

  string determineVarName(Instruction* const I) {
    assert(I && "Instruction cannot be NULL \n");
    int index = isa<StoreInst>(I) ? 1 : 0;
    Value* operand = I->getOperand(index);

    IRBuilder<> builder(I);

    if (operand == NULL) {
        return "*";
    }

    if (operand->hasName()) {
        // we've found a global variable
        if (isa<GlobalVariable>(*operand)) {
            DIGlobalVariable* gv = findDbgGlobalDeclare(cast<GlobalVariable>(operand));
            if (gv != NULL) {
                return string(gv->getDisplayName().data());
            }
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
                        return findStructMemberName(it->second, memberIdx);
                    }
                }
            }

            // we've found an array
            if (PTy->getElementType()->getTypeID() == Type::ArrayTyID && isa<GetElementPtrInst>(*ptrOperand)) {
                return determineVarName((Instruction*)ptrOperand);
            }
            return determineVarName((Instruction*)gep);
        }
        return string(operand->getName().data());
    }

    if (isa<LoadInst>(*operand) || isa<StoreInst>(*operand)) {
        return determineVarName((Instruction*)(operand));
    }
    // if we cannot determine the name, then return *
    return "*";
}

DIGlobalVariable* findDbgGlobalDeclare(GlobalVariable *v) {
    assert(v && "Global variable cannot be null");
    for (set<DIGlobalVariable *>::iterator it = GlobalVars.begin(); it != GlobalVars.end(); ++it)
    {
        if ((*it)->getDisplayName() == v->getName())
            return *it;
    }
    return NULL;
}

Type* pointsToStruct(PointerType* PTy) {
    assert(PTy && "PointerType* PTy is null!\n");
    Type* structType = PTy;
    if (PTy->getTypeID() == Type::PointerTyID) {
        while(structType->getTypeID() == Type::PointerTyID) {
            structType = cast<PointerType>(structType)->getElementType();
        }
    }
    return structType->getTypeID() == Type::StructTyID ? structType : NULL;
}

string findStructMemberName(MDNode* structNode, unsigned idx) {
    MDNode *memberListNodes = cast<MDNode>(structNode->getOperand(10));
    if (idx < memberListNodes->getNumOperands())
    {
        assert(memberListNodes->getOperand(idx));
        MDNode *member = cast<MDNode>(memberListNodes->getOperand(idx));
        if (member->getOperand(3))
            return dyn_cast<MDString>(member->getOperand(3))->getString();
    }
    return "*";
}

void collectDebugInfo(Module &M)
{
    if (NamedMDNode *CU_Nodes = M.getNamedMetadata("llvm.dbg.cu"))
    {
        for (unsigned i = 0, e = CU_Nodes->getNumOperands(); i != e; ++i)
        {
            DICompileUnit *CU = cast<DICompileUnit>(CU_Nodes->getOperand(i));
            // DICompileUnit CU(CU_Nodes->getOperand(i));
            auto GVs = CU->getGlobalVariables();
            for (unsigned i = 0, e = GVs.size(); i < e; ++i)
            {
                DIGlobalVariable *DIG = GVs[i]->getVariable();
                if (DIG)
                {
                    GlobalVars.insert(DIG);
                }
            }
        }
    }
}


void processStructTypes(string const &fullStructName, MDNode *structNode)
{
    assert(structNode && "structNode cannot be NULL");
    DIType *strDes = cast<DIType>(structNode);
    // DIType strDes(structNode);
    assert(strDes->getTag() == dwarf::DW_TAG_structure_type);
    // sometimes it's impossible to get the list of struct members (e.g badref)
    if (structNode->getNumOperands() <= 10 || structNode->getOperand(10) == NULL)
    {
        errs() << "cannot process member list of this struct: \n";
        structNode->dump();
        return;
    }
    Structs[fullStructName] = structNode;

    MDNode *memberListNodes = cast<MDNode>(structNode->getOperand(10));
    for (unsigned i = 0; i < memberListNodes->getNumOperands(); ++i)
    {
        assert(memberListNodes->getOperand(i));
        MDNode *member = cast<MDNode>(memberListNodes->getOperand(i));
        DINode *memberDes = cast<DINode>(member);
        // DIDescriptor memberDes(member);
        if (memberDes->getTag() == dwarf::DW_TAG_member)
        {
            assert(member->getOperand(9));
            MDNode *memberType = cast<MDNode>(member->getOperand(9));
            DIType *memberTypeDes = cast<DIType>(memberType);
            // DIType memberTypeDes(memberType);
            if (memberTypeDes->getTag() == dwarf::DW_TAG_structure_type)
            {
                string fullName = "";
                // try to get namespace
                if (memberType->getNumOperands() > 2 && structNode->getOperand(2) != NULL)
                {
                    MDNode *namespaceNode = cast<MDNode>(structNode->getOperand(2));
                    DINamespace *dins = cast<DINamespace>(namespaceNode);
                    // DINameSpace dins(namespaceNode);
                    fullName = "struct." + string(dins->getName().data()) + "::";
                }
                //fullName += string(memberType->getOperand(3)->getName().data());
                fullName += (dyn_cast<MDString>(memberType->getOperand(3)))->getString();

                if (Structs.find(fullName) == Structs.end())
                    processStructTypes(fullName, memberType);
            }
        }
    }
}
  map<string, Value*> VarNames;
		set<DIGlobalVariable*> GlobalVars;
		map<string, MDNode*> Structs;
  bool doInitialization(Module &M){
      VNF = new dputil::VariableNameFinder(M);
      GlobalVars.clear();
      Structs.clear();
      collectDebugInfo(M);
  }
}; // end of struct Hello
}  // end of anonymous namespace

char Hello::ID = 0;
static RegisterPass<Hello> X("dp-test", "DP Testing Pass",
                             false /* Only looks at CFG */,
                             false /* Analysis Pass */);