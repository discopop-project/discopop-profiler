#include "InstructionDG.h"

#include "DPUtils.h"

InstructionDG::InstructionDG(dputil::VariableNameFinder *_VNF, InstructionCFG *_CFG, int32_t _fid): VNF(_VNF), CFG(_CFG), fid(_fid){
	for(auto edge: CFG->getInEdges(CFG->getExit()))
		recursiveDepFinder(new set<Instruction*>(), edge->getSrc()->getItem());
}

void InstructionDG::recursiveDepChecker(set<Instruction*>* checkedInstructions, Instruction* I, Instruction* C){
	if(CFG->isEntryOrExit(C)) return;
	checkedInstructions->insert(C);
	Value *V, *W;
	if(isa<AllocaInst>(I)) V = dyn_cast<Value>(I);
	else V = I->getOperand(isa<StoreInst>(I) ? 1 : 0);
	
	if(isa<AllocaInst>(C)) W = dyn_cast<Value>(C);
	else W = C->getOperand(isa<StoreInst>(C) ? 1 : 0);


	if(V == W && (isa<StoreInst>(I) || isa<StoreInst>(C))){
		Graph::addEdge(I, C);
		return;
	}
	for(auto edge: CFG->getInEdges(C))
		if(checkedInstructions->find(edge->getSrc()->getItem()) == checkedInstructions->end())
			recursiveDepChecker(checkedInstructions, I, edge->getSrc()->getItem());
}

void InstructionDG::recursiveDepFinder(set<Instruction*>* checkedInstructions, Instruction* I){
	if(CFG->isEntryOrExit(I) || checkedInstructions->find(I) != checkedInstructions->end()) return;
	checkedInstructions->insert(I);
	if(isa<StoreInst>(I) || isa<LoadInst>(I)) Graph::addNode(I);
	for(auto edge: CFG->getInEdges(I)){
		if(isa<StoreInst>(I) || isa<LoadInst>(I)){
			recursiveDepChecker(new set<Instruction*>(), I, edge->getSrc()->getItem());
		}
		recursiveDepFinder(checkedInstructions, edge->getSrc()->getItem());
	}
}

void InstructionDG::highlightNode(Instruction *instr){
	highlightedNodes.insert(instr);
}

string getInstructionLine(Instruction* I){
	if(DebugLoc dl = I->getDebugLoc()){
		return to_string(dl->getLine());
	}else{
		return to_string(I->getFunction()->getSubprogram()->getLine());
	}
}

string InstructionDG::edgeToDPDep(Edge<Instruction*> *e){
    Instruction *I = e->getSrc()->getItem();
    Instruction *J = e->getDst()->getItem();
    string depType;
	if(isa<AllocaInst>(J)){
		depType = "INIT";
		return to_string(fid) + ":"
			+ getInstructionLine(I) + " "
			+ depType + " *|"
			+ VNF->getVarName(I)
		;
	}else if(DebugLoc dl = J->getDebugLoc()){
		depType = (isa<LoadInst>(I) ? string("R") : string("W")) + "A" + (isa<LoadInst>(J) ? string("R") : string("W"));
		return to_string(fid) + ":"
			+ getInstructionLine(I) + " "
			+ depType + " "
			+ to_string(fid) + ":"
			+ getInstructionLine(J) + "|"
			+ VNF->getVarName(I)
		;
	}else{
		depType = (isa<LoadInst>(I) ? string("R") : string("W")) + "A" + (isa<LoadInst>(J) ? string("R") : string("W"));
		return to_string(fid) + ":"
			+ getInstructionLine(I) + " "
			+ depType + " "
			+ to_string(fid) + ":"
			+ getInstructionLine(J) + "|"
			+ VNF->getVarName(I)
		;
	}
}

void InstructionDG::dumpToDot(const string targetPath)
{
	// Write the graph to a DOT file
	ofstream dotStream;
	dotStream.open(targetPath);
	
	dotStream << "digraph g {\n";
	// Create all nodes in DOT format
	for (auto node : getNodes())
	{
		string label = "label=\"" + to_string(Graph::getNodeIndex(node)) + "\\n";
		Instruction* instr = node->getItem();
		if(isa<StoreInst>(instr)){
			if(DebugLoc dl = instr->getDebugLoc())
				label += "write(";
			else
				label += "init(";
		}
		else if(isa<LoadInst>(instr)) label += "read(";
		else label += "alloca(";
		label += VNF->getVarName(instr);
		label += ") ";

		if(DebugLoc dl = instr->getDebugLoc()){
			label += to_string(dl.getLine());
			label += ", ";
			label += to_string(dl.getCol());
		}else{
			to_string(instr->getFunction()->getSubprogram()->getLine());
		}
		label += "\"";
		if(highlightedNodes.find(instr) != highlightedNodes.end()){
			label += ",fillcolor=cyan,style=filled";
		}

		printNode:
		dotStream << "\t\"" << getNodeIndex(node) 
			<< "\" [" << label << "];\n"
		;

	}
	dotStream << "\n\n";
	
	// Now print all outgoing edges and their labels
	for (auto e : getEdges())
	{	
		dotStream << "\t\"" << getNodeIndex(e->getSrc())
			<< "\" -> \"" << getNodeIndex(e->getDst()) 
			<< "\";\n"
		;
	}
	dotStream << "}";
	dotStream.close();
}