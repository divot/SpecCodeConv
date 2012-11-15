//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "DeclTracker.h"

#include "Globals.h"
#include "Tools.h"
#include "VarTraverser.h"

using speculation::tools::GetType;
using speculation::tools::GetLocation;

using clang::DiagnosticsEngine;
using clang::FieldDecl;
using clang::StringRef;

using llvm::dyn_cast;

namespace speculation {

// Public
DeclTracker::DeclTracker()
  : GlobalDecls(),
    AllCalls(),
    AllFunctions(),
    Changed(false) {

}

// Private
void DeclTracker::TrackDecl(VarDecl * VD, SharedDeclMap &TrackedDecls) {

    assert(VD);

    // Do we really need to check that this decl hasn't been added before?
    SharedDeclMap::iterator TrackIt = TrackedDecls.find(VD);

    if (TrackIt == TrackedDecls.end()) {
      TrackIt = TrackedDecls.insert(make_pair(VD, SharedTypeMap())).first;
    }

    SharedTypeMap &Types = TrackIt->second;

    // Loop over this decl's type and its derivate types
    // Adding them as private
    const Type * T = VD->getType()->getUnqualifiedDesugaredType();

    do {

      // Do we really need to check that this type hasn't been added before?
      SharedTypeMap::iterator TypeIt = Types.find(GetType(T));

      if (TypeIt == Types.end()) {
        TypeIt = Types.insert(make_pair(GetType(T), DeclSet())).first;
        TypeIt->second.insert(VD);
      }

      if (T->isPointerType()) {
        T = T->getPointeeType()->getUnqualifiedDesugaredType();
      } else if (T->isArrayType()) {
        T = T->getAsArrayTypeUnsafe()->getElementType()->getUnqualifiedDesugaredType();
      } else {

        if (T->isStructureType()) {

          TrackStructDecl(GetType(T),
                          T->getAsStructureType()->getDecl(),
                          Types,
                          VD);

        } else if (T->isUnionType()) {

          TrackStructDecl(GetType(T),
                          T->getAsUnionType()->getDecl(),
                          Types,
                          VD);

        }

        T = NULL;

      }

    } while (T);

}

void DeclTracker::TrackStructDecl(string BaseType,
                                  const RecordDecl * RD,
                                  SharedTypeMap &Types,
                                  VarDecl * VD) {

  RecordDecl::field_iterator FieldIt;

  for (FieldIt = RD->field_begin(); FieldIt != RD->field_end(); FieldIt++) {

    FieldDecl * FD = *FieldIt;

    if (!FD) {
      return;
    }

    // Loop over this decl's type and its derivate types
    // Adding them as private
    const Type * T = FD->getType()->getUnqualifiedDesugaredType();

    do {

      if (StringRef(BaseType).find(StringRef(GetType(T))) != StringRef::npos) {
        break;
      }

      stringstream TypeString;
      TypeString << BaseType << "." << FD->getName().str() << ":"
		 << GetType(T);

      // Do we really need to check that this type hasn't been added before?
      SharedTypeMap::iterator TypeIt = Types.find(TypeString.str());

      if (TypeIt == Types.end()) {
        TypeIt = Types.insert(make_pair(string(TypeString.str()), DeclSet())).first;
        TypeIt->second.insert(VD);
      }

      if (T->isPointerType()) {
        T = T->getPointeeType()->getUnqualifiedDesugaredType();
      } else if (T->isArrayType()) {
        T = T->getAsArrayTypeUnsafe()->getElementType()->getUnqualifiedDesugaredType();
      } else {

        if (T->isStructureType()) {
          stringstream TypeString;
          TypeString << BaseType << "." << FD->getName().str() << ":"
                     << GetType(T);
          TrackStructDecl(string(TypeString.str()), T->getAsStructureType()->getDecl(), Types, VD);
        } else if (T->isUnionType()) {
          stringstream TypeString;
          TypeString << BaseType << "." << FD->getName().str() << ":"
                     << GetType(T);
          TrackStructDecl(string(TypeString.str()), T->getAsUnionType()->getDecl(), Types, VD);
        }

        T = NULL;

      }

    } while (T);

  }

}

// Private
FunctionTracker * DeclTracker::CreateFunction(FunctionDecl * TheFunction,
                                              CompilerInstance *CI) {

  FunctionTracker *F = new FunctionTracker;

  F->TheFunction = TheFunction;
  F->CI = CI;

  FunctionDecl::param_iterator ParamIt;

  for (ParamIt = TheFunction->param_begin();
       ParamIt != TheFunction->param_end();
       ParamIt++) {
    TrackDecl(*ParamIt, F->TrackedDecls);
  }

  DiagnosticsEngine &Diags = CI->getDiagnostics();

  unsigned DiagID =
      Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                            "Created Function '%0'");
  Diags.Report(TheFunction->getLocStart(), DiagID) << TheFunction->getName();

