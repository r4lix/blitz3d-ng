#ifndef FUNCPTR_NODE_H
#define FUNCPTR_NODE_H

#include "node.h"

// @Function: address of a user function (Blitz3D-TSS), as an Int token
struct FuncPtrNode : public ExprNode{
	std::string ident;Decl *sem_decl=0;
	FuncPtrNode( const std::string &i ):ident( i ){}
	ExprNode *semant( Environ *e );
	TNode *translate( Codegen *g );
#ifdef USE_LLVM
	llvm::Value *translate2( Codegen_LLVM *g );
#endif
	json toJSON( Environ *e );
};

#endif
