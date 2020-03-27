#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/Debug.h"
#include <string>
#include "Graph.hpp"

#include "DPUtils.h"

#define ENTRY 1000000
#define EXIT 2000000

class InstructionCFG : public Graph<Instruction*>
{
	
private:
	Node<Instruction*> *entry;
	Node<Instruction*> *exit;
	dputil::VariableNameFinder *VNF;
	set<Instruction*> highlightedNodes;
	void findAndAddFirstRelevantInstructionInSuccessorBlocks(BasicBlock *BB, Instruction* previousInstruction);
    
public:
	InstructionCFG(dputil::VariableNameFinder *_VNF, Function &F);
	
	set<Instruction*> findBoundaryInstructions(uint startLine, uint endLine);
	
	Node<Instruction*> *getEntry() { return entry; }
	Node<Instruction*> *getExit() { return exit; }

	bool isEntryOrExit(Instruction * I) { return Graph::getNode(I) == entry || Graph::getNode(I) == exit;}

	void highlightNode(Instruction *instr);

	void dumpToDot(const string targetPath);

};