  return F;

}

// Private
FunctionCallTracker * DeclTracker::CreateFunctionCall(CallExpr * TheCall,
                                                      FunctionDecl * TheFunction,
                                                      FunctionTracker * Parent) {

  CompilerInstance &CI = *Parent->CI;

  FunctionCallTracker *F = new FunctionCallTracker;
  F->Parent = Parent;
  F->TheCall = TheCall;
  F->TheFunction = TheFunction;

  // For each of the parameters in the function call
  // Start tracking them
  unsigned i = 0;

  // First get the set of params, and link them to passed in vars
  FunctionDecl::param_iterator ParamIt;
  for (ParamIt = TheFunction->param_begin();
       ParamIt != TheFunction->param_end();
       ParamIt++) {

    VarDecl * TheDecl = *ParamIt;

    const Type * T = (TheDecl)->getType()->getUnqualifiedDesugaredType();

    if (T->isPointerType() || T->isArrayType()) {

      // Get Dominant DeclRef and its expr
      DeclRefExpr * DominantRef = NULL;
      Expr * DominantExpr = NULL;

      VarTraverser vt(TheCall->getArg(i), DominantRef, DominantExpr, CI);
      vt.TraverseStmt(TheCall->getArg(i));

      if (!DominantRef) {
        continue;
      }

      VarDecl * DominantDeclPreTrans = dyn_cast<VarDecl>(DominantRef->getDecl());

      NamedDecl * DominantDecl = globals::GetNamedDecl(DominantDeclPreTrans);

      F->ParamTranslations.insert(make_pair(TheDecl, DominantDecl));

    }

    i++;

  }

  return F;

}

// Private
SharedTypeMap &DeclTracker::GetTypeMap(NamedDecl * D, FunctionTracker * FT) {

  SharedDeclMap::iterator Dit = FT->TrackedDecls.find(D);

  if (Dit == FT->TrackedDecls.end()) {
    Dit = GlobalDecls.find(D);
    assert(Dit != GlobalDecls.end());
  }

  return Dit->second;

}

// Public
FunctionTracker * DeclTracker::GetTracker(FunctionDecl * F) {

  assert(F);

  F = globals::GetFunctionDecl(F);

  map<FunctionDecl *, FunctionTracker *>::iterator it;
  it = AllFunctions.find(F);

  if (it == AllFunctions.end()) {
    return NULL;
  }

  return it->second;

}

// Public
void DeclTracker::AddGlobalDecl(VarDecl * D) {

  assert(D);

  D = dyn_cast<VarDecl>(globals::GetNamedDecl(D));
  assert(D);

  TrackDecl(D, GlobalDecls);
  Changed = true;

}

// Public
void DeclTracker::AddFunction(FunctionDecl * FD) {

  assert(FD);

  FD = globals::GetFunctionDecl(FD);

  CompilerInstance *CI = globals::GetCompilerInstance(FD);

  if (!FD->hasBody()) {

    DiagnosticsEngine &Diags = CI->getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Function '%0' does not have a "
                              "definition, hence cannot be checked for safety");
    Diags.Report(FD->getLocStart(), DiagID) << FD->getName();

    return;

  }

  FunctionTracker * F = CreateFunction(FD, CI);
  AllFunctions.insert(make_pair(FD,F));
  Changed = true;

}

// Public
void DeclTracker::AddLocalDecl(VarDecl * D, FunctionTracker * F) {

  assert(D);
  assert(F);

  D = dyn_cast<VarDecl>(globals::GetNamedDecl(D));
  assert(D);

  TrackDecl(D, F->TrackedDecls);
  Changed = true;

}

// Public
void DeclTracker::AddCall(CallExpr * C, FunctionTracker * FT) {

  assert(C);
  assert(FT);

  FunctionDecl * F = globals::GetFunctionDecl(C->getDirectCallee());

  if (!F->hasBody()) {
    return;
  }

  FunctionCallTracker * FC = CreateFunctionCall(C, F, FT);
  AllCalls.insert(make_pair(C,FC));
  Changed = true;

}

// Public
bool DeclTracker::SharePointers(NamedDecl * VDLHS,
                                string LStub,
                                NamedDecl * VDRHS,
                                string RStub,
                                const Type * T,
                                FunctionTracker * LF,
                                FunctionTracker * RF) {

  assert(VDLHS);
  assert(VDRHS);
  assert(T);
  assert(LF);
  assert(RF);

  VDLHS = globals::GetNamedDecl(VDLHS);
  VDRHS = globals::GetNamedDecl(VDRHS);

  SharedTypeMap &TLHS = GetTypeMap(VDLHS, LF);
  SharedTypeMap &TRHS = GetTypeMap(VDRHS, RF);

  return SharePointers(VDLHS, TLHS, LStub, VDRHS, TRHS, RStub, T);

}

