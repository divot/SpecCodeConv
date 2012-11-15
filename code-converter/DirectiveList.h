//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _DIRECTIVELIST_H_
#define _DIRECTIVELIST_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/Frontend/CompilerInstance.h"

using clang::CallExpr;
using clang::CompilerInstance;
using clang::CompoundStmt;
using clang::Decl;
using clang::DeclRefExpr;
using clang::DiagnosticsEngine;
using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::RecordDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::Stmt;
using clang::Type;
using clang::NamedDecl;
using clang::VarDecl;

namespace speculation {

typedef map<string, bool> TypeMap;
typedef map<NamedDecl *, TypeMap> DeclMap;

struct StackItem {
  enum StackItemType {
    FullDirectiveType, FunctionCallType, SpeculativeFunctionType
  };
  StackItemType TYPE;
  SourceRange ChildRange;
  Stmt *S;
  CompilerInstance *CI;
  DeclMap TrackedDecls;
  set<NamedDecl *> ReadDecls;
  set<NamedDecl *> WriteDecls;
  set<NamedDecl *> ReadOnlyDecls;
  StackItem * Parent;
  int CachesRequired;
 protected:
  StackItem(StackItemType TYPE) { this->TYPE = TYPE; CachesRequired = -1; }
};

struct FullDirective : public StackItem {
  PragmaDirective *Directive;
  CompoundStmt *Header;
  FullDirective() : StackItem(FullDirectiveType) {}
  static bool ClassOf(StackItem *Item) {
    return Item->TYPE == FullDirectiveType;
  }
};

struct FunctionCall : public StackItem {
  CallExpr *TheCall;
  FunctionDecl *TheFunction;
  map<NamedDecl *, NamedDecl *> ParamTranslations;
  FunctionCall() : StackItem(FunctionCallType) {}
  static bool ClassOf(StackItem *Item) {
    return Item->TYPE == FunctionCallType;
  }
};

struct SpeculativeFunction : public StackItem {
  FunctionDecl *TheFunction;
  SpeculativeFunction() : StackItem(SpeculativeFunctionType) {}
  static bool ClassOf(StackItem *Item) {
    return Item->TYPE == SpeculativeFunctionType;
  }
};

class DirectiveList {

 private:
 
  DeclTracker * TrackedVars;
  map<PragmaDirective *, FullDirective *> AllDirectives;
  map<CallExpr *, FunctionCall *> AllCalls;
  map<FunctionDecl *, SpeculativeFunction *> AllSpeculativeFunctions;
  list<FullDirective *> TopLevelDirectives;
  list<StackItem *> CurrentDirectives;

  bool Changed;
  
  void TrackDecl(VarDecl * TheDecl, DeclMap &TrackedDecls);
  void TrackStructDecl(string BaseType, const RecordDecl * RD, TypeMap &Types);
  
  FullDirective * CreateFullDirective(PragmaDirective *Directive,
                                      CompoundStmt *Header,
                                      Stmt *S,
                                      CompilerInstance &CI,
                                      StackItem * Parent);
  
  FunctionCall * CreateFunction(CallExpr *TheCall,
                                FunctionDecl *TheFunction,
                                CompilerInstance *CI,
                                StackItem * Parent);

  SpeculativeFunction * CreateSpecFunction(FunctionCall *TheCall);
  void ContaminateSpecFunction(SpeculativeFunction *TheFunction,
                               FunctionCall *TheCall);
              
  void RemoveToParent(SourceLocation Loc);
  
  bool FinishedSearchingStack(StackItem * Item, NamedDecl *& TheDecl);

  bool ContaminateAll(TypeMap &Types, string TS, const Type * T, bool IncFirst = false);
  bool ContaminateSwap(TypeMap &TypesLHS,
                       string TLHS,
                       TypeMap &TypesRHS,
                       string TRHS,
                       const Type * T,
                       bool IncFirst = false);

  int getMaxCachesRequired(StackItem * SI);

  bool IsReadOnly(NamedDecl *D,
                  FunctionTracker *FT,
                  StackItem *Item,
                  DeclTracker * TrackedVars);

  set<FullDirective *> GetTopLevelDirectives(SpeculativeFunction * Item);

  void GenerateReadOnly(FullDirective * Item,
                        FunctionDecl * Parent);
  void GenerateReadOnly(SpeculativeFunction * Item);


 public:
 
  DirectiveList(DeclTracker * TrackedVars);
  
  void CreateTopLevel(PragmaDirective *Directive,
					  CompoundStmt *Header,
					  Stmt *S,
					  CompilerInstance &CI);
  
  FullDirective * Push(PragmaDirective *Directive,
                       CompoundStmt *Header,
                       Stmt *S,
                       CompilerInstance &CI);
  
  FunctionCall * Push(CallExpr *TheExpr);
  SpeculativeFunction * Push(SpeculativeFunction *TheFunc);
  
  list<FullDirective *> GetTopLevelDirectives();
  bool InsideTopLevel(Stmt * S);
  list<StackItem *> GetHandlerStartPoints();
            
  bool IsPrivate(DeclRefExpr * Current,
                 string TS,
                 const Type * T,
                 SourceLocation Loc);

  bool IsCompletelyPrivate(DeclRefExpr * Current, SourceLocation Loc);

  void InsertPrivateDecl(VarDecl * D);
  
  bool IsLocDirectlyAfterPragma(SourceLocation Loc);
  
  CompoundStmt * GetHeader(SourceLocation Loc);

  bool ContaminateDecl(NamedDecl * VDLHS,
                       string TLHS,
                       NamedDecl * VDRHS,
                       string TRHS,
                       const Type * T);
  
  void GenerateSpecFunctions();

  void ResetChangedStatus();
  bool GetChangedStatus();
  void ResetStack();
  
  CompilerInstance &GetCI(SourceLocation Loc);

  void printStack(NamedDecl * VD);
  void printFullStack();
  void printStackItem(StackItem * D);
  void printDeclMap(DeclMap &Decls);

  void printTopLevelDeclAccess();
  void printDeclAccess(StackItem * D);

  void InsertDeclAccess(NamedDecl * D, bool Write);


  map<PragmaDirective *, FullDirective *> getAllDirectives();
  map<FunctionDecl *, SpeculativeFunction *> getAllSpeculativeFunctions();

  int getMaxCachesRequired();

  void GenerateReadOnly();

  bool IsReadOnly(NamedDecl *D);

};

} // End namespace speculation

#endif
