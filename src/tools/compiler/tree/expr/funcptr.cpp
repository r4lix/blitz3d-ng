#include "funcptr.h"
#include "int_const.h"

ExprNode *FuncPtrNode::semant( Environ *e ){
	Decl *d=sem_decl=e->findFunc( ident );
	if( !d || !(d->kind & DECL_FUNC) ) ex( "Function '"+ident+"' not found" );
	sem_type=Type::int_type;
	return this;
}

TNode *FuncPtrNode::translate( Codegen *g ){
	return d_new TNode( IR_CONST,0,0,0 );
}

#ifdef USE_LLVM
llvm::Value *FuncPtrNode::translate2( Codegen_LLVM *g ){
	Decl *d=sem_decl;
	FuncType *f=d->type->funcType();
	auto func=f->llvmFunction( d->name,g );
	return g->CallIntrinsic( "_bbFuncPtr",g->intTy,1,g->builder->CreateBitOrPointerCast( func,g->voidPtr ) );
}
#endif

json FuncPtrNode::toJSON( Environ *e ){
	json tree;tree["@class"]="FuncPtrNode";
	tree["ident"]=ident;
	return tree;
}
