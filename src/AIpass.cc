#include <vector>
#include <list>

#include "llvm/Module.h"
#include "llvm/Instructions.h"
#include "llvm/BasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/PassManager.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Analysis/Passes.h"

#include "ap_global1.h"

#include "AIpass.h"
#include "Expr.h"
#include "Node.h"
#include "apron.h"
#include "Live.h"
#include "SMT.h"
#include "Debug.h"
#include "Analyzer.h"
#include "PathTree.h"


using namespace llvm;

char AIPass::ID = 0;


//static RegisterPass<AIPass> X("AIPass", "Abstract Interpretation Pass", false, true);


void AIPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesAll();
	AU.addRequired<LoopInfo>();
	AU.addRequired<Live>();
	AU.addRequired<SMT>();
}


void AIPass::initFunction(Function * F) {
	Node * n;
	bool already_seen = false;

	// we create the Node objects associated to each basicblock
	for (Function::iterator i = F->begin(), e = F->end(); i != e; ++i) {
			if (Nodes.count(i) == 0) {
				n = new Node(man,i);
				Nodes[i] = n;
			} else {
				already_seen = true;
				n = Nodes[i];
				n->intVar.clear();
				n->realVar.clear();
				n->env = NULL;
			}
	}
	if (F->size() > 0 && !already_seen) {
		// we find the Strongly Connected Components
		Node * front = Nodes[&(F->front())];
		front->computeSCC();
	}
	//for (Function::iterator i = F->begin(), e = F->end(); i != e; ++i)
	//	printBasicBlock(i);
	*Out << *F;
}

void AIPass::printBasicBlock(BasicBlock* b) {
	Node * n = Nodes[b];
	LoopInfo &LI = getAnalysis<LoopInfo>(*(b->getParent()));
	if (LI.isLoopHeader(b)) {
		*Out << b << ": SCC=" << n->sccId << ": LOOP HEAD" << *b;
	} else {
		*Out << b << ": SCC=" << n->sccId << ":\n" << *b;
	}
	//for (BasicBlock::iterator i = b->begin(), e = b->end(); i != e; ++i) {
	//	Instruction * I = i;
	//	*Out << "\t\t" << I << "\t" << *I << "\n";
	//}

}

void AIPass::printPath(std::list<BasicBlock*> path) {
	std::list<BasicBlock*>::iterator i = path.begin(), e = path.end();
	Out->changeColor(raw_ostream::MAGENTA,true);
	*Out << "PATH: ";
	while (i != e) {
		*Out << *i;
		++i;
		if (i != e) *Out << " --> ";
	}
	*Out << "\n";
	Out->resetColor();
}

void AIPass::computeEnv(Node * n) {
	BasicBlock * b = n->bb;
	Node * pred = NULL;
	std::map<Value*,std::set<ap_var_t> >::iterator i, e;
	std::set<ap_var_t>::iterator it, et;

	std::map<Value*,std::set<ap_var_t> > intVars;
	std::map<Value*,std::set<ap_var_t> > realVars;


	std::set<BasicBlock*> preds = getPredecessors(b);
	std::set<BasicBlock*>::iterator p = preds.begin(), E = preds.end();

	if (p == E) {
		// we are in the first basicblock of the function
		Function * F = b->getParent();
		for (Function::arg_iterator a = F->arg_begin(), e = F->arg_end(); a != e; ++a) {
			Argument * arg = a;
			if (!(arg->use_empty()))
				n->add_var(arg);
		}
		return;
	}
	// for each predecessor, we iterate on their variables, and we insert
	// them if they are associated to a value which is still live in our Block
	// We do this for both int and real variables
	for (; p != E; ++p) {
		BasicBlock *pb = *p;
		pred = Nodes[pb];
		if (pred->X->main != NULL) {
			for (i = pred->intVar.begin(), e = pred->intVar.end(); i != e; ++i) {
				if (LV->isLiveThroughBlock((*i).first,b) || LV->isUsedInBlock((*i).first,b)) {
					intVars[(*i).first].insert((*i).second.begin(),(*i).second.end());
				}
			}

			for (i = pred->realVar.begin(), e = pred->realVar.end(); i != e; ++i) {
				if (LV->isLiveThroughBlock((*i).first,b) || LV->isUsedInBlock((*i).first,b)) {
					realVars[(*i).first].insert((*i).second.begin(),(*i).second.end());
				}
			}
		}
	}
	//n->intVar.clear();
	//n->realVar.clear();
	n->intVar.insert(intVars.begin(), intVars.end());
	n->realVar.insert(realVars.begin(), realVars.end());
}

