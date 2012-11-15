//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "Globals.h"

#include "Tools.h"

#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"

using clang::ASTContext;
using clang::DiagnosticsEngine;
using clang::FileEntry;
using clang::FileID;
using clang::Type;
using clang::Qualifiers;
using clang::QualType;
using clang::SourceManager;

using speculation::tools::GetLocation;

namespace speculation {

namespace globals {

static map<NamedDecl *, CompilerInstance *> CITranslationTable;

static set<NamedDecl *> NamedDecls;
static set<NamedDecl *> ExternNamedDecls;
static map<NamedDecl *, NamedDecl *> NamedDeclTranslationTable;
static set<NamedDecl *> ThreadPrivateDecls;

void InsertVarDecl(VarDecl * TheDecl,
                   CompilerInstance &CI) {

  {
    DiagnosticsEngine &Diags = CI.getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Inserting Global Decl '%0' (%1)");
    Diags.Report(TheDecl->getLocStart(), DiagID) << TheDecl->getName()
                                                 << (int) TheDecl;
  }

  CITranslationTable.insert(make_pair(TheDecl, &CI));

  if (TheDecl->isExternC()) {
    ExternNamedDecls.insert(TheDecl);
  } else {
    NamedDecls.insert(TheDecl);
  }
  
}

void LinkExternDecls() {

  set<NamedDecl *>::iterator ExternIt;
  for (ExternIt = ExternNamedDecls.begin();
       ExternIt != ExternNamedDecls.end();
       ExternIt++) {

    set<NamedDecl *>::iterator VarIt;
    for (VarIt = NamedDecls.begin();
         VarIt != NamedDecls.end();
         VarIt++) {

      if ((*ExternIt)->getName() == (*VarIt)->getName()) {
        break;
      }

    }

    if(VarIt == NamedDecls.end()) {

      DiagnosticsEngine &Diags =
              GetCompilerInstance(*ExternIt)->getDiagnostics();

      unsigned DiagID =
          Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                "Globals: Couldn't find extern var "
                                "'%0'");

      Diags.Report(DiagID) << (*ExternIt)->getNameAsString();

      VarIt = NamedDecls.insert(*ExternIt).first;

    }

    NamedDeclTranslationTable.insert(make_pair(*ExternIt, *VarIt));

  }

}
            
NamedDecl * GetNamedDecl(NamedDecl * TheDecl) {

  map<NamedDecl *, NamedDecl *>::iterator it;
  
  it = NamedDeclTranslationTable.find(TheDecl);
  
  if (it != NamedDeclTranslationTable.end()) {
    return it->second;
  }
  
  return TheDecl;

}

set<NamedDecl *> GetAllNamedDecls() {
  return NamedDecls;
}

NamedDecl * InsertThreadPrivate(string Name, CompilerInstance &CI) {

  set<NamedDecl *>::iterator it;
  
  for (it = NamedDecls.begin();
       it != NamedDecls.end();
       it++) {

    NamedDecl * CurrentDecl = *it;
    
    if (CurrentDecl->getNameAsString() == Name) {
      ThreadPrivateDecls.insert(CurrentDecl);

      {
        DiagnosticsEngine &Diags = CI.getDiagnostics();

        unsigned DiagID =
            Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                  "Inserting ThreadPrivate Decl '%0' (%1)");
        Diags.Report(CurrentDecl->getLocStart(), DiagID)
                    << CurrentDecl->getName()
                    << (int) CurrentDecl;
      }

      return CurrentDecl;
    }

  }
  
  DiagnosticsEngine &Diags = CI.getDiagnostics();

  unsigned DiagID = 
      Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                            "Globals: Couldn't find threadprivate var "
                            "'%0'");
  Diags.Report(DiagID) << Name;
  
  return NULL;

}

bool IsThreadPrivate(NamedDecl * TheDecl) {

  set<NamedDecl *>::iterator it;
  for (it = ThreadPrivateDecls.begin(); it != ThreadPrivateDecls.end(); it++) {
    if (*it == TheDecl) {
      break;
    }
  }
  
  return it != ThreadPrivateDecls.end();

}

set<NamedDecl *> getThreadPrivate() {
  return ThreadPrivateDecls;
}

static set<FunctionDecl *> FilteredFunctions;
static set<FunctionDecl *> DefinedFunctions;
static set<FunctionDecl *> UndefinedFunctions;
static map<FunctionDecl *, FunctionDecl *> FunctionTranslationTable;

