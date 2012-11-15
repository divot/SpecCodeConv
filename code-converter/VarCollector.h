//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _VARCOLLECTOR_H_
#define _VARCOLLECTOR_H_

#include "Classes.h"

#include "clang/Basic/FileManager.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Lookup.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/ParentMap.h"
#include "clang/Frontend/CompilerInstance.h"

using clang::CallExpr;
using clang::ASTContext;
using clang::RecursiveASTVisitor;
using clang::Sema;
using clang::Rewriter;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::ParentMap;
using clang::DeclRefExpr;
using clang::Expr;
using clang::Stmt;
using clang::CompoundStmt;
using clang::ForStmt;
using clang::DeclStmt;
using clang::BinaryOperator;
using clang::VarDecl;
using clang::InitListExpr;
using clang::CompilerInstance;
using clang::Type;
using clang::MemberExpr;

namespace speculation {

class VarCollector
    : public RecursiveASTVisitor<VarCollector> {

 private:
  
  PragmaDirectiveMap *Directives;
  DirectiveList *FullDirectives;

  PragmaDirective * WaitingDirective;
  CompoundStmt * WaitingHeader;

  vector<MemberExpr *> MemberWarnings;
  vector<CompilerInstance *> CompilerInstanceStack;

 public:

  VarCollector();

  void HandleDirective(PragmaDirectiveMap *Directives,
                       DirectiveList *FullDirectives,
                       FullDirective *FD);

  // Directive Header Handling
  bool VisitCompoundStmt(CompoundStmt *s);
  bool VisitStmt(Stmt *s);
  bool VisitForStmt(ForStmt *s);
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
  string GetStmtString(Stmt * Current);

  void maybeContaminated(VarDecl *VDLHS,
                         string TLHS,
                         VarDecl *VDRHS,
                         string TRHS,
                         const Type * T,
                         SourceLocation StmtLoc);

};


} // End namespace speculation

#endif