void AIPass::computeTransform (AbstractMan * aman, Node * n, std::list<BasicBlock*> path, Abstract &Xtemp) {
	
	// setting the focus path, such that the instructions can be correctly
	// handled
	focuspath.clear();
	constraints.clear();
	PHIvars.name.clear();
	PHIvars.expr.clear();
	focuspath.assign(path.begin(), path.end());
	Node * succ = Nodes[focuspath.back()];
	focusblock = 0;
	
	computeEnv(succ);

	std::list<BasicBlock*>::iterator B = path.begin(), E = path.end();
	for (; B != E; ++B) {
		// visit instructions
		for (BasicBlock::iterator i = (*B)->begin(), e = (*B)->end();
				i != e; ++i) {
			if (focusblock == 0) {
				if (!isa<PHINode>(*i)) {
					visit(*i);
				}
			} else if (focusblock == focuspath.size()-1) {
				if (isa<PHINode>(*i)) {
					visit(*i);
				}
			} else {
				visit(*i);
			}
		}
		focusblock++;
	}
	ap_environment_t * env = NULL;
	succ->create_env(&env,LV);

	std::list<std::vector<ap_tcons1_array_t*>*>::iterator i, e;
	for (i = constraints.begin(), e = constraints.end(); i!=e; ++i) {
		if ((*i)->size() == 1) {
				DEBUG(
					tcons1_array_print((*i)->front());
				);
				Xtemp.meet_tcons_array((*i)->front());
		} else {
			std::vector<Abstract*> A;
			std::vector<ap_tcons1_array_t*>::iterator it, et;
			Abstract * X2;
			for (it = (*i)->begin(), et = (*i)->end(); it != et; ++it) {
				X2 = aman->NewAbstract(&Xtemp);
				X2->meet_tcons_array(*it);
				A.push_back(X2);
			}
			Xtemp.join_array(env,A);
		}
	}

	Xtemp.change_environment(env);
	
	Xtemp.assign_texpr_array(&PHIvars.name[0],&PHIvars.expr[0],PHIvars.name.size(),NULL);
	// the environment may have changed because of the constraints and the Phi
	// assignations
	//Xtemp.change_environment(env);
}


bool isequal(std::list<BasicBlock*> p, std::list<BasicBlock*> q) {

	if (p.size() != q.size()) return false;
	std::list<BasicBlock*> P,Q;
	P.assign(p.begin(),p.end());
	Q.assign(q.begin(),q.end());

	while (P.size() > 0) {
		if (P.front() != Q.front()) return false;
		P.pop_front();
		Q.pop_front();
	}
	return true;
}

/// insert_env_vars_into_node_vars - this function takes all apron variables of
/// an environment, and adds them into the Node's variables, with a Value V as
/// a use.
void AIPass::insert_env_vars_into_node_vars(ap_environment_t * env, Node * n, Value * V) {
	ap_var_t var;
	for (size_t i = 0; i < env->intdim; i++) {
		var = ap_environment_var_of_dim(env,i);
		n->intVar[V].insert(var);
	}
	for (size_t i = env->intdim; i < env->intdim + env->realdim; i++) {
		var = ap_environment_var_of_dim(env,i);
		n->realVar[V].insert(var);
	}
}

