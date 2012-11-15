//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "DeclLinker.h"

#include "DeclTracker.h"
#include "DirectiveList.h"
#include "PragmaDirective.h"
#include "StmtPrinter.h"
#include "Tools.h"
#include "VarTraverser.h"
#include "Globals.h"

using speculation::tools::FindLocationAfterSemi;
using speculation::tools::FindSemiAfterLocation;
using speculation::tools::GetLocation;
using speculation::tools::GetRelevantParent;
using speculation::tools::GetType;
using speculation::tools::InsideRange;
using speculation::tools::IsArrayIndex;
using speculation::tools::IsPointerType;
using speculation::tools::IsWrite;
using speculation::tools::IsChild;

namespace speculation {

DeclLinker::DeclLinker(DeclTracker *TrackedVars)
                         : RecursiveASTVisitor<DeclLinker>(),
                           TrackedVars(TrackedVars),
                           CurrentFunction(NULL),
                           CI(NULL) {

  assert(TrackedVars);

}

void DeclLinker::HandleFunction(FunctionDecl * TheFunction) {

  assert(TheFunction);
  TheFunction = globals::GetFunctionDecl(TheFunction);

  CurrentFunction = TrackedVars->GetTracker(TheFunction);
  CI = globals::GetCompilerInstance(TheFunction);

  assert(TheFunction->hasBody());

  TraverseStmt(TheFunction->getBody());

}

bool DeclLinker::VisitCallExpr(CallExpr *e) {

  TrackedVars->AddCall(e, CurrentFunction);

  FunctionDecl * F = globals::GetFunctionDecl(e->getDirectCallee());

  FunctionTracker * CalledFunction = TrackedVars->GetTracker(F);
  if (!CalledFunction) {
    return true;
  }

  CallExpr::arg_iterator ArgIt;
  FunctionDecl::param_iterator ParamIt;

  for (ArgIt = e->arg_begin(), ParamIt = F->param_begin();
       ArgIt != e->arg_end() && ParamIt != F->param_end();
       ++ArgIt, ++ParamIt) {

    // Get Dominant RHS DeclRef and its expr
    DeclRefExpr * RDominantRef = NULL;
    Expr * RDominantExpr = NULL;

    VarTraverser rvt(*ArgIt, RDominantRef, RDominantExpr, *CI);
    rvt.TraverseStmt(*ArgIt);

    // If there isn't a dominant variable now being pointed to, just return
    if (!RDominantRef) {
      continue;
    }

    NamedDecl * VDLHS = globals::GetNamedDecl(*ParamIt);
    NamedDecl * VDRHS = dyn_cast<VarDecl>(RDominantRef->getDecl());

    const Type * T = RDominantExpr->getType().getTypePtr();

    string LT = "";
    string RT = GetType(RDominantExpr);

    if (TrackedVars->SharePointers(VDLHS,
                                   LT,
                                   VDRHS,
                                   RT,
                                   T,
                                   CalledFunction,
                                   CurrentFunction)) {

      DiagnosticsEngine &Diags = CI->getDiagnostics();

      unsigned DiagID =
          Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                "Swapping Across function above type '%0' and '%1'");
      Diags.Report((*ArgIt)->getLocStart(), DiagID) << LT + GetType(T) << RT + GetType(T);

    }


  }



  return true;

}

bool DeclLinker::VisitDeclStmt(DeclStmt *S) {

  DeclGroupRef::iterator it;

  for (it = S->decl_begin(); it != S->decl_end(); it++) {

    VarDecl * VD = dyn_cast<VarDecl>(*it);
    assert(VD);

    TrackedVars->AddLocalDecl(VD, CurrentFunction);

    if (!VD->hasInit()) {
      continue;
    }

    bool IsArray = false;

    const Type * T = VD->getType().getTypePtr();

    do {

      if (T->isPointerType()) {

        string TS = GetType(VD->getType()->getUnqualifiedDesugaredType());

        if (IsArray) {
          HandleArrayInit(VD, TS, S->getLocStart());
        } else {
          HandlePointerInit(VD, TS, S->getLocStart());
        }
        T = NULL;
      } else if (T->isArrayType()) {
        IsArray = true;
        T = T->getAsArrayTypeUnsafe()->getElementType().getTypePtr();
      } else {
        T = NULL;
      }

    } while (T);

  }

  return true;

}

void DeclLinker::HandleArrayInit(VarDecl *VDLHS,
                                 string TLHS,
                                 SourceLocation StmtLoc) {

  InitListExpr * Inits = dyn_cast<InitListExpr>(VDLHS->getInit());

  if (!Inits) {
    // TODO: Convert to real error
    llvm::errs() << "Warning: Expected to find an InitListExpr but didn't!\n";
    return;
  }

  HandleArrayInitList(VDLHS, TLHS, Inits, StmtLoc);

}