// Private
bool DeclTracker::SharePointers(NamedDecl * VDLHS,
                                SharedTypeMap &TLHS,
                                string LStub,
                                NamedDecl * VDRHS,
                                SharedTypeMap &TRHS,
                                string RStub,
                                const Type * InT,
                                bool IncFirst) {

  bool Updated = false;
  const Type * T;

  if (IncFirst) {
    T = InT;
  } else if (InT->isArrayType()) {
    T = InT->getAsArrayTypeUnsafe()->getUnqualifiedDesugaredType();
    T = T->getArrayElementTypeNoTypeQual()->getUnqualifiedDesugaredType();
  } else {
    assert(InT->isPointerType());
    T = InT->getPointeeType()->getUnqualifiedDesugaredType();
  }


  do {

    size_t SLoc = StringRef(LStub).find(StringRef(GetType(T)));
    if (SLoc != StringRef::npos) {
      break;
    }

    SLoc = StringRef(RStub).find(StringRef(GetType(T)));
    if (SLoc != StringRef::npos) {
      break;
    }

    // Do we really need to check that this type hasn't been added before?
    SharedTypeMap::iterator TypeItLHS = TLHS.find(LStub + GetType(T));
    SharedTypeMap::iterator TypeItRHS = TRHS.find(RStub + GetType(T));

    assert(TypeItLHS != TLHS.end());
    assert(TypeItRHS != TRHS.end());

    DeclSet::iterator Dit;

    Updated = TypeItLHS->second.insert(VDRHS).second || Updated;
    for (Dit = TypeItRHS->second.begin();
         Dit != TypeItRHS->second.end();
         Dit++) {
      Updated = TypeItLHS->second.insert(*Dit).second || Updated;
    }

    Updated = TypeItRHS->second.insert(VDLHS).second || Updated;
    for (Dit = TypeItLHS->second.begin();
         Dit != TypeItLHS->second.end();
         Dit++) {
      Updated = TypeItRHS->second.insert(*Dit).second || Updated;
    }

    if (T->isPointerType()) {
      T = T->getPointeeType()->getUnqualifiedDesugaredType();
    } else if (T->isArrayType()) {
      T = T->getAsArrayTypeUnsafe()->getElementType()->getUnqualifiedDesugaredType();
    } else {

      if (T->isStructureType() || T->isUnionType()) {

        RecordDecl * RD;

        if (T->isStructureType()) {
          RD = T->getAsStructureType()->getDecl();
        } else {
          RD = T->getAsUnionType()->getDecl();
        }

        RecordDecl::field_iterator FieldIt;

        for (FieldIt = RD->field_begin(); FieldIt != RD->field_end(); FieldIt++) {

          FieldDecl * FD = *FieldIt;

          const Type * NT = FD->getType()->getUnqualifiedDesugaredType();
          string NLStub = LStub + GetType(T) + "." + FD->getNameAsString() + ":";
          string NRStub = RStub + GetType(T) + "." + FD->getNameAsString() + ":";

          Updated = SharePointers(VDLHS,
                                  TLHS,
                                  NLStub,
                                  VDRHS,
                                  TRHS,
                                  NRStub,
                                  NT,
                                  true) || Updated;

        }


      }

      T = NULL;

    }

  } while (T);

  Changed = Changed || Updated;

  return Updated;

}

// Private
void DeclTracker::PropogateShares(SharedDeclMap &M1, SharedDeclMap &M2) {

  // Loop over each variable (x) in the first list.
  // Then loop over each variable (y) in the second list.
  // For each y loop over each type (t) it covers.
  // If y contains x at type t then
  //    copy over all shared variables for y of type t
  //    into the list of x of type t

  bool Updated = false;

  SharedDeclMap::iterator It1;

  for (It1 = M1.begin(); It1 != M1.end(); It1++) {

    NamedDecl * Current1 = It1->first;
    SharedTypeMap &T1 = It1->second;

    SharedDeclMap::iterator It2;
    for (It2 = M2.begin(); It2 != M2.end(); It2++) {

      SharedTypeMap &T2 = It2->second;
      SharedTypeMap::iterator Tit2;

      for (Tit2 = T2.begin(); Tit2 != T2.end(); Tit2++) {

        string Type2 = Tit2->first;
        DeclSet &D2 = Tit2->second;
        DeclSet::iterator Found = D2.find(Current1);
        if (Found != D2.end()) {
          // TODO: Need to update this to handle structs/unions!!!
          // Might find "int *" in one and "MyStruct.a:int *" in t'other!!
          // If it does at the moment it'll assert out, so we're at least safe
          SharedTypeMap::iterator Tit1;
          Tit1 = T1.find(Type2);
          assert(Tit1 != T1.end());

          DeclSet &D1 = Tit1->second;
          DeclSet::iterator ActualVars;
          for (ActualVars = D2.begin(); ActualVars != D2.end(); ActualVars++) {
            Updated = D1.insert(*ActualVars).second || Updated;
          }

        }

      }

    }

  }

  Changed = Changed || Updated;

}

