//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "VarTraverser.h"

#include "Tools.h"

using clang::dyn_cast;

using clang::ArraySubscriptExpr;
using clang::BinaryOperator;
using clang::MemberExpr;

using speculation::tools::GetRelevantParent;
using speculation::tools::IsArrayIndex;
//using speculation::tools::IsPointerOrArrayType;
using speculation::tools::IsPointerArrayStructUnionType;
using speculation::tools::IsWrite;
using speculation::tools::GetStmtString;

namespace speculation {

VarTraverser::VarTraverser(Expr *Parent, 
                           DeclRefExpr *&OutRef,
                           Expr *&OutExpr,
                           CompilerInstance &CI,
                           bool AddrOf)
                            : RecursiveASTVisitor<VarTraverser>(),
                              Parent(Parent),
                              OutRef(OutRef),
                              OutExpr(OutExpr),
                              CI(CI),
                              AddrOf(AddrOf),
                              PM(new ParentMap(Parent)) {
}

bool VarTraverser::VisitDeclRefExpr(DeclRefExpr *e) {

  TraverseExpr(e, e, e);

  return true;

}

bool VarTraverser::VisitUnaryOperator(UnaryOperator *e) {

  if (e->getOpcode() == clang::UO_AddrOf) {

    DeclRefExpr * DominantRef = NULL;
    Expr * DominantExpr = NULL;
    
    VarTraverser lvt(e->getSubExpr(), DominantRef, DominantExpr, CI, true);
    lvt.TraverseStmt(e->getSubExpr());
    
    assert(DominantRef);
    
    TraverseExpr(DominantRef, e, e);
    
  }
  
  return true;

}

void VarTraverser::TraverseExpr(DeclRefExpr * Original,
                                Expr * Current, 
                                Expr * Var) {

  // If we get passed just a value expr, then this is not 
  // the ref we're looking for.
  // Unless we've got a AddrOf Operator (&), then we could
  // be looking for a value expr. But in that case, there can
  // be only one. Or, there can be a Deref Operator (*) with
  // a dominant pointer.
  if (!AddrOf && !IsPointerArrayStructUnionType(Current)) {
    return;
  }

  Expr * Next = GetRelevantParent(Current, PM);
  
  if (Next) {
  
    switch(Next->getStmtClass()) {
    
     case Stmt::BinaryOperatorClass:
     case Stmt::CompoundAssignOperatorClass:

      HandleBinaryOperator(Original, Next, Var);
      break;
      
     case Stmt::UnaryOperatorClass:

      HandleUnaryOperator(Original, Next, Var);
      break;

     case Stmt::ArraySubscriptExprClass:

      HandleArraySubscriptExpr(Original, Next, Var);
      break;

     case Stmt::MemberExprClass:

       HandleMemberExpr(Original, Next, Var);
       break;

     default:
      assert(false && "Error: Relevant Parent returned something unexpected!");
    }
  
  } else {

    // No Relevant Parent!
    // Must Have Hit Top Of Expr =D
    // These are the droids we're looking for
    // Now work with the Ref and the Type of Var (NOT CURRENT)
    OutRef = Original;
    OutExpr = Var;
    
  }

}

void VarTraverser::HandleBinaryOperator(DeclRefExpr * Original,
                                        Expr * Next,
                                        Expr * Var) {

  BinaryOperator * BO = dyn_cast<BinaryOperator>(Next);
  assert(BO);
  
  if (BO->isAssignmentOp()) {
  
    // Check we're in the LHS and traverse, else return
    if (IsWrite(BO, Next, CI)) {
      TraverseExpr(Original, Next, Var);
    }
    
  } else if (BO->getOpcode() == clang::BO_Comma) {
  
    // Check we're in the RHS and traverse, else return
    if (!IsWrite(BO, Next, CI)) {
      TraverseExpr(Original, Next, Var);
    }    
    
  } else {
  
    // Just traverse
    TraverseExpr(Original, Next, Var);
    
  }
  
}

void VarTraverser::HandleUnaryOperator(DeclRefExpr * Original,
                                       Expr * Next,
                                       Expr * Var) {
  
  UnaryOperator * UO = dyn_cast<UnaryOperator>(Next);
  assert(UO);
  
  switch(UO->getOpcode()) {
  
   case clang::UO_Deref:
   
    if (AddrOf) {
    
      DeclRefExpr * DominantRef = NULL;
      Expr * DominantExpr = NULL;
      
      VarTraverser lvt(UO->getSubExpr(), DominantRef, DominantExpr, CI);
      lvt.TraverseStmt(UO->getSubExpr());
      
      assert(DominantRef);
      
      if (DominantRef == Original) {
        TraverseExpr(Original, Next, Next);
      }

    } else {
      TraverseExpr(Original, Next, Next);
    }

    break;
    
   case clang::UO_AddrOf:
   
    // If we get an AddrOf operator then this is handled by
    // the recursive AST visitor, (VisitUnaryOperator)
    // We stop caring about this ref so just return.
    break;
    
   default:
   
    TraverseExpr(Original, Next, Var);
    
  }
  
}

void VarTraverser::HandleArraySubscriptExpr(DeclRefExpr * Original,
                                            Expr * Next,
                                            Expr * Var) {
                                            
  ArraySubscriptExpr * Array = dyn_cast<ArraySubscriptExpr>(Next);
  assert(Array);

  // Techinically don't need this check, as a pointer can't be an array index?
  if (!IsArrayIndex(Array, Var, CI)) {
    TraverseExpr(Original, Next, Next);
  }
  
}

void VarTraverser::HandleMemberExpr(DeclRefExpr * Original,
                                    Expr * Next,
                                    Expr * Var) {

  MemberExpr * Member = dyn_cast<MemberExpr>(Next);
  assert(Member);

  TraverseExpr(Original, Member, Member);

}

} // End namespace speculation
