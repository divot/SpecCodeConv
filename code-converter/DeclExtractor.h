//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _DECLEXTRACTOR_H_
#define _DECLEXTRACTOR_H_

#include "Classes.h"

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"

using clang::DeclRefExpr;
using clang::NamedDecl;
using clang::RecursiveASTVisitor;

namespace speculation {

class DeclExtractor
    : public RecursiveASTVisitor<DeclExtractor> {

 private:
  
  set<NamedDecl *> &Decls;
 
 public:
 
  DeclExtractor(set<NamedDecl *> &Decls);
  
  bool VisitDeclRefExpr(DeclRefExpr *e);

};

} // End namespace speculation

#endif
