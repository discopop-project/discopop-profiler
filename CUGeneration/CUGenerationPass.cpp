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
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/DebugInfoMetadata.h"
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
#include "llvm/Analysis/CallGraph.h"

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

namespace
{

    static unsigned int CUIDCounter;
    static bool defaultIsGlobalVariableValue;
    int32_t fileID;

    typedef struct Variable_struct
    {

        string name;
        string type;

        Variable_struct(const Variable_struct &other) : name(other.name), type(other.type)
        {
        }

        Variable_struct(string n, string t) : name(n), type(t)
        {
        }

        // We have a set of this struct. The set doesn't know how to order the elements.
        inline bool operator<(const Variable_struct &rhs) const
        {
            return name < rhs.name;
        }

        inline bool operator>(const Variable_struct &rhs) const
        {
            return name > rhs.name;
        }

    } Variable;

    enum nodeTypes {cu, func, loop, dummy};

    typedef struct Node_struct
    {
        string ID;
        nodeTypes type;
        int startLine;
        int endLine;

        //Only for func type
        string name;
        vector<Variable> argumentsList;

        vector<Node_struct *> childrenNodes;
        Node_struct *parentNode;

        //isRecursive function (Mo 5.11.2019)
        string recursiveFunctionCall = "";

        Node_struct()
        {
            ID = to_string(fileID) + ":" + to_string(CUIDCounter++);
            parentNode = NULL;
        }
    } Node;

    typedef struct CU_struct: Node_struct
    {

        string BBID; //BasicBlock Id where the CU appears in

        unsigned readDataSize; // number of bytes read from memory by the cu
        unsigned writeDataSize; // number of bytes written into memory during the cu
        unsigned instructionsCount;

        //basic block id & successor basic blocks for control dependence
        vector<string> successorCUs;// keeps IDs of control dependent CUs
        string basicBlockName;

        set<int> instructionsLineNumbers;
        set<int> readPhaseLineNumbers;
        set<int> writePhaseLineNumbers;

        set<Variable> localVariableNames;
        set<Variable> globalVariableNames;

        //Map to record function call line numbers
        map<int, vector<Node *>> callLineTofunctionMap;

        CU_struct()
        {
            type = nodeTypes::cu;
            readDataSize = 0;
            writeDataSize = 0;
            instructionsCount = 0;
        }

        void removeCU()
        {
            CUIDCounter--;//if a CU does not contain any instruction, e.g. entry basic blocks, then remove it.
        }


    } CU;

    struct CUGeneration : public FunctionPass
    {

        static char ID;
        Function *function;
        LLVMContext *ctx;
        ofstream *outCUs;
        ofstream *outOriginalVariables;
        ofstream *outCUIDCounter;

        //structures to get list of global variables
        Module *ThisModule;
        set<string> programGlobalVariablesSet;
        set<string> originalVariablesSet;

        // used to get variable names. Originally appeared in DiscoPoP code!
        map<string, MDNode *> Structs;
        map<string, Value *> VarNames;

        RegionInfoPass *RIpass;
        RegionInfo     *RI;

        //DiscoPoP Fields
        //set<DIGlobalVariable*> programGlobalVariables;

        //DiscoPoP Functions
        string determineVariableType(Instruction *I);
        string determineVariableName(Instruction *I, bool &isGlobalVariable = defaultIsGlobalVariableValue);
        Type *pointsToStruct(PointerType *PTy);
        string getOrInsertVarName(string varName, IRBuilder<> &builder);
        string findStructMemberName(MDNode *structNode, unsigned idx, IRBuilder<> &builder);
        //DIGlobalVariable* findDbgGlobalDeclare(GlobalVariable *v);

        //functions to get list of global variables
        bool doInitialization(Module &ThisModule);
        bool doFinalization(Module &M);
        void collectGlobalVariables();

        //functions to get recursive functions (Mo 5.11.2019)
        bool isRecursive(Function &F, CallGraph &CG);

        //Populate variable sets global to BB
        void populateGlobalVariablesSet(Region *TopRegion, set<string> &globalVariablesSet);
        void createCUs(Region *TopRegion, set<string> &globalVariablesSet, vector<CU *> &CUVector, map<string, vector<CU *>> &BBIDToCUIDsMap, Node *root, LoopInfo &LI);
        string refineVarName(string varName);
        void fillCUVariables(Region *TopRegion, set<string> &globalVariablesSet, vector<CU *> &CUVector, map<string, vector<CU *>> &BBIDToCUIDsMap);
        void fillStartEndLineNumbers(Node *root);
        void findStartEndLineNumbers(Node *root, int &start, int &end);

