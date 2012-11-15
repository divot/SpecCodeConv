//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "Tools.h"

#include "Globals.h"
#include "NoEditStmtPrinter.h"

using clang::cast;
using clang::dyn_cast;

using clang::CompoundStmt;
using clang::DiagnosticsEngine;
using clang::ForStmt;
using clang::FileEntry;
using clang::FileID;
using clang::FullSourceLoc;
using clang::Lexer;
using clang::PrintingPolicy;
using clang::Qualifiers;
using clang::QualType;
using clang::StringRef;
using clang::Token;

namespace speculation {

namespace tools {

SourceRange GetChildRange(Stmt *S, CompilerInstance &CI) {

  SourceLocation Start = S->getLocStart();
  SourceLocation End;

  ForStmt * F = dyn_cast<ForStmt>(S);
  if (F) {
    S = F->getBody();
  }

  if (dyn_cast<CompoundStmt>(S)) {
    End = S->getLocEnd().getLocWithOffset(1);
  } else {
    End = FindLocationAfterSemi(S->getLocEnd(), CI.getASTContext());
  }

  return SourceRange(Start, End);

}

SourceLocation GetNearestValidLoc(Stmt * S,
                                  SourceManager &SM,
                                  ParentMap *PM) {

  FileID FID = SM.getFileID(S->getLocStart());
  const FileEntry * FE = SM.getFileEntryForID(FID);

  while (!FE) {
    S = PM->getParent(S);
    FID = SM.getFileID(S->getLocStart());
    FE = SM.getFileEntryForID(FID);
  }
  
  return S->getLocStart();

}

static SourceLocation skipToMacroArgExpansion(const SourceManager &SM,
                                              SourceLocation StartLoc) {
  for (SourceLocation L = StartLoc; L.isMacroID();
       L = SM.getImmediateSpellingLoc(L)) {
    if (SM.isMacroArgExpansion(L))
      return L;
  }

  // Otherwise just return initial location, there's nothing to skip.
  return StartLoc;
}

static SourceLocation getImmediateMacroCallerLoc(const SourceManager &SM,
                                                 SourceLocation Loc) {
  if (!Loc.isMacroID()) return Loc;

  // When we have the location of (part of) an expanded parameter, its spelling
  // location points to the argument as typed into the macro call, and
  // therefore is used to locate the macro caller.
  if (SM.isMacroArgExpansion(Loc))
    return SM.getImmediateSpellingLoc(Loc);

  // Otherwise, the caller of the macro is located where this macro is
  // expanded (while the spelling is part of the macro definition).
  return SM.getImmediateExpansionRange(Loc).first;
}

SourceLocation UnpackMacroLoc(SourceLocation Loc, SourceManager &SM) {

  while (Loc.isMacroID()) {
    Loc = skipToMacroArgExpansion(SM, Loc);
    Loc = getImmediateMacroCallerLoc(SM, Loc);
  }

  return Loc;

}

SourceLocation UnpackMacroLoc(SourceLocation Loc, CompilerInstance &CI) {
  return UnpackMacroLoc(Loc, CI.getSourceManager());
}

string GetLocation(SourceLocation Loc, SourceManager &SM) {

  stringstream ss;

  //Loc = UnpackMacroLoc(Loc, SM);

  const FileID FID = SM.getFileID(Loc);
  const FileEntry * FE = SM.getFileEntryForID(FID);
  
  unsigned FileOffset = SM.getFileOffset(Loc);
  unsigned LineNumber = SM.getLineNumber(FID, FileOffset);
  unsigned ColumnNumber = SM.getColumnNumber(FID, FileOffset);
  
  if (FE) {
    ss << FE->getName();
  } else {
    ss << "<InvalidFile>";
    DiagnosticsEngine &Diags = SM.getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Could not find location!!!");
    Diags.Report(Loc, DiagID);

  }
  
  ss << ":" << LineNumber << ":" << ColumnNumber;

  return ss.str();
  
}

string GetLocation(SourceLocation Loc, CompilerInstance &CI) {
  return GetLocation(Loc, CI.getSourceManager());
}

string GetLocation(SourceLocation Loc, ASTContext &Context) {
  return GetLocation(Loc, Context.getSourceManager());
}

string GetLocation(SourceLocation Loc, Preprocessor &PP) {
  return GetLocation(Loc, PP.getSourceManager());
}

SourceLocation FindLocationAfterSemi(SourceLocation loc,
                                     ASTContext &Context) {

  SourceLocation SemiLoc = FindSemiAfterLocation(loc, Context);

  if (SemiLoc.isInvalid())
    return SourceLocation();

  return SemiLoc.getLocWithOffset(1);

}

SourceLocation FindSemiAfterLocation(SourceLocation loc,
                                     ASTContext &Context) {

  SourceManager &SM = Context.getSourceManager();
  if (loc.isMacroID()) {
    if (!Lexer::isAtEndOfMacroExpansion(loc, SM, Context.getLangOpts(), &loc))
      return SourceLocation();
  }
  loc = Lexer::getLocForEndOfToken(loc, /*Offset=*/0, SM, Context.getLangOpts());

  // Break down the source location.
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);

  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return SourceLocation();

