//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _DECLLINKER_H_
#define _DECLLINKER_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"

using clang::BinaryOperator;
using clang::CallExpr;
using clang::CompilerInstance;
using clang::DeclStmt;
using clang::FunctionDecl;
using clang::InitListExpr;
using clang::RecursiveASTVisitor;
using clang::SourceLocation;
using clang::Stmt;
using clang::Type;
using clang::VarDecl;

namespace speculation {

class DeclLinker
    : public RecursiveASTVisitor<DeclLinker> {

      DeclTracker *TrackedVars;
      FunctionTracker *CurrentFunction;
      CompilerInstance *CI;

     public:

      DeclLinker(DeclTracker *TrackedVars);

      void HandleFunction(FunctionDecl *TheFunction);

      bool VisitCallExpr(CallExpr *e);

      // Variable Usage Tracking
      bool VisitDeclStmt(DeclStmt *s);
      bool VisitBinaryOperator(BinaryOperator *e);

      void HandleArrayInit(VarDecl *VDLHS, string TLHS, SourceLocation StmtLoc);
      void HandleArrayInitList(VarDecl *VDLHS,
                               string TLHS,
                               InitListExpr * Inits,
                               SourceLocation StmtLoc);
      void HandlePointerInit(VarDecl *VDLHS, string TLHS, SourceLocation StmtLoc);

      void maybeSwapped(VarDecl *VDLHS,
                        string TLHS,
                        VarDecl *VDRHS,
                        string TRHS,
                        const Type * T,
                        SourceLocation StmtLoc);

};

} // End namespace speculation

#endif
