//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"

using clang::CompilerInstance;
using clang::Decl;
using clang::FunctionDecl;
using clang::NamedDecl;
using clang::Rewriter;
using clang::SourceLocation;
using clang::VarDecl;

namespace speculation {

namespace globals {

void InsertVarDecl(VarDecl * TheDecl, CompilerInstance &CI);
void LinkExternDecls();
NamedDecl * GetNamedDecl(NamedDecl * TheDecl);
set<NamedDecl *> GetAllNamedDecls();

NamedDecl * InsertThreadPrivate(string Name, CompilerInstance &CI);
bool IsThreadPrivate(NamedDecl * TheDecl);
set<NamedDecl *> getThreadPrivate();

void InsertFunctionDecl(FunctionDecl * TheDecl, CompilerInstance &CI);
void LinkExternFunctions();
FunctionDecl * GetFunctionDecl(FunctionDecl * TheDecl);
set<FunctionDecl *> GetAllFunctionDecls();

CompilerInstance *GetCompilerInstance(NamedDecl * TheDecl);

void RegisterCompilerInstance(CompilerInstance &CI);
Rewriter &GetRewriter(CompilerInstance &CI);

} // End namespace globals

} // End namespace speculation

#endif
