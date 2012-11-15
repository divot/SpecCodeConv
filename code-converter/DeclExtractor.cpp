//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "DeclExtractor.h"

#include "Globals.h"

namespace speculation {

DeclExtractor::DeclExtractor(set<NamedDecl *> &Decls)
    : RecursiveASTVisitor<DeclExtractor>(),
      Decls(Decls) {

}

bool DeclExtractor::VisitDeclRefExpr(DeclRefExpr *e) {

  Decls.insert(globals::GetNamedDecl(e->getDecl()));
  
  return true;

}

} // End namespace speculation

