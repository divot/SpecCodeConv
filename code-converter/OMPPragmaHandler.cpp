//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "OMPPragmaHandler.h"

#include "PragmaDirective.h"

namespace speculation {

OMPPragmaHandler::OMPPragmaHandler(PragmaDirectiveMap &Directives,
                                   DiagnosticsEngine &Diags) 
  : PragmaHandler("omp"),
    Directives(Directives),
    Diags(Diags),
    DiagUnrecognisedIdentifier(0),
    DiagFoundPragmaStmt(0),
    DiagUnsupportedConstruct(0),
    DiagMalformedStatement(0),
    DiagUnknownClause(0),
    DiagUnknownDirective(0) {

  DiagUnrecognisedIdentifier = 
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Pragma Handler: Unrecognised identifier");

  DiagFoundPragmaStmt = 
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Pragma Handler: Found pragma statement");

  DiagUnsupportedConstruct = 
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Pragma Handler: Unsupported construct");

  DiagMalformedStatement = 
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Pragma Handler: Malformed statement");

  DiagUnknownClause = 
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Pragma Handler: Unknown clause");
    
  DiagUnknownDirective = 
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Pragma Handler: Unknown directive");
    
}

Token OMPPragmaHandler::createToken(SourceLocation &Loc,
                                    clang::tok::TokenKind Kind,
                                    IdentifierInfo * Ident) {
  Token Tok;

  Tok.startToken();

  Tok.setLocation(Loc);
  Tok.setKind(Kind);
  
  if (Kind == clang::tok::identifier) {
    assert(Ident);
    Tok.setIdentifierInfo(Ident);
  }
  
  return Tok;

}

SourceRange OMPPragmaHandler::getTokenRange(Token &Tok, Preprocessor &PP) {

  SourceLocation Start = Tok.getLocation();
  SourceLocation End = PP.getLocForEndOfToken(Start);
  
  return SourceRange(Start, End);

}

bool OMPPragmaHandler::handleList(Token &Tok,
                                  Preprocessor &PP,
                                  PragmaClause &C) {
  
  if (!Tok.is(clang::tok::l_paren)) return false;
  PP.Lex(Tok);
  
  if (C.Type == ReductionClause) {
    C.Op = Tok;
    PP.Lex(Tok);
    if (Tok.isNot(clang::tok::colon)) return false;
    PP.Lex(Tok);
  } else if (C.Type == IfClause 
             || C.Type == NumThreadsClause
             || C.Type == DefaultClause
             || C.Type == ScheduleClause) {
    
    LexUntil(PP, Tok, clang::tok::r_paren);
    
    return Tok.is(clang::tok::r_paren);
    
  }

  while(Tok.isNot(clang::tok::eod)) {
          
    if (! (Tok.is(clang::tok::identifier))) return false;

    C.Options.push_back(Tok.getIdentifierInfo());
    PP.Lex(Tok);

    if (Tok.is(clang::tok::r_paren)) {
      break;
    }
    
    if (Tok.isNot(clang::tok::comma)) return false;
    PP.Lex(Tok);
    
  }
  
  if (Tok.is(clang::tok::eod)) {
    return false;
  }
  
  return true;

}

StringRef OMPPragmaHandler::getIdentifier(Token &Tok) {

  switch(Tok.getKind()) {
   case clang::tok::identifier:
    return Tok.getIdentifierInfo()->getName();
   case clang::tok::kw_for:
    return StringRef("for");
   case clang::tok::eod:
    return StringRef();
   default:
    Diags.Report(Tok.getLocation(), DiagUnrecognisedIdentifier);
    return StringRef();
  }
  
}

void OMPPragmaHandler::LexUntil(Preprocessor &PP, Token &Tok, TokenKind Kind) {

  while (Tok.isNot(Kind) && Tok.isNot(clang::tok::eod)) {
    PP.Lex(Tok);
  }

}

void OMPPragmaHandler::HandlePragma(Preprocessor &PP,
                                    PragmaIntroducerKind Introducer,
                                    SourceRange IntroducerRange,
                                    Token &FirstTok) {


  Diags.Report(IntroducerRange.getBegin(), DiagFoundPragmaStmt);
                                    
  // TODO: Clean this up because I'm too lazy to now
  PragmaDirective * DirectivePointer = new PragmaDirective;
  PragmaDirective &Directive = *DirectivePointer;
    
  // First lex the pragma statement extracting the variable names

  SourceLocation Loc = IntroducerRange.getBegin();
  Token Tok = FirstTok;
  StringRef ident = getIdentifier(Tok);
  
  if (ident != "omp") {
    LexUntil(PP, Tok, clang::tok::eod);
    return;
  }
    
  PP.Lex(Tok);
  ident = getIdentifier(Tok);
  
  bool isParallel = false;
  bool isThreadPrivate = false;

  if (ident == "parallel") {

    PragmaConstruct C;
    C.Type = ParallelConstruct;
    C.Range = getTokenRange(Tok, PP);
    Directive.insertConstruct(C);
    isParallel = true;

  } else if (ident == "sections"
             || ident == "section"
             || ident == "task"
             || ident == "taskyield"
             || ident == "taskwait"
             || ident == "atomic"
             || ident == "ordered") {

    Diags.Report(Tok.getLocation(), DiagUnsupportedConstruct);

    LexUntil(PP, Tok, clang::tok::eod);
    return;

  } else if (ident == "for") {

    PragmaConstruct C;
    C.Type = ForConstruct;
    C.Range = getTokenRange(Tok, PP);
    Directive.insertConstruct(C);

  } else if (ident == "threadprivate") {
  
    isThreadPrivate = true;

    PragmaConstruct C;
    C.Type = ThreadprivateConstruct;
    C.Range = getTokenRange(Tok, PP);
    Directive.insertConstruct(C);
  
  } else if (ident == "single") {

    PragmaConstruct C;
    C.Type = SingleConstruct;
    C.Range = getTokenRange(Tok, PP);
    Directive.insertConstruct(C);

  } else if (ident == "master") {

    PragmaConstruct C;
    C.Type = MasterConstruct;
    C.Range = getTokenRange(Tok, PP);
    Directive.insertConstruct(C);

  } else if (ident == "critical"
             || ident == "flush") {

    // Ignored Directive
    // (Critical, Flush)
    LexUntil(PP, Tok, clang::tok::eod);
    return;
  
  } else if (ident == "barrier") {

    PragmaConstruct C;
    C.Type = BarrierConstruct;
    C.Range = getTokenRange(Tok, PP);
    Directive.insertConstruct(C);

  } else {
    
    Diags.Report(Tok.getLocation(), DiagUnknownDirective);
    return;
    
  }
  
  if (!isThreadPrivate) {
    PP.Lex(Tok);
  }

  if (isParallel) {

    ident = getIdentifier(Tok);
    
    if (ident == "sections") {

      Diags.Report(Tok.getLocation(), DiagUnsupportedConstruct);

      LexUntil(PP, Tok, clang::tok::eod);
      return;

    } else if (ident == "for") {

      PragmaConstruct C;
      C.Type = ForConstruct;
      C.Range = getTokenRange(Tok, PP);
      Directive.insertConstruct(C);
    
      PP.Lex(Tok);
      
    } else {

      // Just a standard "#pragma omp parallel" clause
      if (Tok.isNot(clang::tok::eod)
             && PragmaDirective::getClauseType(ident)
                == UnknownClause) {
       
        Diags.Report(Tok.getLocation(), DiagUnknownClause);
        return;
                
      }

    }
  
  }
  
  // If we've made it this far then we either have:
  // "#pragma omp parallel",
  // "#pragma omp parallel for",
  // "#pragma omp for",
  // "#pragma omp threadprivate
  
  // Need to read in the options, if they exists
  // Don't really care about them unless there exists a private(...) list
  // In which case, get the variables inside that list
  // But we read them all in anyway.

  // There's also threadprivate, which won't have any clauses, but will have
  // a list of private variables just after the threadprivate directive
  // Treating threadprivate as a clause and directive at the same time.
  
  while(Tok.isNot(clang::tok::eod)) {
  
    PragmaClause C;

    ident = getIdentifier(Tok);
    C.Type = PragmaDirective::getClauseType(ident);

    if (C.Type == UnknownClause) {
     
      Diags.Report(Tok.getLocation(), DiagUnknownClause);
      return;
              
    }

    SourceLocation clauseStart = Tok.getLocation();
    SourceLocation clauseEnd = PP.getLocForEndOfToken(clauseStart);

    PP.Lex(Tok);
      
    if (Tok.is(clang::tok::l_paren)) {

      if (!handleList(Tok, PP, C)) {
  
        Diags.Report(clauseStart, DiagMalformedStatement);

        LexUntil(PP, Tok, clang::tok::eod);
        return;
      }
      
      clauseEnd = PP.getLocForEndOfToken(Tok.getLocation());

      // Eat the clang::tok::r_paren
      PP.Lex(Tok);

    }
    
    C.Range = SourceRange(clauseStart, clauseEnd);
    
    Directive.insertClause(C);

  }
  
  SourceLocation EndLoc = PP.getLocForEndOfToken(Tok.getLocation());

  Directive.setRange(SourceRange(Loc, EndLoc));

  Directives.insert(std::make_pair(Loc.getRawEncoding(), DirectivePointer));

  // Then replace with parseable compound statement to catch in Sema, and 
  // references to private variables;
  // {
  //   i;
  //   j;
  //   k;
  // }
  
  // If it's a threadprivate directive, then we skip this completely
  if (isThreadPrivate) {
    return;
  }
  
  set<IdentifierInfo *> PrivateVars = Directive.getPrivateIdentifiers();

  int tokenCount = 2 + 2 * PrivateVars.size();
  int currentToken = 0;    
  
  Token * Toks = new Token[tokenCount];

  Toks[currentToken++] = createToken(Loc, clang::tok::l_brace);

  set<IdentifierInfo *>::iterator PrivIt;
  for (PrivIt = PrivateVars.begin(); PrivIt != PrivateVars.end(); PrivIt++) {
  
    Toks[currentToken++] = createToken(Loc, clang::tok::identifier, *PrivIt);
    Toks[currentToken++] = createToken(Loc, clang::tok::semi);

  }

  Toks[currentToken++] = createToken(EndLoc, clang::tok::r_brace);

  assert(currentToken == tokenCount);
  
  Diags.setDiagnosticGroupMapping("unused-value", 
                                  clang::diag::MAP_IGNORE,
                                  Loc);

  Diags.setDiagnosticGroupMapping("unused-value", 
                                  clang::diag::MAP_WARNING,
                                  EndLoc);

  PP.EnterTokenStream(Toks, tokenCount, true, true);
  
}

} // End namespace "speculation"
