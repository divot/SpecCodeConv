//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _OMPPRAGMAHANDLER_H_
#define _OMPPRAGMAHANDLER_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Pragma.h"

using clang::DiagnosticsEngine;
using clang::IdentifierInfo;
using clang::PragmaHandler;
using clang::PragmaIntroducerKind;
using clang::Preprocessor;
using clang::SourceLocation;
using clang::SourceRange;
using clang::StringRef;
using clang::Token;

//namespace tok = clang::tok;
using clang::tok::TokenKind;

namespace speculation {

class OMPPragmaHandler : public PragmaHandler {

 private:
  
  PragmaDirectiveMap &Directives;
  DiagnosticsEngine &Diags;
  
  unsigned DiagUnrecognisedIdentifier;
  unsigned DiagFoundPragmaStmt;
  unsigned DiagUnsupportedConstruct;
  unsigned DiagMalformedStatement;
  unsigned DiagUnknownClause;
  unsigned DiagUnknownDirective;
  
  Token createToken(SourceLocation &Loc,
                    clang::tok::TokenKind Kind,
                    IdentifierInfo *ident = NULL);

  SourceRange getTokenRange(Token &Tok, Preprocessor &PP);

  bool handleList(Token &Tok, Preprocessor &PP, PragmaClause &C);

  StringRef getIdentifier(Token &Tok);
  
  void LexUntil(Preprocessor &PP, Token &Tok, TokenKind Kind);
  
 public:

  OMPPragmaHandler(PragmaDirectiveMap &Directives, DiagnosticsEngine &Diags);

  virtual void HandlePragma(Preprocessor &PP, PragmaIntroducerKind Introducer,
                            SourceRange IntroducerRange, Token &FirstTok);

};

} // End namespace "speculation"

#endif
