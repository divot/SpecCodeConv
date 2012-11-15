//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "BaseASTConsumer.h"

#include "PragmaDirective.h"
using clang::SourceRange;

namespace speculation {

BaseASTConsumer::BaseASTConsumer(vector<Decl *> &AllDecls, 
                                 DiagnosticsEngine &Diags)
    : SemaConsumer(),
      AllDecls(AllDecls),
      Diags(Diags) { }

bool BaseASTConsumer::HandleTopLevelDecl(DeclGroupRef d) {

  for (DeclGroupRef::iterator it = d.begin(); it != d.end(); it++) {
    AllDecls.push_back(*it);
  }
  
  return true;

}

} // End namespace speculation
