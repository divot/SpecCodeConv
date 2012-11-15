//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "VarCollector.h"

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

VarCollector::VarCollector() : RecursiveASTVisitor<VarCollector>(),
                               Directives(NULL),
                               FullDirectives(NULL),
                               WaitingDirective(NULL),
                               WaitingHeader(NULL),
                               MemberWarnings(),
                               CompilerInstanceStack() {

}

void VarCollector::HandleDirective(PragmaDirectiveMap *Directives,
                                   DirectiveList *FullDirectives,
                                   FullDirective *FD) {

  this->Directives = Directives;
  this->FullDirectives = FullDirectives;

  FullDirectives->ResetStack();

  FullDirectives->Push(FD->Directive, FD->Header, FD->S, *(FD->CI));

  CompilerInstanceStack.push_back(FD->CI);

  TraverseStmt(FD->S);

  CompilerInstanceStack.pop_back();

}

bool VarCollector::VisitCompoundStmt(CompoundStmt *S) {

  if (WaitingHeader) {

    CompilerInstance &CI = FullDirectives->GetCI(S->getLocStart());

    // Ignore the directive header
    if (IsChild(S, WaitingHeader)) {
      return true;
    }

    // Found a Compound Stmt after a #pragma

    FullDirectives->Push(WaitingDirective, WaitingHeader, S, CI);
    
    WaitingDirective = NULL;
    WaitingHeader = NULL;
    
    return true;
  
  } else {

    PragmaDirectiveMap::iterator it;
    
    it = Directives->find(S->getLocStart().getRawEncoding());
  
    if (it != Directives->end()) {
    
      WaitingHeader = S;
      WaitingDirective = it->second;
    
    }
    
    return true;
    
  }

}

bool VarCollector::VisitStmt(Stmt *S) {

  if (CompoundStmt::classof(S) || ForStmt::classof(S)) {
    return true;
  }

  if (WaitingHeader) {
    
    CompilerInstance &CI = FullDirectives->GetCI(S->getLocStart());

    if (IsChild(S, WaitingHeader)) {
      return true;
    }

    FullDirectives->Push(WaitingDirective, WaitingHeader, S, CI);
    
    WaitingHeader = NULL;
    WaitingDirective = NULL;
    
  }

  return true;

}

bool VarCollector::VisitForStmt(ForStmt *S) {

  if (WaitingHeader) {
  
    CompilerInstance &CI = FullDirectives->GetCI(S->getLocStart());

    FullDirectives->Push(WaitingDirective, WaitingHeader, S, CI);
    
    WaitingDirective = NULL;
    WaitingHeader = NULL;
  
  }
  
  return true;

}

bool VarCollector::VisitCallExpr(CallExpr *e) {

  FunctionCall * FC = FullDirectives->Push(e);

  if (!FC) {
    CompilerInstance &CI = *(CompilerInstanceStack.back());
    DiagnosticsEngine &Diags = CI.getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Call to function '%0' does not have a "
                              "definition, hence cannot be checked for safety");
    Diags.Report(e->getLocStart(), DiagID) << e->getDirectCallee()->getName();

  } else {
    CompilerInstanceStack.push_back(FC->CI);
    TraverseStmt(FC->S);
    CompilerInstanceStack.pop_back();
  }

  return true;

}