// create_constraints - this function is called by computeCondition
// it creates the constraint from its arguments and insert it into t_cons
void AIPass::create_constraints(
	ap_constyp_t constyp,
	ap_texpr1_t * expr,
	ap_texpr1_t * nexpr,
	std::vector<ap_tcons1_array_t*> * t_cons) {
	
	ap_tcons1_t cons;
	ap_tcons1_array_t * consarray;
	
	if (constyp == AP_CONS_DISEQ) {
		// we have a disequality constraint. We tranform it into 2 different
		// constraints: < and >, in order to create further 2 abstract domain
		// instead of one.
		consarray = new ap_tcons1_array_t();
		cons = ap_tcons1_make(
				AP_CONS_SUP,
				expr,
				ap_scalar_alloc_set_double(0));
		*consarray = ap_tcons1_array_make(cons.env,1);
		ap_tcons1_array_set(consarray,0,&cons);
		t_cons->push_back(consarray);

		consarray = new ap_tcons1_array_t();
		cons = ap_tcons1_make(
				AP_CONS_SUP,
				ap_texpr1_copy(nexpr),
				ap_scalar_alloc_set_double(0));
		*consarray = ap_tcons1_array_make(cons.env,1);
		ap_tcons1_array_set(consarray,0,&cons);
		t_cons->push_back(consarray);
	} else {
		consarray = new ap_tcons1_array_t();
		cons = ap_tcons1_make(
				constyp,
				expr,
				ap_scalar_alloc_set_double(0));
		*consarray = ap_tcons1_array_make(cons.env,1);
		ap_tcons1_array_set(consarray,0,&cons);
		t_cons->push_back(consarray);
	}
}


bool AIPass::computeCondition(	CmpInst * inst, 
		bool result,
		std::vector<ap_tcons1_array_t *> * cons) {

	//Node * n = Nodes[inst->getParent()];
	Node * n = Nodes[focuspath.back()];
	ap_constyp_t constyp;
	ap_constyp_t nconstyp;
	
	Value * op1 = inst->getOperand(0);
	Value * op2 = inst->getOperand(1);

	ap_texpr1_t * exp1 = get_ap_expr(n,op1);
	ap_texpr1_t * exp2 = get_ap_expr(n,op2);
	common_environment(&exp1,&exp2);

	ap_texpr1_t * expr;
	ap_texpr1_t * nexpr;
	ap_texpr1_t * swap;

	ap_texpr_rtype_t ap_type;
	get_ap_type(op1,ap_type);

	expr = ap_texpr1_binop(AP_TEXPR_SUB,
			ap_texpr1_copy(exp1),
			ap_texpr1_copy(exp2),
			ap_type,
			AP_RDIR_RND);

	nexpr = ap_texpr1_binop(AP_TEXPR_SUB,
			ap_texpr1_copy(exp2),
			ap_texpr1_copy(exp1),
			ap_type,
			AP_RDIR_RND);


	switch (inst->getPredicate()) {
		case CmpInst::FCMP_FALSE:
		case CmpInst::FCMP_OEQ: 
		case CmpInst::FCMP_UEQ: 
		case CmpInst::ICMP_EQ:
			constyp = AP_CONS_EQ; // equality constraint
			nconstyp = AP_CONS_DISEQ;
			break;
		case CmpInst::ICMP_UGT: 
		case CmpInst::ICMP_SGT: 
			// in the case of the false constraint, we have to add 1
			// to the nexpr

		case CmpInst::FCMP_OGT:
		case CmpInst::FCMP_UGT:
			constyp = AP_CONS_SUP; // > constraint
			nconstyp = AP_CONS_SUPEQ;
			break;
		case CmpInst::FCMP_OLT: 
		case CmpInst::FCMP_ULT: 
		case CmpInst::ICMP_ULT: 
		case CmpInst::ICMP_SLT: 
			swap = expr;
			expr = nexpr;
			nexpr = swap;
			constyp = AP_CONS_SUP; // > constraint
			nconstyp = AP_CONS_SUPEQ; 
			break;
		case CmpInst::FCMP_OGE:
		case CmpInst::FCMP_UGE:
		case CmpInst::ICMP_UGE:
		case CmpInst::ICMP_SGE:
			constyp = AP_CONS_SUPEQ; // >= constraint
			nconstyp = AP_CONS_SUP;
			break;
		case CmpInst::FCMP_OLE:
		case CmpInst::FCMP_ULE:
		case CmpInst::ICMP_ULE:
		case CmpInst::ICMP_SLE:
			swap = expr;
			expr = nexpr;
			nexpr = swap;
			constyp = AP_CONS_SUPEQ; // >= constraint
			nconstyp = AP_CONS_SUP;
			break;
		case CmpInst::FCMP_ONE:
		case CmpInst::FCMP_UNE: 
		case CmpInst::ICMP_NE: 
		case CmpInst::FCMP_TRUE: 
			constyp = AP_CONS_DISEQ; // disequality constraint
			nconstyp = AP_CONS_EQ;
			break;
		case CmpInst::FCMP_ORD: 
		case CmpInst::FCMP_UNO:
		case CmpInst::BAD_ICMP_PREDICATE:
		case CmpInst::BAD_FCMP_PREDICATE:
			*Out << "ERROR : Unknown predicate\n";
			break;
	}
	if (result) {
		// creating the TRUE constraints
		create_constraints(constyp,expr,nexpr,cons);
	} else {
		// creating the FALSE constraints
		create_constraints(nconstyp,nexpr,expr,cons);
	}
	return true;
}

