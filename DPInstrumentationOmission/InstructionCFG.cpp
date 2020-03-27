#include "InstructionCFG.h"

InstructionCFG::InstructionCFG(dputil::VariableNameFinder *_VNF, Function &F): VNF(_VNF){
	entry = Graph::addNode((Instruction*)ENTRY);
	exit = Graph::addNode((Instruction*)EXIT);
	Instruction *previousInstruction;
	for (BasicBlock &BB : F){
		// Add current block's store/load/call-instructions and declarations to graph
		previousInstruction = nullptr;
		for (Instruction &I : BB){
			if(isa<StoreInst>(I) || isa<LoadInst>(I) || isa<AllocaInst>(&I)){
				if(previousInstruction != nullptr){
					Graph::addEdge(previousInstruction, &I);
				}
				previousInstruction = &I;
			}
		}
		// Add edges from last instruction in current block to first instruction all the successor blocks
		if(previousInstruction != nullptr) findAndAddFirstRelevantInstructionInSuccessorBlocks(&BB, previousInstruction);
	}
	
	// Conect entry/exit nodes
	for(auto node : Graph::getNodes()){
		if(node != entry && node != exit){
			if(Graph::getInEdges(node).empty()){
				Graph::addEdge(entry, node);
			}else if(Graph::getOutEdges(node).empty()){
				Graph::addEdge(node, exit);
			}
		}
	}
}


void InstructionCFG::findAndAddFirstRelevantInstructionInSuccessorBlocks(BasicBlock *BB, Instruction* previousInstruction) {
	bool hasSuccessors = false;
	for (BasicBlock *S : successors(BB)) {
		hasSuccessors = true;
		for (Instruction &I : *S){
			if(isa<StoreInst>(I) || isa<LoadInst>(I)){
				Graph::addEdge(previousInstruction, &I);
				goto next;
			}else if(isa<AllocaInst>(&I)){
				Graph::addEdge(previousInstruction, &I);
				goto next;
			}else if(isa<ReturnInst>(&I)){
				Graph::addEdge(Graph::getNode(previousInstruction), exit);
			}
		}
		if(S != BB) findAndAddFirstRelevantInstructionInSuccessorBlocks(S, previousInstruction);
		next:;
	}
}

set<Instruction*> InstructionCFG::findBoundaryInstructions(uint startLine, uint endLine){
	
}

void InstructionCFG::highlightNode(Instruction *instr){
	highlightedNodes.insert(instr);
}

void InstructionCFG::dumpToDot(const std::string targetPath)
{
	// Write the graph to a DOT file
	ofstream dotStream;
	dotStream.open(targetPath);
	dotStream << "digraph{";
	// Create all nodes in DOT format
	for (auto node : getNodes())
	{
		string label;
		DebugLoc dl;
		Instruction* instr;
		if(node == entry){
			label = "label=ENTRY"; goto printNode;
		}
		if(node == exit){
			label = "label=EXIT"; goto printNode;
		}
		label = "label=\"" + to_string(Graph::getNodeIndex(node)) + "\\n";
		instr = node->getItem();
		dl = instr->getDebugLoc();
		if(isa<StoreInst>(instr) || isa<LoadInst>(instr)){
			if(isa<StoreInst>(instr)){
				if(DebugLoc dl = instr->getDebugLoc())
					label += "write(";
				else
					label += "init(";
			}
			else label += "read(";
			label += VNF->getVarName(instr);
			label += ") ";

			if(dl){
				label += to_string(dl.getLine());
				label += ", ";
				label += to_string(dl.getCol());
			}else{
				label += to_string(instr->getFunction()->getSubprogram()->getLine());
			}
			label += "\"";
			if(highlightedNodes.find(instr) != highlightedNodes.end()){
				label += "\",fillcolor=cyan,style=filled";
			}
		} else if(isa<AllocaInst>(instr)){
			label += "alloca(" + VNF->getVarName(instr) + ")";
			label += "\",shape=rectangle,fillcolor=wheat,style=filled";
		} else label += "?\"";

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