//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _DIRECTIVEFINDER_H_
#define _DIRECTIVEFINDER_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"

using clang::CompilerInstance;
using clang::CompoundStmt;
using clang::ForStmt;
using clang::RecursiveASTVisitor;
using clang::SourceRange;
using clang::Stmt;

namespace speculation {

class DirectiveFinder
    : public RecursiveASTVisitor<DirectiveFinder> {

 private:
  
  PragmaDirectiveMap &Directives;
  DirectiveList &FullDirectives;
  CompilerInstance &CI;

  bool Waiting;
  PragmaDirective *CurrentDirective;
  CompoundStmt *CurrentDirectiveHeader;

  bool Evolve;

  void HandlePossiblePragma(Stmt *S);

public:
  
  DirectiveFinder(PragmaDirectiveMap &Directives,
                  DirectiveList &FullDirectives,
                  CompilerInstance &CI);
  
  bool VisitCompoundStmt(CompoundStmt *S);
  
  bool VisitForStmt(ForStmt *S);
  
  bool VisitStmt(Stmt *S);

};

} // End namespace speculation

#endif
