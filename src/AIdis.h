#ifndef _AIDIS_H
#define _AIDIS_H

#include <queue>
#include <vector>

#include "llvm/Support/CFG.h"

#include "AIpass.h"

using namespace llvm;

/// Pass that computes disjunctive invariants
class AIdis : public ModulePass, public AIPass {

	private:
		/// paths - remembers all the paths that have already been
		/// visited
		std::map<BasicBlock*,PathTree*> pathtree;

		std::set<Node*> A_prime;

		void computeNewPaths(
			Node * n
			);

		int sigma(std::list<BasicBlock*> path, int start);
	public:
		static char ID;	

	public:

		AIdis(char &_ID) : ModulePass(_ID) {init();}
		AIdis() : ModulePass(ID) {init();}
		void init()
			{
				aman = new AbstractManDisj();
				passID = LW_WITH_PF_DISJ;
				Passes[LW_WITH_PF_DISJ] = passID;	
			}

		~AIdis () {
			for (std::map<BasicBlock*,PathTree*>::iterator 
				it = pathtree.begin(),
				et = pathtree.end(); 
				it != et; 
				it++) {
				if ((*it).second != NULL)
					delete (*it).second;
				}
			}

		const char *getPassName() const;

		void getAnalysisUsage(AnalysisUsage &AU) const;

		bool runOnModule(Module &M);

		void computeFunction(Function * F);

		std::set<BasicBlock*> getPredecessors(BasicBlock * b) const;

		/// computeNode - compute and update the Abstract value of the Node n
		void computeNode(Node * n);
		
		/// narrowNode - apply narrowing at node n
		void narrowNode(Node * n);

		void loopiter(
			Node * n, 
			int index,
			Abstract * &Xtemp,
			std::list<BasicBlock*> * path,
			bool &only_join,
			PathTree * const U);
};

#endif