        //Output function
        void initializeCUIDCounter();
        string xmlEscape(string data);
        void secureStream();
        string getLineNumbersString(set<int> LineNumbers);
        string getChildrenNodesString(Node *node);
        void printOriginalVariables(set<string> &originalVariablesSet);
        void printData(Node *node);
        void printTree(Node *node, bool isRoot);
        void printNode(Node *node, bool isRoot);
        void closeOutputFiles();


        virtual bool runOnFunction(Function &F);
        //changed const char * to Stringref
        StringRef getPassName() const;
        void getAnalysisUsage(AnalysisUsage &Info) const;

        CUGeneration() : FunctionPass(ID) {}

    }; // end of struct CUGeneration
}  // end of anonymous namespace


/*****************************   DiscoPoP Functions  ***********************************/
string CUGeneration::determineVariableType(Instruction *I)
{
    string s = "";
    string type_str;
    int index = isa<StoreInst>(I) ? 1 : 0;
    raw_string_ostream rso(type_str);
    (*((I->getOperand(index))->getType())).print(rso);

    Value *operand = I->getOperand(index);

    if (operand->hasName())
    {
        if (isa<GetElementPtrInst>(*operand))
        {
            GetElementPtrInst *gep = cast<GetElementPtrInst>(operand);
            Value *ptrOperand = gep->getPointerOperand();
            PointerType *PTy = cast<PointerType>(ptrOperand->getType());
            // we've found a struct/class
            Type *structType = pointsToStruct(PTy);
            if (structType && gep->getNumOperands() > 2)
            {
                //errs() << "STRUCT DETECTED!\n";
                s = "STRUCT,";
            }
            // we've found an array
            if (PTy->getElementType()->getTypeID() == Type::ArrayTyID )
            {
                //errs() << "ARRAY DETECTED!\n";
                s = "ARRAY,";
            }
        }
    }

    s = s + rso.str();
    return s;
}