bool AIPass::computeConstantCondition(	ConstantInt * inst, 
		bool result,
		std::vector<ap_tcons1_array_t*> * cons) {

		// this is always true
		return false;

	//	if (result) {
	//		// always true
	//		return false;
	//	}

	//	// we create a unsat constraint
	//	// such as one of the successor is unreachable
	//	ap_tcons1_t tcons;
	//	ap_tcons1_array_t * consarray;
	//	ap_environment_t * env = ap_environment_alloc_empty();
	//	consarray = new ap_tcons1_array_t();
	//	tcons = ap_tcons1_make(
	//			AP_CONS_SUP,
	//			ap_texpr1_cst_scalar_double(env,1.),
	//			ap_scalar_alloc_set_double(0.));
	//	*consarray = ap_tcons1_array_make(tcons.env,1);
	//	ap_tcons1_array_set(consarray,0,&tcons);
	//	
	//	if (inst->isZero() && !result) {
	//		// condition is always false
	//		cons->push_back(consarray);
	//	}
}

bool AIPass::computePHINodeCondition(PHINode * inst, 
		bool result,
		std::vector<ap_tcons1_array_t*> * cons) {

	bool res;

	// we only consider one single predecessor: the predecessor from the path
	BasicBlock * pred = focuspath[focusblock-1];
	Value * pv;
	for (unsigned i = 0; i < inst->getNumIncomingValues(); i++) {
		if (pred == inst->getIncomingBlock(i)) {
			pv = inst->getIncomingValue(i);

			if (CmpInst * cmp = dyn_cast<CmpInst>(pv)) {
				ap_texpr_rtype_t ap_type;
				if (get_ap_type(cmp->getOperand(0), ap_type)) return false;
				res = computeCondition(cmp,result,cons);
			} else if (ConstantInt * c = dyn_cast<ConstantInt>(pv)) {
				res = computeConstantCondition(c,result,cons);
			} else if (PHINode * phi = dyn_cast<PHINode>(pv)) {
				// I.getOperand(0) could also be a
				// boolean PHI-variable
				res = computePHINodeCondition(phi,result,cons);
			} else {
				// loss of precision...
				return false;
			}
		}
	}
	return res;
}






void AIPass::visitReturnInst (ReturnInst &I){
	//*Out << "returnInst\n" << I << "\n";
}


void AIPass::visitBranchInst (BranchInst &I){
	//*Out << "BranchInst\n" << I << "\n";	
	bool test;
	bool res;

	//*Out << "BranchInst\n" << I << "\n";	
	if (I.isUnconditional()) {
		/* no constraints */
		return;
	}
	// branch under condition

	// what is the successor in our path ?
	// i.e: Does the test has to be 'false' or 'true' ?
	BasicBlock * next = focuspath[focusblock+1];
	if (next == I.getSuccessor(0)) {
		// test is evaluated at true
		test = true;
	} else {
		// test is evaluated at false
		test = false;
	}

	std::vector<ap_tcons1_array_t*> * cons = new std::vector<ap_tcons1_array_t*>();

	if (CmpInst * cmp = dyn_cast<CmpInst>(I.getOperand(0))) {
		ap_texpr_rtype_t ap_type;
		if (get_ap_type(cmp->getOperand(0), ap_type)) return;
		res = computeCondition(cmp,test,cons);
	} else if (ConstantInt * c = dyn_cast<ConstantInt>(I.getOperand(0))) {
		res = computeConstantCondition(c,test,cons);
	} else if (PHINode * phi = dyn_cast<PHINode>(I.getOperand(0))) {
		// I.getOperand(0) could also be a
		// boolean PHI-variable
		res = computePHINodeCondition(phi,test,cons);
	} else {
		// loss of precision...
		return;
	}

	// we add cons in the set of constraints of the path
	if (res) constraints.push_back(cons);
}

