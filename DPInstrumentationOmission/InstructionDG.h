#include <string>
#include "InstructionCFG.h"
#include "DPUtils.h"

class InstructionDG : public Graph<Instruction*>
{
	
private:
	dputil::VariableNameFinder *VNF;
	InstructionCFG *CFG;
	set<Instruction*> highlightedNodes;
	int32_t fid;

	void recursiveDepChecker(set<Instruction*>* checkedInstructions, Instruction* I, Instruction* C);
	void recursiveDepFinder(set<Instruction*>* checkedInstructions, Instruction* I);

public:
	InstructionDG(dputil::VariableNameFinder *_VNF, InstructionCFG *_CFG, int32_t _fid);

	string edgeToDPDep(Edge<Instruction*> *e);

	void highlightNode(Instruction *instr);
	void dumpToDot(const string targetPath);

};