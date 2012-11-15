//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _DIRECTIVEHANDLER_H_
#define _DIRECTIVEHANDLER_H_

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

using clang::ASTContext;
using clang::RecursiveASTVisitor;
using clang::Sema;
using clang::FileID;
using clang::Rewriter;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::ParentMap;
using clang::CompilerInstance;

using clang::Decl;
using clang::DeclRefExpr;
using clang::Expr;
using clang::Stmt;
using clang::CompoundStmt;
using clang::ForStmt;
using clang::IfStmt;
using clang::WhileStmt;
using clang::DoStmt;
using clang::SwitchStmt;
using clang::DeclStmt;
using clang::ReturnStmt;

namespace speculation {

typedef struct {
  bool insertAfter;
  Stmt * stmt;
} StmtPair;

class DirectiveHandler
    : public RecursiveASTVisitor<DirectiveHandler> {

 private:
  
  PragmaDirectiveMap *Directives;
  DirectiveList *FullDirectives;

  ParentMap * PM;

  //set<FileID> files; // TODO: Fix to work with multiple CI's
  // Put this in global?
  map<CompilerInstance *, set<FileID> > files;

  struct SourceRangeComp {
    bool operator() (const SourceRange &lhs, const SourceRange &rhs) const {
      return lhs.getBegin().getRawEncoding() < rhs.getBegin().getRawEncoding();
    }
  };

  struct SourceLocationComp {
    bool operator() (const SourceLocation &lhs, const SourceLocation &rhs) const {
      return lhs.getRawEncoding() < rhs.getRawEncoding();
    }
  };

  typedef set<SourceRange, SourceRangeComp> SourceRangeSet;
  typedef multimap<SourceLocation, string, SourceLocationComp> SourceLocationMultimap;

  SourceRangeSet BracketLocs;
  SourceLocationMultimap ReadLocs;
  SourceLocationMultimap WriteLocs;
  
  PragmaDirective * WaitingDirective;
  CompoundStmt * WaitingHeader;


 public:

  DirectiveHandler(DirectiveList *FullDirectives);

  void Finish();
  
  void SetParentMap(Stmt * s);

  void HandleStackItem(PragmaDirectiveMap *Directives, StackItem *FD);

  bool VisitDeclStmt(DeclStmt *s);
  bool VisitDeclRefExpr(DeclRefExpr *e);
  bool VisitCompoundStmt(CompoundStmt *s);
  bool VisitForStmt(ForStmt *s);
  bool VisitStmt(Stmt *s);

  Expr * getVar(Expr * Original);
  Stmt * getBase(Expr * Original);
  Stmt * getParent(Stmt * Base);
  
  vector<StmtPair> GenerateWritePairs(Stmt * Base, Stmt * Parent, Stmt * Top);
  vector<StmtPair> GenerateWritePairs(Stmt * Base, CompoundStmt * Parent, Stmt * Top);
  vector<StmtPair> GenerateWritePairs(Stmt * Base, ForStmt * Parent, Stmt * Top);
  vector<StmtPair> GenerateWritePairs(Stmt * Base, IfStmt * Parent, Stmt * Top);
  vector<StmtPair> GenerateWritePairs(Stmt * Base, WhileStmt * Parent, Stmt * Top);
  vector<StmtPair> GenerateWritePairs(Stmt * Base, DoStmt * Parent, Stmt * Top);
  vector<StmtPair> GenerateWritePairs(Stmt * Base, SwitchStmt * Parent, Stmt * Top);
  
  void WalkUpExpr(DeclRefExpr * Original, 
                  Expr * Var,
                  vector<StmtPair> WritePairs,
                  bool ActualVar,
                  string Struct);
                  
  void HandleBinaryOperator(DeclRefExpr * Original,
                            Expr * Current,
                            Expr * Next,
                            vector<StmtPair> WritePairs,
                            bool ActualVar,
                            string Struct);
  
  void HandleUnaryOperator(DeclRefExpr * Original,
                           Expr * Current,
                           Expr * Next,
                           vector<StmtPair> WritePairs,
                           bool ActualVar,
                           string Struct);
  
  void HandleArraySubscriptExpr(DeclRefExpr * Original,
                                Expr * Current,
                                Expr * Next,
                                vector<StmtPair> WritePairs,
                                bool ActualVar,
                                string Struct);
  
  void HandleMemberExpr(DeclRefExpr * Original,
                        Expr * Current,
                        Expr * Next,
                        vector<StmtPair> WritePairs,
                        bool ActualVar,
                        string Struct);

  void InsertAccess(Expr * Current, 
                    DeclRefExpr * Original,
                    bool Write,
                    vector<StmtPair> WritePairs,
                    bool ActualVar,
                    string Struct);

  bool GetOrSetAccessed(SourceLocation Loc, Expr * Current, bool Write);
  
  string GetStmtString(Stmt * Current);

  void InsertBrackets(SourceRange StmtRange, CompilerInstance *CI = 0);

  void InsertCacheAssignments(StackItem * SI);
  void InsertCacheAssignments(FullDirective * FD);
  void InsertCacheAssignments(SpeculativeFunction * SF);

  void InsertChecks(CompilerInstance &CI, PragmaDirectiveMap &Directives);
  void InsertInit();

};


} // End namespace speculation

#endif
