#ifndef PR_H
#define PR_H

#include <map>
#include <set>

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFG.h"
#include "Node.h"

using namespace llvm;

/// @brief Pr computation pass
class Pr : public ModulePass {

	private:
		/// associate to each formula its set of Pr nodes
		static std::map<Function*,std::set<BasicBlock*>*> Pr_set;

		static std::map<Function*,std::set<BasicBlock*>*> Assert_set;

		std::map<Node*,int> index;
		std::map<Node*,int> lowlink;
		std::map<Node*,bool> isInStack;

		/// compute the set Pr for a function
		void computePr(Function &F);

		bool check_acyclic(Function &F, Node * n);
		bool check_acyclic_rec(
				Function &F,
				Node * n, 
				int & N,
				std::stack<Node*> * S);


	public:
		static char ID;	

		Pr();
		~Pr();

		const char *getPassName() const;

		void getAnalysisUsage(AnalysisUsage &AU) const;

		bool runOnModule(Module &M);

		/// associate to each basicBlock its successors in Pr
		/// WARNING : this set is filled by SMTpass
		static std::map<BasicBlock*,std::set<BasicBlock*> > Pr_succ;
		/// associate to each basicBlock its predecessors in Pr
		/// WARNING : this set is filled by SMTpass
		static std::map<BasicBlock*,std::set<BasicBlock*> > Pr_pred;

		/// getPr - get the set Pr. The set Pr is computed only once
		static std::set<BasicBlock*>* getPr(Function &F);

		static std::set<BasicBlock*>* getAssert(Function &F);

		/// getPrPredecessors - returns a set containing all the predecessors of
		/// b in Pr
		static std::set<BasicBlock*> getPrPredecessors(BasicBlock * b);

		/// getPrPredecessors - returns a set containing all the successors of
		/// b in Pr
		static std::set<BasicBlock*> getPrSuccessors(BasicBlock * b);
};
#endif