string CUGeneration::determineVariableName(Instruction *I, bool &isGlobalVariable/*=defaultIsGlobalVariableValue*/)
{

    assert(I && "Instruction cannot be NULL \n");
    int index = isa<StoreInst>(I) ? 1 : 0;
    Value *operand = I->getOperand(index);

    IRBuilder<> builder(I);

    if (operand == NULL)
    {
        return getOrInsertVarName("", builder);
    }

    if (operand->hasName())
    {
        //// we've found a global variable
        if (isa<GlobalVariable>(*operand))
        {
            //MOHAMMAD ADDED THIS FOR CHECKING
            isGlobalVariable = true;
            return string(operand->getName());
        }
        if (isa<GetElementPtrInst>(*operand))
        {
            GetElementPtrInst *gep = cast<GetElementPtrInst>(operand);
            Value *ptrOperand = gep->getPointerOperand();
            PointerType *PTy = cast<PointerType>(ptrOperand->getType());

            // we've found a struct/class
            Type *structType = pointsToStruct(PTy);
            if (structType && gep->getNumOperands() > 2)
            {
                Value *constValue = gep->getOperand(2);
                if (constValue && isa<ConstantInt>(*constValue))
                {
                    ConstantInt *idxPtr = cast<ConstantInt>(gep->getOperand(2));
                    uint64_t memberIdx = *(idxPtr->getValue().getRawData());

                    string strName(structType->getStructName().data());
                    map<string, MDNode *>::iterator it = Structs.find(strName);
                    if (it != Structs.end())
                    {
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
            if (PTy->getElementType()->getTypeID() == Type::ArrayTyID && isa<GetElementPtrInst>(*ptrOperand))
            {
                return determineVariableName((Instruction *)ptrOperand, isGlobalVariable);
            }
            return determineVariableName((Instruction *)gep, isGlobalVariable);
        }
        return string(operand->getName().data());
        //return getOrInsertVarName(string(operand->getName().data()), builder);
    }

    if (isa<LoadInst>(*operand) || isa<StoreInst>(*operand))
    {
        return determineVariableName((Instruction *)(operand), isGlobalVariable);
    }
    // if we cannot determine the name, then return *
    return "";//getOrInsertVarName("*", builder);
}


Type *CUGeneration::pointsToStruct(PointerType *PTy)
{
    assert(PTy);
    Type *structType = PTy;
    if (PTy->getTypeID() == Type::PointerTyID)
    {
        while (structType->getTypeID() == Type::PointerTyID)
        {
            structType = cast<PointerType>(structType)->getElementType();
        }
    }
    return structType->getTypeID() == Type::StructTyID ? structType : NULL;
}

string CUGeneration::getOrInsertVarName(string varName, IRBuilder<> &builder)
{
    Value *valName = NULL;
    std::string vName = varName;
    map<string, Value *>::iterator pair = VarNames.find(varName);
    if (pair == VarNames.end())
    {
        valName = builder.CreateGlobalStringPtr(StringRef(varName.c_str()), ".str");

        VarNames[varName] = valName;
    }
    else
    {
        vName = pair->first;
    }

    return vName;
}


string CUGeneration::findStructMemberName(MDNode *structNode, unsigned idx, IRBuilder<> &builder)
{
    assert(structNode);
    assert(structNode->getOperand(10));
    MDNode *memberListNodes = cast<MDNode>(structNode->getOperand(10));
    if (idx < memberListNodes->getNumOperands())
    {
        assert(memberListNodes->getOperand(idx));
        MDNode *member = cast<MDNode>(memberListNodes->getOperand(idx));
        if (member->getOperand(3))
        {
            //getOrInsertVarName(string(member->getOperand(3)->getName().data()), builder);
            //return string(member->getOperand(3)->getName().data());
            getOrInsertVarName(dyn_cast<MDString>(member->getOperand(3))->getString(), builder);
            return dyn_cast<MDString>(member->getOperand(3))->getString();
        }
    }
    return NULL;
}
/*********************************** End of DiscoPoP Functions ***********************************/

/*********************************** Output functions ********************************************/
string CUGeneration::xmlEscape(string data)
{
    string::size_type pos = 0;
    for (;;)
    {
        pos = data.find_first_of("\"&<>", pos);
        if (pos == string::npos) break;
        string replacement;
        switch (data[pos])
        {
        case '\"':
            replacement = "&quot;";
            break;
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        default:
            ;
        }
        data.replace(pos, 1, replacement);
        pos += replacement.size();
    };
    return data;
}

void CUGeneration::secureStream()
{
    outOriginalVariables = new std::ofstream();
    outOriginalVariables->open("OriginalVariables.txt", std::ios_base::app);

    outCUs = new std::ofstream();
    outCUs->open("Data.xml", std::ios_base::app);

    outCUIDCounter = new std::ofstream();
    outCUIDCounter->open("DP_CUIDCounter.txt", std::ios_base::out);

}


string CUGeneration::getLineNumbersString(set<int> LineNumbers)
{
    string line = "";
    for (auto li : LineNumbers)
    {
        std::string temp = ',' + dputil::decodeLID(li);
        if (temp != ",*")
        {
            if (line == "")
            {
                line = dputil::decodeLID(li);
            }
            else
            {
                line = line + temp;
            }
        }
    }
    return line;
}


string CUGeneration::getChildrenNodesString(Node *root)
{
    string childrenIDs = "";
    Node *tmp = root;
    int i = 0;

    for (auto node : tmp->childrenNodes)
    {
        if (i == 0)
        {
            childrenIDs = node->ID;
            i++;
        }
        else
        {
            childrenIDs = childrenIDs + "," + node->ID;
        }
    }
    return childrenIDs;
}

void CUGeneration::printOriginalVariables(set<string> &originalVariablesSet)
{

    for(auto i : originalVariablesSet)
    {
        *outOriginalVariables << i << endl;
    }
}

void CUGeneration::printData(Node *root)
{
    *outCUs << "<Nodes>" << endl << endl;

    printTree(root, true);

    *outCUs << "</Nodes>" << endl << endl << endl;

    closeOutputFiles();

}


void CUGeneration::printTree(Node *root, bool isRoot)
{
    Node *tmp = root;
    printNode(tmp, isRoot);


    for (auto node : tmp->childrenNodes)
    {
        if (root->type == nodeTypes::func)
        {
            isRoot = false;
        }
        //CU* cu = dynamic_cast<CU*>(node);
        printTree(node, isRoot);
    }
}

void CUGeneration::printNode(Node *root, bool isRoot)
{
    if(root->name.find("llvm"))
    {
        *outCUs << "\t<Node"
                << " id=\"" << xmlEscape(root->ID) << "\""
                << " type=\"" << root->type << "\""
                << " name=\"" << xmlEscape(root->name) << "\""
                << " startsAtLine = \"" << dputil::decodeLID(root->startLine) << "\""
                << " endsAtLine = \"" << dputil::decodeLID(root->endLine) << "\""
                << ">" << endl;
        *outCUs << "\t\t<childrenNodes>" << getChildrenNodesString(root) << "</childrenNodes>" << endl;
        if (root->type == nodeTypes::func || root->type == nodeTypes::dummy)
        {

            *outCUs << "\t\t<funcArguments>" << endl;
            for (auto ai : root->argumentsList)
            {
                *outCUs << "\t\t\t<arg type=\"" << xmlEscape(ai.type) << "\">" << xmlEscape(ai.name) << "</arg>" << endl;
            }
            *outCUs << "\t\t</funcArguments>" << endl;
        }

        if (root->type == nodeTypes::cu)
        {
            CU *cu = static_cast<CU *>(root);
            *outCUs << "\t\t<BasicBlockID>" << cu->BBID << "</BasicBlockID>" << endl;
            *outCUs << "\t\t<readDataSize>" << cu->readDataSize << "</readDataSize>" << endl;
            *outCUs << "\t\t<writeDataSize>" << cu->writeDataSize << "</writeDataSize>" << endl;

            *outCUs << "\t\t<instructionsCount>" << cu->instructionsCount << "</instructionsCount>" << endl;
            *outCUs << "\t\t<instructionLines count=\"" << (cu->instructionsLineNumbers).size() << "\">" << getLineNumbersString(cu->instructionsLineNumbers) << "</instructionLines>" << endl;
            *outCUs << "\t\t<readPhaseLines count=\"" << (cu->readPhaseLineNumbers).size() << "\">" << getLineNumbersString(cu->readPhaseLineNumbers) << "</readPhaseLines>" << endl;
            *outCUs << "\t\t<writePhaseLines count=\"" << (cu->writePhaseLineNumbers).size() << "\">" << getLineNumbersString(cu->writePhaseLineNumbers) << "</writePhaseLines>" << endl;

            *outCUs << "\t\t<successors>" << endl;
            for (auto sucCUi : cu->successorCUs)
            {
                *outCUs << "\t\t\t<CU>" << sucCUi  << "</CU>" << endl;
            }
            *outCUs << "\t\t</successors>" << endl;

            *outCUs << "\t\t<localVariables>" << endl;
            for (auto lvi : cu->localVariableNames)
            {
                *outCUs << "\t\t\t<local type=\"" << xmlEscape(lvi.type) << "\">" << xmlEscape(lvi.name) << "</local>" << endl;
            }
            *outCUs << "\t\t</localVariables>" << endl;

            *outCUs << "\t\t<globalVariables>" << endl;
            for (auto gvi : cu->globalVariableNames)
            {
                *outCUs << "\t\t\t<global type=\"" << xmlEscape(gvi.type) << "\">" << xmlEscape(gvi.name) << "</global>" << endl;
            }
            *outCUs << "\t\t</globalVariables>" << endl;

            *outCUs << "\t\t<callsNode>" << endl;
            for (auto i : (cu->callLineTofunctionMap))
            {
                for (auto ii : i.second){
                    *outCUs << "\t\t\t<nodeCalled atLine=\"" << dputil::decodeLID(i.first) << "\">" << ii->ID << "</nodeCalled>" << endl;
                    // specifica for recursive fucntions inside loops. (Mo 5.11.2019)
                    *outCUs << "\t\t\t\t<recursiveFunctionCall>" << ii->recursiveFunctionCall << "</recursiveFunctionCall>" << endl;
                }
            }
            *outCUs << "\t\t</callsNode>" << endl;
        }

        *outCUs << "\t</Node>" << endl << endl;
    }
}

void CUGeneration::closeOutputFiles()
{

    if (outCUs != NULL && outCUs->is_open())
    {
        outCUs->flush();
        outCUs->close();
    }

    if (outOriginalVariables != NULL && outOriginalVariables->is_open())
    {
        outOriginalVariables->flush();
        outOriginalVariables->close();
    }
    //delete outCUs;
}
/*********************************** End of output functions **************************************/


string CUGeneration::refineVarName(string varName)
{

    //refine variable name
    size_t pos = varName.find(".addr");
    if (pos != varName.npos)
        varName.erase(varName.find(".addr"), 5);

    return varName;

}

//recieves the region and outputs all variables and variables crossing basic block boundaries in the region.
void CUGeneration::populateGlobalVariablesSet(Region *TopRegion, set<string> &globalVariablesSet)
{

    map<string, BasicBlock *> variableToBBMap;
    bool isGlobalVariable;
    for (Region::block_iterator bb = TopRegion->block_begin(); bb != TopRegion->block_end(); ++bb)
    {
        for (BasicBlock::iterator instruction = (*bb)->begin(); instruction != (*bb)->end(); ++instruction)
        {
            if(isa<LoadInst>(instruction) || isa<StoreInst>(instruction) || isa<CallInst>(instruction))
            {

                //string varName = refineVarName(determineVariableName(instruction, isGlobalVariable));
                // NOTE: changed 'instruction' to '&*instruction'
                string varName = determineVariableName(&*instruction, isGlobalVariable);

                if(isGlobalVariable)  // add it if it is a global variable in the program
                {
                    programGlobalVariablesSet.insert(varName);
                }

                if(variableToBBMap.find(varName) != variableToBBMap.end())
                {
                    //this var has already once recordded. check for bb id
                    if(variableToBBMap[varName] != *bb)
                    {
                        //global variable found. Insert into the globalVariablesSet
                        globalVariablesSet.insert(varName);
                    }
                }
                else
                {
                    //record usage of the variable.
                    variableToBBMap.insert(pair<string, BasicBlock *>(varName, *bb));
                    //errs() << varName << "\n";
                }
            }
        }
    }
}


void CUGeneration::createCUs(Region *TopRegion, set<string> &globalVariablesSet, vector<CU *> &CUVector, map<string, vector<CU *>> &BBIDToCUIDsMap, Node *root, LoopInfo &LI)
{
    // NOTE: changed 'ThisModule->getDataLayout()' to '&ThisModule->getDataLayout()'
    const DataLayout *DL = &ThisModule->getDataLayout(); // used to get data size of variables, pointers, structs etc.
    Node *currentNode = root;
    CU *cu;
    int lid;
    string varName;
    string varType;
    set<string> suspiciousVariables;
    string basicBlockName;

    map<Loop *, Node *> loopToNodeMap;


    for (Region::block_iterator bb = TopRegion->block_begin(); bb != TopRegion->block_end(); ++bb)
    {

        // Get the closest loop where bb lives in.
        // (loop == NULL) if bb is not in any loop.
        Loop *loop = LI.getLoopFor(*bb);
        if (loop)
        {
            //if bb is in a loop and if we have already created a node for that loop, assign it to currentNode.
            if (loopToNodeMap.find(loop) != loopToNodeMap.end())
            {
                currentNode = loopToNodeMap[loop];
                //errs() << "bb->Name: " << bb->getName() << " , " << "node->ID: " << currentNode->ID << "\n";


            }
            //else, create a new Node for the loop, add it as children of currentNode and add it to the map.
            else
            {
                if(bb->getName().size() != 0)
                {
                    //errs() << "Name: " << bb->getName() << "\n";
                }

                Node *n = new Node;
                n->type = nodeTypes::loop;
                n->parentNode = currentNode;
                currentNode->childrenNodes.push_back(n);

                loopToNodeMap[loop] = n;
                currentNode = n;
                //errs() << "--bb->Name: " << bb->getName() << " , " << "node->ID: " << currentNode->ID << "\n";
            }
        }
        else
        {
            if(bb->getName().size() != 0)
            {
                //errs() << "bb Name: " << bb->getName() << "\n";
            }
            //end of loops. go to the parent of the loop. may have to jump several nodes in case of nested loops
            for(map<Loop *, Node *>::iterator it = loopToNodeMap.begin(); it != loopToNodeMap.end() ; it++ )
                if (it -> second == currentNode)   // current node found in loop map jump to its parent.
                {
                    currentNode = currentNode->parentNode;
                    it = loopToNodeMap.begin(); // search the whole map again for current node
                    if(it -> second == currentNode) // due to it++ we need to check first element of map ourself
                        currentNode = currentNode -> parentNode;
                }
        }

        cu = new CU;

        //errs() << "cu->ID: " << cu->ID << " , " << "node->ID: " << currentNode->ID << " , " << "tmpNode->ID: " << tmpNode->ID << " , " << "bb->Name: " << bb->getName() << "\n";

        if(bb->getName().size() == 0)
            bb->setName(cu->ID);

        cu->BBID = bb->getName();
        currentNode->childrenNodes.push_back(cu);
        vector<CU *> basicBlockCUVector;
        basicBlockCUVector.push_back(cu);
        BBIDToCUIDsMap.insert(pair<string, vector<CU *>>(bb->getName(), basicBlockCUVector));

        for (BasicBlock::iterator instruction = (*bb)->begin(); instruction != (*bb)->end(); ++instruction)
        {

            //NOTE: 'instruction' --> '&*instruction'
            lid = getLID(&*instruction, fileID);
            basicBlockName = bb->getName();
            if (lid > 0)
            {
                cu-> instructionsLineNumbers.insert(lid);
                cu-> instructionsCount++;
            }
            if(isa < StoreInst >(instruction))
            {

                // get size of data written into memory by this store instruction
                Value *operand = instruction->getOperand(1);
                Type *Ty = operand->getType();
                unsigned u = DL->getTypeSizeInBits(Ty);
                cu-> writeDataSize += u;
                //varName = refineVarName(determineVariableName(instruction));
                varName = determineVariableName(&*instruction);
                varType = determineVariableType(&*instruction);
                if(globalVariablesSet.count(varName) || programGlobalVariablesSet.count(varName))
                {
                    suspiciousVariables.insert(varName);
                    if (lid > 0)
                        cu->writePhaseLineNumbers.insert(lid);
                }
            }
            else if(isa < LoadInst >(instruction))
            {

                // get size of data read from memory by this load instruction
                Type *Ty = instruction->getType();
                unsigned u = DL->getTypeSizeInBits(Ty);
                cu-> readDataSize += u;
                //varName = refineVarName(determineVariableName(instruction));
                varName = determineVariableName(&*instruction);
                if(suspiciousVariables.count(varName))
                {
                    // VIOLATION OF CAUTIOUS PROPERTY
                    //it is a load instruction which read the value of a global variable.
                    // This global variable has already been stored previously.
                    // A new CU should be created here.
                    cu->readPhaseLineNumbers.erase(lid);
                    cu->writePhaseLineNumbers.erase(lid);
                    cu->instructionsLineNumbers.erase(lid);
                    cu-> instructionsCount--;
                    if (cu->instructionsLineNumbers.empty())
                    {
                        cu->removeCU();
                        cu->startLine = -1;
                        cu->endLine = -1;
                    }
                    else
                    {
                        cu->startLine = *(cu->instructionsLineNumbers.begin());
                        cu->endLine = *(cu->instructionsLineNumbers.rbegin());
                    }
                    cu->basicBlockName = basicBlockName;
                    CUVector.push_back(cu);
                    suspiciousVariables.clear();
                    CU *temp = cu;// keep current CU to make a reference to the successor CU
                    cu = new CU;

                    cu->BBID = bb->getName();
                    //errs() << "bb->Name: "  << bb->getName() << " , " << "cu->ID: " << cu->ID  << " , " << "node->ID: " << currentNode->ID << "\n";

                    currentNode->childrenNodes.push_back(cu);
                    temp->successorCUs.push_back(cu->ID);
                    BBIDToCUIDsMap[bb->getName()].push_back(cu);
                    if (lid > 0)
                    {
                        cu->readPhaseLineNumbers.insert(lid);
                        cu->instructionsLineNumbers.insert(lid);
                    }
                }
                else
                {
                    if(globalVariablesSet.count(varName) || programGlobalVariablesSet.count(varName))
                    {
                        if (lid > 0)
                            cu->readPhaseLineNumbers.insert(lid);
                    }
                }
            }
        }
        if (cu->instructionsLineNumbers.empty())
        {
            cu->removeCU();
            cu->startLine = -1;
            cu->endLine = -1;
        }
        else
        {
            cu->startLine = *(cu->instructionsLineNumbers.begin());
            cu->endLine = *(cu->instructionsLineNumbers.rbegin());
        }

        cu->basicBlockName = basicBlockName;
        CUVector.push_back(cu);
        suspiciousVariables.clear();

        //errs() << "cu->basicBlockName: "  << cu->basicBlockName << "\n";

        //check for call instructions in current basic block
        for (BasicBlock::iterator instruction = (*bb)->begin(); instruction != (*bb)->end(); ++instruction)
        {
            if(dyn_cast<DbgDeclareInst>(instruction) || dyn_cast<DbgValueInst>(instruction))
                continue;
            if (isa < CallInst >(instruction))
            {
                Node *n = new Node;
                n->type = nodeTypes::dummy;

                //TODO: DO the same for Invoke inst

                //get function name and parameters
                Function *f = (cast<CallInst>(instruction))->getCalledFunction();

                // For ordinary function calls, F has a name.
                // However, sometimes the function being called
                // in IR is encapsulated by "bitcast()" due to
                // the way of compiling and linking. In this way,
                // getCalledFunction() method returns NULL.
                // Also, getName() returns NULL if this is an indirect function call.
                if(f)
                {
                    n->name = f->getName();
                    // @Zia: This for loop appeared after the else part. For some function calls, the value of f is null.
                    // I guess that is why you have checked if f is not null here. Anyway, I (Mohammad) had to bring the
                    // for loop inside to avoid the segmentation fault. If you think it is not appropriate, find a solution for it.
                    // 14.2.2016
                    for (Function::arg_iterator it = f->arg_begin(); it != f->arg_end(); it++)
                    {
                        string type_str;
                        raw_string_ostream rso(type_str);
                        (it->getType())->print(rso);
                        Variable v(string(it->getName()), rso.str());

                        n->argumentsList.push_back(v);
                    }
                }
                else  // get name of the indirect function which is called
                {
                    Value *v = (cast<CallInst>(instruction))->getCalledValue();
                    Value *sv = v->stripPointerCasts();
                    StringRef  fname = sv->getName();
                    n->name = fname;
                }

                //Recursive functions (Mo 5.11.2019)
                CallGraphWrapperPass* CGWP = &(getAnalysis<CallGraphWrapperPass>());
                if(isRecursive(*f, CGWP->getCallGraph())){
                    int lid = getLID(&*instruction, fileID);
                    n->recursiveFunctionCall = n->name + " " + dputil::decodeLID(lid) + ",";
                }

                vector<CU *> BBCUsVector = BBIDToCUIDsMap[bb->getName()];
                //locate the CU where this function call belongs
                for (auto i : BBCUsVector)
                {
                    int lid = getLID(&*instruction, fileID);
                    if(lid >= i->startLine && lid <= i->endLine)
                    {
                        i->instructionsLineNumbers.insert(lid);
                        i->childrenNodes.push_back(n);

                        i->callLineTofunctionMap[lid].push_back(n);
                        break;
                    }
                }
            }
        }
    }
}

void CUGeneration::fillCUVariables(Region *TopRegion, set<string> &globalVariablesSet, vector<CU *> &CUVector, map<string, vector<CU *>> &BBIDToCUIDsMap)
{
    int lid;
    string varName, varType;
    // Changed TerminatorInst to Instuction
    const Instruction *TInst;
    string successorBB;

    for (Region::block_iterator bb = TopRegion->block_begin(); bb != TopRegion->block_end(); ++bb)
    {
        CU *lastCU = BBIDToCUIDsMap[bb->getName()].back();//get the last CU in the basic block
        //get all successor basic blocks for bb
        TInst = bb->getTerminator();
        for (unsigned i = 0, nSucc = TInst->getNumSuccessors(); i < nSucc; ++i)
        {
            //get the name of successor basicBlock
            successorBB = TInst->getSuccessor(i)->getName();
            //get the first CU of the successor basicBlock and record its ID in current CU's successorCUs
            lastCU->successorCUs.push_back(BBIDToCUIDsMap[successorBB].front()->ID);
        }

        auto bbCU = BBIDToCUIDsMap[bb->getName()].begin();
        for (BasicBlock::iterator instruction = (*bb)->begin(); instruction != (*bb)->end(); ++instruction)
        {

            if (isa<LoadInst>(instruction) || isa<StoreInst>(instruction))
            {
                // NOTE: changed 'instruction' to '&*instruction'
                lid = getLID(&*instruction, fileID);
                if(lid == 0)
                    continue;
                //varName = refineVarName(determineVariableName(instruction));
                // NOTE: changed 'instruction' to '&*instruction', next 2 lines
                varName = determineVariableName(&*instruction);
                varType = determineVariableType(&*instruction);
                Variable v(varName, varType);

                std::string prefix("ARRAY");
                if (!varType.compare(0, prefix.size(), prefix))
                {
                    varName = "ARRAY, " + varName;
                }

                //errs() << "Name: "  << varName << " " << "Type: " << varType << "\n";

                if(lid > (*bbCU)->endLine)
                {
                    bbCU = next(bbCU, 1);
                }
                if(globalVariablesSet.count(varName) || programGlobalVariablesSet.count(varName))
                {
                    (*bbCU)->globalVariableNames.insert(v);
                    originalVariablesSet.insert(varName);
                }
                else
                {
                    (*bbCU)->localVariableNames.insert(v);
                    originalVariablesSet.insert(varName);
                }
            }
        }
    }
}

void CUGeneration::findStartEndLineNumbers(Node *root, int &start, int &end)
{
    if (root->type == nodeTypes::cu)
    {
        if(start == -1 || start > root->startLine)
        {
            start = root->startLine;
        }

        if (end < root->endLine)
        {
            end = root->endLine;
        }
    }

    for (auto i : root->childrenNodes)
    {
        findStartEndLineNumbers(i, start, end);
    }

}



void CUGeneration::fillStartEndLineNumbers(Node *root)
{
    if (root->type != nodeTypes::cu)
    {
        int start = -1, end = -1;

        findStartEndLineNumbers(root, start, end);

        root->startLine = start;
        root->endLine = end;
    }

    for (auto i : root->childrenNodes)
    {
        fillStartEndLineNumbers(i);
    }

}

bool CUGeneration::doFinalization(Module &M)
{
    //write the current count of CUs to a file to avoid duplicate CUs.
    *outCUIDCounter << CUIDCounter;
    if (outCUIDCounter != NULL && outCUIDCounter->is_open())
    {
        outCUIDCounter->flush();
        outCUIDCounter->close();
    }
    return true;
}

bool CUGeneration::doInitialization(Module &M)
{

    CUIDCounter = 0;
    defaultIsGlobalVariableValue = false;
    ThisModule = &M;

    initializeCUIDCounter();

    for(Module::global_iterator I = ThisModule->global_begin(); I != ThisModule->global_end(); I++)
    {
        Value *globalVariable = dyn_cast<Value>(I);
        string glo = string(globalVariable->getName());
        if(glo.find(".") == glo.npos)
        {
            programGlobalVariablesSet.insert(glo);
            originalVariablesSet.insert(glo);
        }

    }

    return true;
}

void CUGeneration::initializeCUIDCounter()
{
    std::string CUCounterFile = "DP_CUIDCounter.txt";

    if (dputil::fexists(CUCounterFile))
    {
        std::fstream inCUIDCounter(CUCounterFile, std::ios_base::in);;
        inCUIDCounter >> CUIDCounter;
        inCUIDCounter.close();
    }
}

bool CUGeneration::isRecursive(Function &F, CallGraph &CG)
{
    auto callNode = CG[&F];
    for (unsigned i = 0; i < callNode->size(); i++)
    {
        if ((*callNode)[i]->getFunction() == &F)
            return true;
    }

    return false;
}

void CUGeneration::getAnalysisUsage(AnalysisUsage &AU) const
{
    AU.addRequiredTransitive<RegionInfoPass>();
    //NOTE: changed 'LoopInfo' to 'LoopInfoWrapperPass'
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    //Get recursive functions called in loops. (Mo 5.11.2019)
    AU.addRequired<CallGraphWrapperPass>();
    AU.setPreservesAll();
}

bool CUGeneration::runOnFunction(Function &F)
{
    StringRef funcName = F.getName();
    // Avoid functions we don't want to instrument
    if (funcName.find("llvm.dbg") != string::npos)    // llvm debug calls
    {
        return false;
    }
    if (funcName.find("__dp_") != string::npos)       // instrumentation calls
    {
        return false;
    }
    if (funcName.find("__cx") != string::npos)        // c++ init calls
    {
        return false;
    }
    if (funcName.find("__clang") != string::npos)     // clang helper calls
    {
        return false;
    }
    //    if (funcName.find("_GLOBAL_") != string::npos) {  // global init calls (c++)
    //        return false;
    //    }
    if (funcName.find("pthread_") != string::npos)
    {
        return false;
    }

    //initializeCUIDCounter();
    vector<CU *> CUVector;
    set<string> globalVariablesSet; // list of variables which appear in more than one basic block
    map<string, vector<CU *>> BBIDToCUIDsMap;

    determineFileID(F, fileID);
    /********************* Initialize root values ***************************/
    Node *root = new Node;
    root->name = F.getName();
    root->type = nodeTypes::func;

    //Get list of arguments for this function and store them in root.
    // NOTE: changed the way we get the arguments
    // for (Function::ArgumentListType::iterator it = F.getArgumentList().begin(); it != F.getArgumentList().end(); it++) {
    for ( Function::arg_iterator it = F.arg_begin(); it != F.arg_end(); it++)
    {

        string type_str;
        raw_string_ostream rso(type_str);
        (it->getType())->print(rso);
        Variable v(it->getName(), rso.str());

        root->argumentsList.push_back(v);
    }
    /********************* End of initialize root values ***************************/

    // NOTE: changed the pass name for loopinfo -- LoopInfo &LI = getAnalysis<LoopInfo>();
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

    //get the top level region
    RIpass = &getAnalysis<RegionInfoPass>();
    RI = &(RIpass->getRegionInfo());
    Region *TopRegion = RI->getTopLevelRegion();

    populateGlobalVariablesSet(TopRegion, globalVariablesSet);
    createCUs(TopRegion, globalVariablesSet, CUVector, BBIDToCUIDsMap, root, LI);

    fillCUVariables(TopRegion, globalVariablesSet, CUVector, BBIDToCUIDsMap);

    fillStartEndLineNumbers(root);

    secureStream();
    
    printOriginalVariables(originalVariablesSet);

    printData(root);

    for(auto i : CUVector)
    {
        delete(i);
    }

    return false;
}

char CUGeneration::ID = 0;

static RegisterPass<CUGeneration> X("CUGeneration", "CUGeneration: determine computation units.", false, false);

static void loadPass(const PassManagerBuilder &Builder, legacy::PassManagerBase &PM)
{
    PM.add(new LoopInfoWrapperPass());
    PM.add(new RegionInfoPass());
    PM.add(new CUGeneration());
}

static RegisterStandardPasses CUGenerationLoader_Ox(PassManagerBuilder::EP_OptimizerLast, loadPass);
static RegisterStandardPasses CUGenerationLoader_O0(PassManagerBuilder::EP_EnabledOnOptLevel0, loadPass);

FunctionPass *createCUGenerationPass()
{

    initializeLoopInfoWrapperPassPass(*PassRegistry::getPassRegistry());
    initializeRegionInfoPassPass(*PassRegistry::getPassRegistry());
    return new CUGeneration();
}

StringRef CUGeneration::getPassName() const
{
    return "CUGeneration";
}
