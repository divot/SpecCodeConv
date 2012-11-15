//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _BASEASTCONSUMER_H_
#define _BASEASTCONSUMER_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/Sema/SemaConsumer.h"

using clang::Decl;
using clang::DeclGroupRef;
using clang::DiagnosticsEngine;
using clang::SemaConsumer;

namespace speculation {

class BaseASTConsumer : public SemaConsumer {

 private:

  vector<Decl *> &AllDecls;
  DiagnosticsEngine &Diags;

 public:
 
  BaseASTConsumer(vector<Decl *> &AllDecls,
                  DiagnosticsEngine &Diags);
 
  virtual bool HandleTopLevelDecl(DeclGroupRef d);
  
};

} // End namespace speculation

#endif
