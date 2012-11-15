//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _VARTRAVERSER_H_
#define _VARTRAVERSER_H_

#include "Classes.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/AST.h"

using clang::CompilerInstance;
using clang::DeclRefExpr;
using clang::Expr;
using clang::ParentMap;
using clang::RecursiveASTVisitor;
using clang::UnaryOperator;

namespace speculation {

class VarTraverser
    : public RecursiveASTVisitor<VarTraverser> {

 private:
 
  Expr *Parent;
  DeclRefExpr *&OutRef;
  Expr *&OutExpr;
  CompilerInstance &CI;
  bool AddrOf;
  ParentMap *PM;
  
 public:
 
  VarTraverser(Expr *Parent,
               DeclRefExpr *&OutRef,
               Expr *&OutExpr,
               CompilerInstance &CI,
               bool AddrOf = false);
  
  bool VisitDeclRefExpr(DeclRefExpr *e);
  bool VisitUnaryOperator(UnaryOperator *e);
  
  void TraverseExpr(DeclRefExpr * Ref, Expr * Current, Expr * Var);
  
  void HandleBinaryOperator(DeclRefExpr * Original, Expr * Next, Expr * Var);
  void HandleUnaryOperator(DeclRefExpr * Original, Expr * Next, Expr * Var);
  void HandleArraySubscriptExpr(DeclRefExpr * Original, Expr * Next, Expr * Var);
  void HandleMemberExpr(DeclRefExpr * Original, Expr * Next, Expr * Var);

  
};

} // End namespace speculation

#endif