// Public
void DeclTracker::PropogateShares() {

  do {

    resetChanged();

    PropogateShares(GlobalDecls, GlobalDecls);

    map<FunctionDecl *, FunctionTracker *>::iterator FuncIt;
    for (FuncIt = AllFunctions.begin();
         FuncIt != AllFunctions.end();
         FuncIt++) {

      PropogateShares(FuncIt->second->TrackedDecls, FuncIt->second->TrackedDecls);
      PropogateShares(GlobalDecls, FuncIt->second->TrackedDecls);
      PropogateShares(FuncIt->second->TrackedDecls, GlobalDecls);

    }

    map<CallExpr *, FunctionCallTracker *>::iterator CallIt;
    for (CallIt = AllCalls.begin();
         CallIt != AllCalls.end();
         CallIt++) {

      FunctionTracker * F1 = CallIt->second->Parent;
      FunctionTracker * F2 = GetTracker(CallIt->second->TheFunction);

      PropogateShares(F1->TrackedDecls, F2->TrackedDecls);
      PropogateShares(F2->TrackedDecls, F1->TrackedDecls);

    }

  } while(getChanged());

}

// Public
void DeclTracker::printGlobalTracker() {

  llvm::errs() << "Globals:\n";
  printSharedDeclMap(GlobalDecls);

}

// Public
void DeclTracker::printAllFunctionsTrackers() {

  printGlobalTracker();

  map<FunctionDecl *, FunctionTracker *>::iterator it;

  for (it = AllFunctions.begin(); it != AllFunctions.end(); it++) {

    if (it->first->hasBody()) {
      printFunctionTracker(it->second);
    }

  }

}

// Public
void DeclTracker::printFunctionTracker(FunctionTracker * F) {

  assert(F);

  llvm::errs() << "Function: " << F->TheFunction->getName() << ", "
               << GetLocation(F->TheFunction->getLocStart(), *(F->CI))
               << "\n";

  printSharedDeclMap(F->TrackedDecls);

}

// Public
void DeclTracker::printSharedDeclMap(SharedDeclMap &Decls) {

  SharedDeclMap::iterator DeclIt;

  for (DeclIt = Decls.begin(); DeclIt != Decls.end(); DeclIt++) {

    llvm::errs() << "\tDecl: " << DeclIt->first->getNameAsString()
                 << " (" << (int) DeclIt->first << ")\n";

    SharedTypeMap::iterator TypeIt;

    for (TypeIt = DeclIt->second.begin();
         TypeIt != DeclIt->second.end();
         TypeIt++) {

      DeclSet::iterator it;

      llvm::errs() << "\t\t" << TypeIt->first;
      llvm::errs() << ": ";

      for (it = TypeIt->second.begin(); it != TypeIt->second.end(); it++) {

        if (it != TypeIt->second.begin()) {
          llvm::errs() << ", ";
        }

        llvm::errs() << (*it)->getNameAsString() << " (" << (int) (*it) << ")";

      }

      llvm::errs() << "\n";

    }

  }

}

// Private
void DeclTracker::resetChanged() {
  Changed = false;
}

// Private
bool DeclTracker::getChanged() {
  return Changed;
}

// Public
bool DeclTracker::ContainsMatch(NamedDecl * D,
                                FunctionTracker * F,
                                set<NamedDecl *> &Accesses) {

  assert(D);
  assert(F);

  D = globals::GetNamedDecl(D);

  SharedTypeMap &T = GetTypeMap(D, F);
  SharedTypeMap::iterator Tit;

  for (Tit = T.begin();
       Tit != T.end();
       Tit++) {

    DeclSet::iterator DeclIt;

    for (DeclIt = Tit->second.begin();
         DeclIt != Tit->second.end();
         DeclIt++) {

      set<NamedDecl *>::iterator AccIt;
      AccIt = Accesses.find(*DeclIt);

      if (AccIt != Accesses.end()) {
        llvm::errs() << "\t\tFound Match: " << D->getNameAsString() << " -> " << (*AccIt)->getNameAsString() << "\n";
        return true;
      }

    }


  }

  return false;


}


} // End namespace speculation
