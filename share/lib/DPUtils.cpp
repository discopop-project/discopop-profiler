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

#include "DPUtils.h"

using namespace std;

cl::opt<string> FileMappingPath("fm-path", cl::init(""),
	cl::desc("Specify file mapping location"), cl::Hidden);

namespace dputil {

int32_t getFileID(string fileMapping, string fullPathName) {
	int32_t index = 0; // if the associated file id is not found, then we return 0
	string line;
	ifstream fileMap(fileMapping.c_str());

	if (fileMap.is_open()) {
		vector<string>* substrings = NULL;
		while (getline(fileMap, line)) {
			substrings = split(line, '\t');
			if (substrings->size() == 2) {
				string indexString = (*substrings)[0];
				string fileName = (*substrings)[1];
				if (fileName.compare(fullPathName) == 0) {
					index = (int32_t)atoi(indexString.c_str());
					break;	
				}
			}
			delete substrings;	
		}
		fileMap.close();
	}
	return index;
}

// Encode the fileID and line number of BI as LID.
// This is needed to support multiple files in a project.
int32_t getLID(Instruction* BI, int32_t& fileID)
{
    int32_t lid = 0;
    int32_t lno = 0;

    const DebugLoc &location = BI->getDebugLoc();
    if (location)
        lno = BI->getDebugLoc().getLine();
    else if(isa<AllocaInst>(BI) || isa<StoreInst>(BI))
        lno = BI->getFunction()->getSubprogram()->getLine();
    else
        return 0;

    if (fileID == 0)
    {
        // Get the new fileID.
        StringRef File = "", Dir = "";
        MDNode *N = BI->getMetadata("dbg");
        if (N == NULL)
        {
            // N == NULL means BI is only a helper instruction.
            // No metadata is attached to BI.
            return 0;
        }
        // NOTE: Replace  next 3 lines with next 3 lines
        // DILocation Loc(N);
        // File = Loc.getFilename();
        // Dir = Loc.getDirectory();
        const DebugLoc &location = BI->getDebugLoc();
        const DILocation *Loc = location.get();
        File = Loc->getFilename();
        Dir = Loc->getDirectory();

        if (File.str().substr(0, 2) == "./")
        {
            std::string sub = File.str().substr(0, 2);
            File = File.substr(2, File.size() - 1);
        }

        fileID = dputil::getFileID(FileMappingPath, Dir.str() + "/" + File.str());

        // file is not in FileMapping.txt
        if (fileID == 0)
            return -1;
    }
    lid = (fileID << LIDSIZE) + lno;
    //ofmap << lid << std::endl;
    return lid;
}

// determine the file index according to the given FileMapping
void determineFileID(Function &F, int32_t& fileID)
{
    fileID = 0;
    if (!dputil::fexists(FileMappingPath)){
        fileID = 1;
        return;
    }
    
    DIFile* DIF = F.getSubprogram()->getFile();
    
    StringRef File = "", Dir = "";
    File = DIF->getFilename();
    Dir = DIF->getDirectory();

    char *absolutePathFileName = realpath((Dir.str() + "/" + File.str()).c_str(), NULL);
    
    if (absolutePathFileName)
    {
        fileID = dputil::getFileID(FileMappingPath, string(absolutePathFileName));
        delete[] absolutePathFileName;
        return;
    }
}

string get_exe_dir() {
	char buff[1024];
	ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff)-1);
    if (len != -1) {
		buff[len] = '\0';
		string fullPath = std::string(buff);
		return fullPath.substr(0, fullPath.find_last_of('/'));
    } else {
		return "";     
    }
}

VariableNameFinder::VariableNameFinder(Module &M){
    DebugInfoFinder DIF;
    DIF.processModule(M);
    for (auto DI: DIF.types()) {
        if(auto CT = dyn_cast<DICompositeType>(DI)){
            vector<string> v;
            for(auto E: CT->getElements()){
                if(auto DT = dyn_cast<DIDerivedType>(E)){
                    v.push_back(DT->getName());
                }
            }
            StructMemberMap.insert({CT->getName(), v});
        }
    }
}

string VariableNameFinder::getVarName(Value const *V){
    if(const Instruction *I = dyn_cast<Instruction>(V)){
        if(auto GEPI = dyn_cast<GetElementPtrInst>(I)){
            Type *srcElemT = GEPI->getSourceElementType();
            string r = getVarName(GEPI->getOperand(0));

            if(GEPI->getNumOperands() == 2){
                return r + "[" + getVarName(GEPI->getOperand(1)) + "]";
            }

            if(dyn_cast<ConstantInt>(GEPI->getOperand(1))->getSExtValue() > 0){
                r += "[" + getVarName(GEPI->getOperand(1)) + "]";
            }

            if(isa<SequentialType>(srcElemT) || isa<IntegerType>(srcElemT)){
                return r + "[" + getVarName(GEPI->getOperand(2)) + "]";
            }else if(StructType *st = dyn_cast<StructType>(srcElemT)){
                int64_t offset = dyn_cast<ConstantInt>(GEPI->getOperand(2))->getSExtValue();
                if(st->hasName()){
                    string structTypeName = st->getName().str();
                    if(structTypeName.find("struct.") != string::npos){
                        structTypeName = structTypeName.erase(0,7);
                    }
                    if(structTypeName.find("class.") != string::npos){
                        structTypeName = structTypeName.erase(0,6);
                    }
                    if(StructMemberMap.count(structTypeName) > 0){
                        return r + "." + StructMemberMap[structTypeName][offset];
                    }
                }
            }
            return r;
        }

        if(isa<SExtInst>(I)){
            return getVarName(I->getOperand(0));
        }

        if(const BinaryOperator* binop = dyn_cast<BinaryOperator>(I)){
            string op;
            switch(binop->getOpcode()){
                case 12: op = "+"; break;
                case 14: op = "-"; break;
                case 16: op = "*"; break;
                case 19: op = "/"; break;
            }
            return getVarName(I->getOperand(0)) + op + getVarName(I->getOperand(1));
        }

        if(isa<StoreInst>(I) || isa<LoadInst>(I)){
            Value* v = I->getOperand((isa<StoreInst>(I) ? 1 : 0));
            if(isa<LoadInst>(v)) return "*" + getVarName(v);
            return getVarName(v);
        }
    }
    
    if (const GEPOperator* gepo = dyn_cast<GEPOperator>(V)){
        if (const GlobalVariable* gv = dyn_cast<GlobalVariable>(gepo->getPointerOperand())){
            string r = gv->getGlobalIdentifier();
            Type *st = gepo->getSourceElementType();
            if(StructType *ct = dyn_cast<StructType>(st)){
                string structTypeName = ct->getName().str().erase(0,7);
                int64_t offset = dyn_cast<ConstantInt>(gepo->getOperand(2))->getSExtValue();
                r += "." + StructMemberMap[structTypeName][offset];
                return r;
            }
        }
    }
    
    if(const ConstantInt* CI = dyn_cast<ConstantInt>(V)){
        return to_string(CI->getSExtValue());
    }
    
    if(V->hasName()){
        string r = V->getName().str();
        std::size_t found = r.find(".addr");
        if(found != string::npos){
            return r.erase(found);
        }
        return r;
    }
    return "!";
}

}//namespace
