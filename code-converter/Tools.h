//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef TOOLS_H_
#define TOOLS_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/AST/ParentMap.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"

using clang::ASTContext;
using clang::Preprocessor;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::Stmt;
using clang::ParentMap;
using clang::Expr;
using clang::BinaryOperator;
using clang::ArraySubscriptExpr;
using clang::CompilerInstance;
using clang::Type;
using clang::VarDecl;

namespace speculation {

namespace tools {

SourceRange GetChildRange(Stmt *S, CompilerInstance &CI);

SourceLocation GetNearestValidLoc(Stmt * S, SourceManager &SM, ParentMap *PM);
SourceLocation UnpackMacroLoc(SourceLocation Loc, SourceManager &SM);
SourceLocation UnpackMacroLoc(SourceLocation Loc, CompilerInstance &CI);

string GetLocation(SourceLocation Loc, SourceManager &SM);
string GetLocation(SourceLocation Loc, CompilerInstance &CI);
string GetLocation(SourceLocation Loc, ASTContext &Context);
string GetLocation(SourceLocation Loc, Preprocessor &PP);

SourceLocation FindLocationAfterSemi(SourceLocation loc, ASTContext &Context);

SourceLocation FindSemiAfterLocation(SourceLocation loc, ASTContext &Context);

bool InsideRange(SourceLocation Loc, SourceRange Range, CompilerInstance &CI);
bool IsChild(Stmt * Item, Stmt * Parent);

bool IsPointerArrayStructUnionType(Expr * E);
bool IsPointerOrArrayType(Expr * E);
bool IsStructOrUnionType(Expr * E);
bool IsValueType(Expr * E);
bool IsPointerType(Expr * E);
bool IsArrayType(Expr * E);
bool IsStructType(Expr * E);
bool IsUnionType(Expr * E);

bool IsWrite(BinaryOperator * Next, Expr * Var, CompilerInstance &CI);

bool IsArrayIndex(ArraySubscriptExpr * Next,
                  Expr * Current,
                  CompilerInstance &CI);

Expr * GetRelevantParent(Expr * Current, ParentMap * PM);
string GetType(const Type * T);
string GetType(Expr * E);
string GetStmtString(Stmt * Current, CompilerInstance &CI);

} // End namespace tools

} // End namespace speculation

#endif