/// for the moment, we only create the constraint for the default branch of the
/// switch. The other branches lose information...
/// This code is dead since we use the LowerSwitch pass before doing abstract
/// interpretation.
void AIPass::visitSwitchInst (SwitchInst &I){
	//*Out << "SwitchInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitIndirectBrInst (IndirectBrInst &I){
	//*Out << "IndirectBrInst\n" << I << "\n";	
}

void AIPass::visitInvokeInst (InvokeInst &I){
	//*Out << "InvokeInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitUnwindInst (UnwindInst &I){
	//*Out << "UnwindInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitUnreachableInst (UnreachableInst &I){
	//*Out << "UnreachableInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitICmpInst (ICmpInst &I){
	//*Out << "ICmpInst\n" << I << "\n";	
	//visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitFCmpInst (FCmpInst &I){
	//*Out << "FCmpInst\n" << I << "\n";	
	//visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitAllocaInst (AllocaInst &I){
	//*Out << "AllocaInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitLoadInst (LoadInst &I){
	//*Out << "LoadInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitStoreInst (StoreInst &I){
	//*Out << "StoreInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitGetElementPtrInst (GetElementPtrInst &I){
	//*Out << "GetElementPtrInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitPHINode (PHINode &I){
	Node * n = Nodes[focuspath.back()];
	//*Out << "PHINode\n" << I << "\n";
	// we only consider one single predecessor: the predecessor from the path
	BasicBlock * pred = focuspath[focusblock-1];

	Value * pv;
	Node * nb;

	ap_texpr_rtype_t ap_type;
	if (get_ap_type((Value*)&I, ap_type)) return;
	DEBUG(
		*Out << I << "\n";
	);
	for (unsigned i = 0; i < I.getNumIncomingValues(); i++) {
		if (pred == I.getIncomingBlock(i)) {
			pv = I.getIncomingValue(i);
			nb = Nodes[I.getIncomingBlock(i)];
			ap_texpr1_t * expr = get_ap_expr(n,pv);

			if (focusblock == focuspath.size()-1) {
				if (LV->isLiveThroughBlock(&I,n->bb) || LV->isUsedInBlock(&I,n->bb)) {
					n->add_var(&I);
					PHIvars.name.push_back((ap_var_t)&I);
					PHIvars.expr.push_back(*expr);
					ap_environment_t * env = expr->env;
					insert_env_vars_into_node_vars(env,n,(Value*)&I);
					DEBUG(
						*Out << I << " is equal to ";
						texpr1_print(expr);
						*Out << "\n";
					);
				}
			} else {
				if (expr == NULL) continue;
				DEBUG(
					*Out << I << " is equal to ";
					texpr1_print(expr);
					*Out << "\n";
				);
				set_ap_expr(&I,expr);
				if (LV->isLiveThroughBlock(&I,n->bb) || LV->isUsedInBlock(&I,n->bb)) {
					ap_environment_t * env = expr->env;
					insert_env_vars_into_node_vars(env,n,(Value*)&I);
				}
			}
		}
	}
}