  const char *tokenBegin = file.data() + locInfo.second;

  // Lex from the start of the given location.
  Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
              Context.getLangOpts(),
              file.begin(), tokenBegin, file.end());
  Token tok;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(clang::tok::semi))
    return SourceLocation();

  return tok.getLocation();

}

bool InsideRange(SourceLocation Loc, SourceRange Range, CompilerInstance &CI) {

  SourceManager &SM = CI.getSourceManager();

  if (!SM.isLocalSourceLocation(Loc)) {
    return false;
  }

  Loc = UnpackMacroLoc(Loc, CI);

//  ASTContext &Context = CI.getASTContext();

//  FullSourceLoc Begin = Context.getFullLoc(UnpackMacroLoc(Range.getBegin(), CI));
//  FullSourceLoc End = Context.getFullLoc(UnpackMacroLoc(Range.getEnd(), CI));
//  FullSourceLoc Current = Context.getFullLoc(Loc);

  unsigned begin = UnpackMacroLoc(Range.getBegin(), CI).getRawEncoding();
  unsigned end = UnpackMacroLoc(Range.getEnd(), CI).getRawEncoding();
  unsigned current = Loc.getRawEncoding();

  return (begin <= current) && (current <= end);
//  return (Begin.isBeforeInTranslationUnitThan(Current))
//         && (Current.isBeforeInTranslationUnitThan(End));

}

bool IsChild(Stmt * Item, Stmt * Parent) {

  if (Item == Parent) {
    return true;
  }

  if (!Parent) {
    return false;
  }

  Stmt::child_iterator It;

  for (It = Parent->child_begin(); It != Parent->child_end(); It++) {

    if (IsChild(Item, *It)) {
      return true;
    }

  }

  return false;

}

bool IsPointerArrayStructUnionType(Expr * E) {
  return IsPointerOrArrayType(E) || IsStructOrUnionType(E);
}

bool IsPointerOrArrayType(Expr * E) {
  return IsPointerType(E) || IsArrayType(E);
}

bool IsStructOrUnionType(Expr * E) {
  return IsStructType(E) || IsUnionType(E);
}

bool IsValueType(Expr * E) {
  return !IsPointerOrArrayType(E);
}

bool IsPointerType(Expr * E) {

  QualType QT = E->getType();
  const Type * T = QT.getTypePtr();
  
  return T->isPointerType();

}

bool IsArrayType(Expr * E) {

  QualType QT = E->getType();
  const Type * T = QT.getTypePtr();
  
  return T->isArrayType();

}

bool IsStructType(Expr * E) {

  QualType QT = E->getType();
  const Type * T = QT.getTypePtr();

  return T->isStructureType();

}

bool IsUnionType(Expr * E) {

  QualType QT = E->getType();
  const Type * T = QT.getTypePtr();

  return T->isUnionType();

}

bool IsWrite(BinaryOperator * Next, Expr * Var, CompilerInstance &CI) {

  return IsChild(Var, Next->getLHS());

}

bool IsArrayIndex(ArraySubscriptExpr * Next,
                  Expr * Current,
                  CompilerInstance &CI) {

  return IsChild(Current, Next->getRHS());

}

Expr * GetRelevantParent(Expr * Current, ParentMap * PM) {

  Expr * Next = Current;
  
  while(PM->hasParent(Next) && (Next = dyn_cast<Expr>(PM->getParent(Next)))) {

    switch(Next->getStmtClass()) {

      case Stmt::BinaryOperatorClass:
      case Stmt::CompoundAssignOperatorClass:
      case Stmt::ArraySubscriptExprClass:
      case Stmt::UnaryOperatorClass:
      case Stmt::MemberExprClass:
        return Next;
      case Stmt::CallExprClass:
        break;
      case Stmt::ConditionalOperatorClass:
        llvm::errs() << "Warning: Unchecked Ternary\n";
        break;
      default:
        break;
    }
    
  }
  
  return NULL;

}

string GetType(const Type * T) {
  return QualType::getAsString(T, Qualifiers());
}

string GetType(Expr * E) {

  MemberExpr * ME = dyn_cast<MemberExpr>(E);
  string TypeString;

  if (ME) {

    const Type * ParentType = ME->getBase()->getType()->getUnqualifiedDesugaredType();

    if (ME->isArrow()) {
      ParentType = ParentType->getPointeeType()->getUnqualifiedDesugaredType();
    }

    stringstream TypeStream;

    TypeStream << GetType(ParentType) << "." << string(ME->getMemberDecl()->getName())
               << ":";

    TypeString = TypeStream.str();

  }
  return TypeString;

}

string GetStmtString(Stmt * Current, CompilerInstance &CI) {

    string SStr;
    llvm::raw_string_ostream S(SStr);

    NoEditStmtPrinter P(S, 0, PrintingPolicy(CI.getLangOpts()), 0);
    P.Visit(const_cast<Stmt*>(cast<Stmt>(Current)));

    return S.str();

}


} // End namespace tools

} // End namespace speculation