void DeclLinker::HandleArrayInitList(VarDecl *VDLHS,
                                     string TLHS,
                                     InitListExpr * Inits,
                                     SourceLocation StmtLoc) {

  for (unsigned i = 0; i < Inits->getNumInits(); i++) {

    Expr * Current = Inits->getInit(i);

    InitListExpr * InitList = dyn_cast<InitListExpr>(Current);

    if (InitList) {

      HandleArrayInitList(VDLHS, TLHS, InitList, StmtLoc);

    } else {

      // Get Dominant RHS DeclRef and its expr
      DeclRefExpr * RDominantRef = NULL;
      Expr * RDominantExpr = NULL;

      VarTraverser rvt(Current, RDominantRef, RDominantExpr, *CI);
      rvt.TraverseStmt(Current);

      // If there isn't a dominant variable now being pointed to, just return
      if (!RDominantRef) {
        continue;
      }

      VarDecl * VDRHS = dyn_cast<VarDecl>(RDominantRef->getDecl());
      string TRHS = GetType(RDominantExpr);

      const Type * T = RDominantExpr->getType()->getUnqualifiedDesugaredType();

      maybeSwapped(VDLHS, TLHS, VDRHS, TRHS, T, StmtLoc);

    }

  }

}

void DeclLinker::HandlePointerInit(VarDecl *VDLHS,
                                   string TLHS,
                                   SourceLocation StmtLoc) {

  // Get Dominant RHS DeclRef and its expr
  DeclRefExpr * RDominantRef = NULL;
  Expr * RDominantExpr = NULL;

  VarTraverser rvt(VDLHS->getInit(), RDominantRef, RDominantExpr, *CI);
  rvt.TraverseStmt(VDLHS->getInit());

  // If there isn't a dominant variable now being pointed to, just return
  if (!RDominantRef) {
    return;
  }

  VarDecl * VDRHS = dyn_cast<VarDecl>(RDominantRef->getDecl());

  string TRHS = GetType(RDominantExpr);

  const Type * T = RDominantExpr->getType()->getUnqualifiedDesugaredType();

  maybeSwapped(VDLHS, TLHS, VDRHS, TRHS, T, StmtLoc);

}

bool DeclLinker::VisitBinaryOperator(BinaryOperator *e)  {

  // If it's not an assignment, no sharing can occur
  if (!e->isAssignmentOp()) {
    return true;
  }

  // If we're not changing a pointer, no sharing can occur
  if (!e->getLHS()->getType()->isPointerType()) {
    return true;
  }

  assert(!e->getLHS()->getType()->isArrayType());

  // Get Dominant LHS DeclRef and its expr
  DeclRefExpr * LDominantRef = NULL;
  Expr * LDominantExpr = NULL;

  VarTraverser lvt(e->getLHS(), LDominantRef, LDominantExpr, *CI);
  lvt.TraverseStmt(e->getLHS());

  // Shouldn't get this!!
  // But if there isn't a dominant variable being written to just return
  // Where on earth is it writing to!?
  // TODO: Convert to actual error!
  if (!LDominantRef) {
    llvm::errs() << "Warning: Found a pointer being modified that we have no "
                 << "idea where came from. Can't determine if private or not!\n";
    return true;
  }

  // Get Dominant RHS DeclRef and its expr
  DeclRefExpr * RDominantRef = NULL;
  Expr * RDominantExpr = NULL;

  VarTraverser rvt(e->getRHS(), RDominantRef, RDominantExpr, *CI);
  rvt.TraverseStmt(e->getRHS());

  // If there isn't a dominant variable now being pointed to, just return
  if (!RDominantRef) {
    llvm::errs() << "No dominant right ref\n";
    return true;
  }

  VarDecl * VDLHS = dyn_cast<VarDecl>(LDominantRef->getDecl());
  VarDecl * VDRHS = dyn_cast<VarDecl>(RDominantRef->getDecl());

  const Type * T = LDominantExpr->getType().getTypePtr();

  string LT = GetType(LDominantExpr);
  string RT = GetType(RDominantExpr);

  maybeSwapped(VDLHS, LT, VDRHS, RT, T, e->getLocStart());

  return true;

}

void DeclLinker::maybeSwapped(VarDecl *VDLHS,
                              string LStub,
                              VarDecl *VDRHS,
                              string RStub,
                              const Type * T,
                              SourceLocation StmtLoc) {

  if (TrackedVars->SharePointers(VDLHS,
                                 LStub,
                                 VDRHS,
                                 RStub,
                                 T,
                                 CurrentFunction,
                                 CurrentFunction)) {

    DiagnosticsEngine &Diags = CI->getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Swapping Occurred above type '%0' and '%1'");
    Diags.Report(StmtLoc, DiagID) << LStub + GetType(T) << RStub + GetType(T);

    llvm::errs() << "LHS: " << VDLHS->getNameAsString() << "\n";
    //FullDirectives->printStack(VDLHS);
    llvm::errs() << "RHS: " << VDRHS->getNameAsString() << "\n";
    //FullDirectives->printStack(VDRHS);

  }

}


} // End namespace speculation