void AIPass::visitTruncInst (TruncInst &I){
	//*Out << "TruncInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitZExtInst (ZExtInst &I){
	//*Out << "ZExtInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitSExtInst (SExtInst &I){
	//*Out << "SExtInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitFPTruncInst (FPTruncInst &I){
	//*Out << "FPTruncInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitFPExtInst (FPExtInst &I){
	//*Out << "FPExtInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitFPToUIInst (FPToUIInst &I){
	//*Out << "FPToUIInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitFPToSIInst (FPToSIInst &I){
	//*Out << "FPToSIInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitUIToFPInst (UIToFPInst &I){
	//*Out << "UIToFPInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitSIToFPInst (SIToFPInst &I){
	//*Out << "SIToFPInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitPtrToIntInst (PtrToIntInst &I){
	//*Out << "PtrToIntInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitIntToPtrInst (IntToPtrInst &I){
	//*Out << "IntToPtrInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitBitCastInst (BitCastInst &I){
	//*Out << "BitCastInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitSelectInst (SelectInst &I){
	//*Out << "SelectInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

// a call instruction is treated as follow : 
// we consider that the function call doesn't modify the value of the different
// variable, and we the result returned by the function is a new variable of
// type int or float, depending on the return type
void AIPass::visitCallInst(CallInst &I){
	//*Out << "CallInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitVAArgInst (VAArgInst &I){
	//*Out << "VAArgInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitExtractElementInst (ExtractElementInst &I){
	//*Out << "ExtractElementInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitInsertElementInst (InsertElementInst &I){
	//*Out << "InsertElementInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitShuffleVectorInst (ShuffleVectorInst &I){
	//*Out << "ShuffleVectorInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitExtractValueInst (ExtractValueInst &I){
	//*Out << "ExtractValueInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitInsertValueInst (InsertValueInst &I){
	//*Out << "InsertValueInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitTerminatorInst (TerminatorInst &I){
	//*Out << "TerminatorInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitBinaryOperator (BinaryOperator &I){
	Node * n = Nodes[focuspath.back()];

	//*Out << "BinaryOperator\n" << I << "\n";	
	ap_texpr_op_t op;
	switch(I.getOpcode()) {
		// Standard binary operators...
		case Instruction::Add : 
		case Instruction::FAdd: 
			op = AP_TEXPR_ADD;
			break;
		case Instruction::Sub : 
		case Instruction::FSub: 
			op = AP_TEXPR_SUB;
			break;
		case Instruction::Mul : 
		case Instruction::FMul: 
			op = AP_TEXPR_MUL;
			break;
		case Instruction::UDiv: 
		case Instruction::SDiv: 
		case Instruction::FDiv: 
			op = AP_TEXPR_DIV;
			break;
		case Instruction::URem: 
		case Instruction::SRem: 
		case Instruction::FRem: 
			op = AP_TEXPR_MOD;
			break;
			// Logical operators
		case Instruction::Shl : // Shift left  (logical)
		case Instruction::LShr: // Shift right (logical)
		case Instruction::AShr: // Shift right (arithmetic)
		case Instruction::And :
		case Instruction::Or  :
		case Instruction::Xor :
		case Instruction::BinaryOpsEnd:
			// we consider the result is unknown
			// so we create a new variable
			visitInstAndAddVarIfNecessary(I);
			return;
	}
	ap_texpr_rtype_t type;
	get_ap_type(&I,type);
	ap_texpr_rdir_t dir = AP_RDIR_RND;
	Value * op1 = I.getOperand(0);
	Value * op2 = I.getOperand(1);

	ap_texpr1_t * exp1 = get_ap_expr(n,op1);
	ap_texpr1_t * exp2 = get_ap_expr(n,op2);
	common_environment(&exp1,&exp2);

	// we create the expression associated to the binary op
	ap_texpr1_t * exp = ap_texpr1_binop(op,exp1, exp2, type, dir);
	set_ap_expr(&I,exp);

	// this value may use some apron variables 
	// we add these variables in the Node's variable structure, such that we
	// remember that instruction I uses these variables
	//
	ap_environment_t * env = exp->env;
	insert_env_vars_into_node_vars(env,n,(Value*)&I);
}

void AIPass::visitCmpInst (CmpInst &I){
	//*Out << "CmpInst\n" << I << "\n";	
	//visitInstAndAddVarIfNecessary(I);
}

void AIPass::visitCastInst (CastInst &I){
	//*Out << "CastInst\n" << I << "\n";	
	visitInstAndAddVarIfNecessary(I);
}


void AIPass::visitInstAndAddVarIfNecessary(Instruction &I) {
	Node * n = Nodes[focuspath.back()];
	ap_environment_t* env = NULL;
	ap_var_t var = (Value *) &I; 

	ap_texpr_rtype_t ap_type;
	
	if (get_ap_type((Value*)&I, ap_type)) return;

	if (!LV->isLiveThroughBlock(&I,I.getParent()) 
		&& !LV->isUsedInBlock(&I,I.getParent()))
		return;

	if (ap_type == AP_RTYPE_INT) { 
		env = ap_environment_alloc(&var,1,NULL,0);
	} else {
		env = ap_environment_alloc(NULL,0,&var,1);
	}
	ap_texpr1_t * exp = ap_texpr1_var(env,var);
	set_ap_expr(&I,exp);
	//print_texpr(exp);
	n->add_var((Value*)var);
}