// Inserts a local function declaration, in preparation for building a lookup
// table for functions across multiple files
void InsertFunctionDecl(FunctionDecl * TheDecl,
                        CompilerInstance &CI) {

  {
    DiagnosticsEngine &Diags = CI.getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Inserting Function Decl '%0' (%1)");
    Diags.Report(TheDecl->getLocStart(), DiagID) << TheDecl->getName()
                                                 << (int) TheDecl;
  }

  // Insert the CompilerInstance translation
  CITranslationTable.insert(make_pair(TheDecl, &CI));

  // Insert the CompilerInstance translation for every parameter
  FunctionDecl::param_iterator ParamIt;

  for (ParamIt = TheDecl->param_begin();
       ParamIt != TheDecl->param_end();
       ParamIt++) {
    CITranslationTable.insert(make_pair(*ParamIt, &CI));
  }

  if (TheDecl->isDefined()) {
    DefinedFunctions.insert(TheDecl);
  } else {
    UndefinedFunctions.insert(TheDecl);
  }

}

// Generates the translation table for functiondecl -> implementation
void LinkExternFunctions() {

  set<FunctionDecl *>::iterator FuncIt;
  for (FuncIt = DefinedFunctions.begin();
       FuncIt != DefinedFunctions.end();
       FuncIt++) {

    set<FunctionDecl *>::iterator FiltIt;
    for (FiltIt = FilteredFunctions.begin();
         FiltIt != FilteredFunctions.end();
         FiltIt++) {

      if ((*FuncIt)->getName() == (*FiltIt)->getName()) {
        break;
      }

    }

    if(FiltIt == FilteredFunctions.end()) {
      FiltIt = FilteredFunctions.insert(*FuncIt).first;
    }

    FunctionTranslationTable.insert(make_pair(*FuncIt, *FiltIt));

    FunctionDecl::param_iterator Pit, FPit;
    for (Pit = (*FuncIt)->param_begin(), FPit = (*FiltIt)->param_begin();
         Pit != (*FuncIt)->param_end() && FPit != (*FiltIt)->param_end();
         Pit++, FPit++) {

      NamedDeclTranslationTable.insert(make_pair(*Pit, *FPit));

    }

  }

  for (FuncIt = UndefinedFunctions.begin();
       FuncIt != UndefinedFunctions.end();
       FuncIt++) {

    set<FunctionDecl *>::iterator FiltIt;
    for (FiltIt = FilteredFunctions.begin();
         FiltIt != FilteredFunctions.end();
         FiltIt++) {

      if ((*FuncIt)->getName() == (*FiltIt)->getName()) {
        break;
      }

    }

    if(FiltIt == FilteredFunctions.end()) {

      DiagnosticsEngine &Diags = GetCompilerInstance(*FuncIt)->getDiagnostics();

      unsigned DiagID =
          Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                "Globals: Couldn't definition for function "
                                "'%0'");

      Diags.Report(DiagID) << (*FuncIt)->getNameAsString();

      FiltIt = FilteredFunctions.insert(*FuncIt).first;

    }

    FunctionTranslationTable.insert(make_pair(*FuncIt, *FiltIt));

    FunctionDecl::param_iterator Pit, FPit;
    for (Pit = (*FuncIt)->param_begin(), FPit = (*FiltIt)->param_begin();
         Pit != (*FuncIt)->param_end() && FPit != (*FiltIt)->param_end();
         Pit++, FPit++) {

      NamedDeclTranslationTable.insert(make_pair(*Pit, *FPit));

    }


  }

}

FunctionDecl * GetFunctionDecl(FunctionDecl * TheDecl) {

  map<FunctionDecl *, FunctionDecl *>::iterator FuncIt;
  
  FuncIt = FunctionTranslationTable.find(TheDecl);
  
  if (FuncIt != FunctionTranslationTable.end()) {
    return FuncIt->second;
  }
  
  return TheDecl;

}

set<FunctionDecl *> GetAllFunctionDecls() {
  return FilteredFunctions;
}

CompilerInstance *GetCompilerInstance(NamedDecl * TheDecl) {

  map<NamedDecl *, CompilerInstance *>::iterator DeclIt;
  
  DeclIt = CITranslationTable.find(TheDecl);
  
  if (DeclIt != CITranslationTable.end()) {
    return DeclIt->second;
  } else {
    assert(false && "Attempted to get CompilerInstance from globals, when not a global");
    return NULL;
  }

}

vector<CompilerInstance *> CIs;
map<CompilerInstance *, Rewriter *> Rewriters;

void RegisterCompilerInstance(CompilerInstance &CI) {

  CIs.push_back(&CI);

  Rewriter * rw = new Rewriter(CI.getSourceManager(), CI.getLangOpts());
  Rewriters.insert(make_pair(&CI, rw));

}

Rewriter &GetRewriter(CompilerInstance &CI) {

  map<CompilerInstance *, Rewriter *>::iterator it;
  it = Rewriters.find(&CI);

  assert(it != Rewriters.end());

  return *(it->second);

}


} // End namespace globals

} // End namespace speculation
