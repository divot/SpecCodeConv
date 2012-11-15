//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "DirectiveFinder.h"
#include "DirectiveList.h"
#include "PragmaDirective.h"
#include "Tools.h"

using speculation::tools::InsideRange;
using speculation::tools::IsChild;

namespace speculation {

DirectiveFinder::DirectiveFinder(PragmaDirectiveMap &Directives,
                                 DirectiveList &FullDirectives,
                                 CompilerInstance &CI)
  : RecursiveASTVisitor<DirectiveFinder>(),
    Directives(Directives),
    FullDirectives(FullDirectives),
    CI(CI),
    Waiting(false),
    CurrentDirective(NULL),
    CurrentDirectiveHeader(NULL) { }

void DirectiveFinder::HandlePossiblePragma(Stmt *S) {

  if (IsChild(S, CurrentDirectiveHeader)) {
    // We're in the header of the a previous pragma.
    // There's no new pragma here
    return;
  }

  // Found a Stmt after a #pragma
  Waiting = false;

  if (FullDirectives.InsideTopLevel(S)) {
    // This pragma is a sub-pragma
    // We don't add a top level
    return;
  }

  DiagnosticsEngine &Diags = CI.getDiagnostics();

  unsigned DiagID =
      Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                            "Found a top-level OpenMP directive");
  Diags.Report(CurrentDirectiveHeader->getLocStart(), DiagID);


  FullDirectives.CreateTopLevel(CurrentDirective,
                                CurrentDirectiveHeader,
                                S,
                                CI);
  
}

bool DirectiveFinder::VisitCompoundStmt(CompoundStmt *S) {

  if (Waiting) {
    HandlePossiblePragma(S);
  } else {

    PragmaDirectiveMap::iterator it;
    
    it = Directives.find(S->getLocStart().getRawEncoding());
  
    if (it != Directives.end()) {
    
      // Found a #pragma stmt

      CurrentDirective = it->second;
      CurrentDirectiveHeader = S;
      Waiting = true;
        
    }
    
  }
  
  return true;

}

bool DirectiveFinder::VisitStmt(Stmt *S) {

  if (CompoundStmt::classof(S) || ForStmt::classof(S)) {
    return true;
  }

  if (Waiting) {
    HandlePossiblePragma(S);
  }

  return true;

}

bool DirectiveFinder::VisitForStmt(ForStmt *S) {

  if (Waiting) {
    HandlePossiblePragma(S);
  }
  
  return true;

}

} // End namespace speculation

