//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _DECLTRACKER_H_
#define _DECLTRACKER_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/Frontend/CompilerInstance.h"

using clang::CallExpr;
using clang::CompilerInstance;
using clang::CompoundStmt;
using clang::Decl;
using clang::DeclRefExpr;
using clang::FunctionDecl;
using clang::NamedDecl;
using clang::RecordDecl;
using clang::SourceLocation;
using clang::SourceRange;
using clang::Stmt;
using clang::Type;
using clang::VarDecl;

namespace speculation {

typedef set<NamedDecl *> DeclSet;
typedef map<string, DeclSet> SharedTypeMap;
typedef map<NamedDecl *, SharedTypeMap> SharedDeclMap;

struct FunctionTracker {
  CompilerInstance *CI;
  FunctionDecl *TheFunction;
  SharedDeclMap TrackedDecls;
};

struct FunctionCallTracker {
  FunctionTracker * Parent;
  CallExpr *TheCall;
  FunctionDecl *TheFunction;
  map<NamedDecl *, NamedDecl *> ParamTranslations;
};

class DeclTracker {

 private:

  SharedDeclMap GlobalDecls;
  map<CallExpr *, FunctionCallTracker *> AllCalls;
  map<FunctionDecl *, FunctionTracker *> AllFunctions;

  bool Changed;

  void TrackDecl(VarDecl * TheDecl, SharedDeclMap &TrackedDecls);
  void TrackStructDecl(string BaseType, const RecordDecl * RD, SharedTypeMap &Types, VarDecl * VD);

  FunctionTracker * CreateFunction(FunctionDecl * TheDecl, CompilerInstance *CI);
  FunctionCallTracker * CreateFunctionCall(CallExpr *TheCall,
                                           FunctionDecl * TheFunction,
                                           FunctionTracker * FT);

  bool SharePointers(NamedDecl * VDLHS,
                     SharedTypeMap &TLHS,
                     string LStub,
                     NamedDecl * VDRHS,
                     SharedTypeMap &TRHS,
                     string RStub,
                     const Type * T,
                     bool IncFirst = false);

  void PropogateShares(SharedDeclMap &M1, SharedDeclMap &M2);

  void resetChanged();
  bool getChanged();

  SharedTypeMap &GetTypeMap(NamedDecl * D, FunctionTracker * FT);


 public:

  DeclTracker();

  FunctionTracker * GetTracker(FunctionDecl * F);

  // Stage 1
  void AddGlobalDecl(VarDecl * D);
  void AddFunction(FunctionDecl * FD);

  // Stage 2
  void AddLocalDecl(VarDecl * D, FunctionTracker * F);
  void AddCall(CallExpr * C, FunctionTracker *F);

  bool SharePointers(NamedDecl * VDLHS,
                     string LStub,
                     NamedDecl * VDRHS,
                     string RStub,
                     const Type * T,
                     FunctionTracker * LF,
                     FunctionTracker * RF);

  void PropogateShares();

  void printGlobalTracker();
  void printAllFunctionsTrackers();
  void printFunctionTracker(FunctionTracker * I);
  void printSharedDeclMap(SharedDeclMap &Decls);

  bool ContainsMatch(NamedDecl * D,
                     FunctionTracker * F,
                     set<NamedDecl *> &Accesses);

};

} // End namespace speculation

#endif