bool VarCollector::VisitDeclStmt(DeclStmt *S) {

  DeclGroupRef::iterator it;
  
  for (it = S->decl_begin(); it != S->decl_end(); it++) {

    VarDecl * VD = dyn_cast<VarDecl>(*it);
    assert(VD);
    FullDirectives->InsertPrivateDecl(VD);
    
    
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

bool VarCollector::VisitBinaryOperator(BinaryOperator *e)  {

  CompilerInstance &CI = FullDirectives->GetCI(e->getLocStart());

  // If it's not an assignment, no contamination can occur
  if (!e->isAssignmentOp()) {
    return true;
  }
  
  // If we're not changing a pointer, no contamination can occur
  if (!e->getLHS()->getType()->isPointerType()) {
    return true;
  }
  
  assert(!e->getLHS()->getType()->isArrayType());
  
  // Get Dominant LHS DeclRef and its expr
  DeclRefExpr * LDominantRef = NULL;
  Expr * LDominantExpr = NULL;

  VarTraverser lvt(e->getLHS(), LDominantRef, LDominantExpr, CI);
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

  VarTraverser rvt(e->getRHS(), RDominantRef, RDominantExpr, CI);
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

  maybeContaminated(VDLHS, LT, VDRHS, RT, T, e->getLocStart());

  return true;

}

void VarCollector::HandleArrayInit(VarDecl *VDLHS,
                                   string TLHS,
                                   SourceLocation StmtLoc) {

  InitListExpr * Inits = dyn_cast<InitListExpr>(VDLHS->getInit());
  
  if (!Inits) {
    llvm::errs() << "Warning: Expected to find an InitListExpr but didn't!\n";
    return;
  }

  HandleArrayInitList(VDLHS, TLHS, Inits, StmtLoc);
  
}

void VarCollector::HandleArrayInitList(VarDecl *VDLHS,
                                       string TLHS,
                                       InitListExpr * Inits,
                                       SourceLocation StmtLoc) {

  CompilerInstance &CI = FullDirectives->GetCI(Inits->getLocStart());


  for (unsigned i = 0; i < Inits->getNumInits(); i++) {
  
    Expr * Current = Inits->getInit(i);
  
    InitListExpr * InitList = dyn_cast<InitListExpr>(Current);
    
    if (InitList) {
    
      HandleArrayInitList(VDLHS, TLHS, InitList, StmtLoc);
      
    } else {

      // Get Dominant RHS DeclRef and its expr
      DeclRefExpr * RDominantRef = NULL;
      Expr * RDominantExpr = NULL;

      VarTraverser rvt(Current, RDominantRef, RDominantExpr, CI);
      rvt.TraverseStmt(Current);

      // If there isn't a dominant variable now being pointed to, just return
      if (!RDominantRef) {
        continue;
      }

      // DEBUG:
      // Print out the dominant LHS and RHS
      
/*      llvm::errs() << "Found init of a pointer:\n";
      llvm::errs() << "RHS Ref " << RDominantRef->getDecl()->getName()
                   << " -> " << GetStmtString(RDominantExpr)
                   << " ("
                   << RDominantExpr->getType().getAsString()
                   << ")\n";*/

      VarDecl * VDRHS = dyn_cast<VarDecl>(RDominantRef->getDecl());
      string TRHS = GetType(RDominantExpr);

      const Type * T = RDominantExpr->getType()->getUnqualifiedDesugaredType();

      maybeContaminated(VDLHS, TLHS, VDRHS, TRHS, T, StmtLoc);
    
    }
  
  }

}

void VarCollector::HandlePointerInit(VarDecl *VDLHS,
                                     string TLHS,
                                     SourceLocation StmtLoc) {

  CompilerInstance &CI = FullDirectives->GetCI(StmtLoc);

  // Get Dominant RHS DeclRef and its expr
  DeclRefExpr * RDominantRef = NULL;
  Expr * RDominantExpr = NULL;

  VarTraverser rvt(VDLHS->getInit(), RDominantRef, RDominantExpr, CI);
  rvt.TraverseStmt(VDLHS->getInit());

  // If there isn't a dominant variable now being pointed to, just return
  if (!RDominantRef) {
    return;
  }

  // DEBUG:
  // Print out the dominant LHS and RHS
  
  /*llvm::errs() << "Found init of a pointer:\n";
  llvm::errs() << "RHS Ref " << RDominantRef->getDecl()->getName()
               << " -> " << GetStmtString(RDominantExpr)
               << " ("
               << RDominantExpr->getType().getAsString()
               << ")\n";*/

  VarDecl * VDRHS = dyn_cast<VarDecl>(RDominantRef->getDecl());

  string TRHS = GetType(RDominantExpr);

  const Type * T = RDominantExpr->getType()->getUnqualifiedDesugaredType();

  maybeContaminated(VDLHS, TLHS, VDRHS, TRHS, T, StmtLoc);

}


string VarCollector::GetStmtString(Stmt * S) {

    string SStr;
    llvm::raw_string_ostream Stream(SStr);

    CompilerInstance &CI = FullDirectives->GetCI(S->getLocStart());

    StmtPrinter P(Stream, 0, PrintingPolicy(CI.getLangOpts()), 0);
    P.Visit(const_cast<Stmt*>(cast<Stmt>(S)));

    return Stream.str();

}

void VarCollector::maybeContaminated(VarDecl *VDLHS,
                                     string TLHS,
                                     VarDecl *VDRHS,
                                     string TRHS,
                                     const Type * T,
                                     SourceLocation StmtLoc) {

  llvm::errs() << "MaybeContamined\n";
  if (FullDirectives->ContaminateDecl(VDLHS, TLHS, VDRHS, TRHS, T)) {

    CompilerInstance &CI = FullDirectives->GetCI(StmtLoc);
    DiagnosticsEngine &Diags = CI.getDiagnostics();

    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "Contamination Occurred above type '%0' and '%1'");
    Diags.Report(StmtLoc, DiagID) << TLHS + GetType(T) << TRHS + GetType(T);

    llvm::errs() << "LHS: " << VDLHS->getNameAsString() << "\n";
    FullDirectives->printStack(VDLHS);
    llvm::errs() << "RHS: " << VDRHS->getNameAsString() << "\n";
    FullDirectives->printStack(VDRHS);

  }

}


} // End namespace speculation

