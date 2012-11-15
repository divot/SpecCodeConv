//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "FakeDirectiveHandler.h"

#include "DirectiveList.h"
#include "Globals.h"
#include "NoEditStmtPrinter.h"
#include "PragmaDirective.h"
#include "Tools.h"
#include "VarTraverser.h"

using speculation::tools::GetChildRange;
using speculation::tools::GetType;
using speculation::tools::InsideRange;
using speculation::tools::FindSemiAfterLocation;
using speculation::tools::FindLocationAfterSemi;
using speculation::tools::IsPointerType;
using speculation::tools::IsWrite;
using speculation::tools::IsArrayIndex;
using speculation::tools::GetRelevantParent;
using speculation::tools::IsChild;

namespace speculation {

FakeDirectiveHandler::FakeDirectiveHandler(DirectiveList *FullDirectives)
                                : RecursiveASTVisitor<FakeDirectiveHandler>(),
                                  Directives(NULL),
                                  FullDirectives(FullDirectives),
                                  PM(NULL),
                                  files(),
                                  BracketLocs(),
                                  ReadLocs(),
                                  WriteLocs(),
                                  WaitingDirective(NULL),
                                  WaitingHeader(NULL) {

}

void FakeDirectiveHandler::SetParentMap(Stmt * s) {

  PM = new ParentMap(s);

}

void FakeDirectiveHandler::HandleStackItem(PragmaDirectiveMap *Directives,
                                       StackItem *SI) {

  FullDirectives->ResetStack();
  this->Directives = Directives;

  if (FullDirective::ClassOf(SI)) {
    FullDirective *FD = (FullDirective *) SI;
    FullDirectives->Push(FD->Directive, FD->Header, FD->S, *(FD->CI));
  } else if (SpeculativeFunction::ClassOf(SI)) {
    SpeculativeFunction *SF = (SpeculativeFunction *) SI;
    FullDirectives->Push(SF);
  } else {
    assert(false && "Attempted to handle a Call or Something Else");
  }

  SetParentMap(SI->S);
  
  TraverseStmt(SI->S);

  SourceManager &sm = SI->CI->getSourceManager();

  map<CompilerInstance *, set<FileID> >::iterator CIit;
  CIit = files.find(SI->CI);

  if (CIit == files.end()) {

    CIit = files.insert(make_pair(SI->CI,set<FileID>())).first;

  }

  // Log file as modified so requires saving/outputting
  CIit->second.insert(sm.getFileID(SI->S->getLocStart()));

}

bool FakeDirectiveHandler::VisitDeclStmt(DeclStmt *s) {

  DeclGroupRef::iterator it;
  
  for (it = s->decl_begin(); it != s->decl_end(); it++) {
    VarDecl * VD = dyn_cast<VarDecl>(*it);
    assert(VD);
    FullDirectives->InsertPrivateDecl(VD);
  }

  return true;
  
}

// TODO: Handle call expr? Still need this for reads of params etc.
bool FakeDirectiveHandler::VisitDeclRefExpr(DeclRefExpr *S) {

  if (WaitingHeader && IsChild(S, WaitingHeader)) {
    return true;
  }

  FunctionDecl * F = dyn_cast<FunctionDecl>(S->getDecl());
  
  if (F) {
    return true;
  }
  
  SourceManager &SM = FullDirectives->GetCI(S->getLocStart()).getSourceManager();

  // TODO: Replace with presumed loc stuff
  SourceLocation Loc = tools::GetNearestValidLoc(S, SM, PM);

  if (FullDirectives->IsCompletelyPrivate(S, Loc)) {
    return true;
  }

  Expr * Var = getVar(S);
  Stmt * Base = getBase(S);
  Stmt * Parent = getParent(Base);

  Stmt * Top;
  
  if (!Parent || FullDirectives->IsLocDirectlyAfterPragma(Parent->getLocStart())) {
    Top = FullDirectives->GetHeader(S->getLocStart());
  } else {
    Top = Parent;
  }

  vector<LocalStmtPair> WritePairs = GenerateWritePairs(Base, Parent, Top);
  
  WalkUpExpr(S, Var, WritePairs, true, string());
  
  return true;
  
}

bool FakeDirectiveHandler::VisitCompoundStmt(CompoundStmt *S) {

  if (WaitingHeader) {

    CompilerInstance &CI = FullDirectives->GetCI(S->getLocStart());

    if (IsChild(S, WaitingHeader)) {
      return true;
    }

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

bool FakeDirectiveHandler::VisitStmt(Stmt *S) {

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

bool FakeDirectiveHandler::VisitForStmt(ForStmt *S) {

  if (WaitingHeader) {
  
    CompilerInstance &CI = FullDirectives->GetCI(S->getLocStart());

    if (IsChild(S, WaitingHeader)) {
      return true;
    }

    FullDirectives->Push(WaitingDirective, WaitingHeader, S, CI);
    
    WaitingDirective = NULL;
    WaitingHeader = NULL;
  
  }
  
  return true;

}



Expr * FakeDirectiveHandler::getVar(Expr * Original) {

  Expr * Current = Original;
  Expr * Next;
  CompilerInstance &CI = FullDirectives->GetCI(Original->getLocStart());
  
  while((Next = dyn_cast<Expr>(PM->getParent(Current)))) {

    switch(Next->getStmtClass()) {
     case Stmt::ArraySubscriptExprClass:
      if (IsArrayIndex(dyn_cast<ArraySubscriptExpr>(Next), Current, CI)) {
        return Current;
      }
     // Fall Through
     case Stmt::ImplicitCastExprClass:
      Current = Next;
      break;
     default:
      return Current;
     
    }    
  
  }
  
  return Current;

}

Stmt * FakeDirectiveHandler::getBase(Expr * Original) {

  Expr * CurrentExpr = Original;
  Expr * NextExpr = 0;
  Stmt * BaseStmt = 0;

  while(PM->getParent(CurrentExpr) && (NextExpr = dyn_cast<Expr>(PM->getParent(CurrentExpr)))) {
    CurrentExpr = NextExpr;
  }

  if (PM->getParent(CurrentExpr)) {
    BaseStmt = dyn_cast<DeclStmt>(PM->getParent(CurrentExpr));
  
    if (!BaseStmt) {
      BaseStmt = dyn_cast<ReturnStmt>(PM->getParent(CurrentExpr));
    }
  }

  if (!BaseStmt) {
    BaseStmt = cast<Stmt>(CurrentExpr);
  }
  
  return BaseStmt;

}

Stmt * FakeDirectiveHandler::getParent(Stmt * Base) {

  // Get parent stmt:
  Stmt * Parent = Base;
  
  while((Parent = dyn_cast<clang::Stmt>(PM->getParent(Parent)))) {

    // Wanted: CompoundStmt, ForStmt, IfStmt, WhileStmt, DoStmt, SwitchStmt
    // Ignore: CaseStmt, DefaultStmt
    // Ignore But Don't Expect: BreakStmt, ContinueStmt, NullStmt
    // Ignore With Big Warnings: Every other stmt
    
    switch(Parent->getStmtClass()) {
    
     case Stmt::CompoundStmtClass:
     case Stmt::ForStmtClass:
     case Stmt::IfStmtClass:
     case Stmt::WhileStmtClass:
     case Stmt::DoStmtClass:
     case Stmt::SwitchStmtClass:
      return Parent;
     case Stmt::CaseStmtClass:
     case Stmt::DefaultStmtClass:
     case Stmt::ReturnStmtClass:
      break;
     case Stmt::BreakStmtClass:
     case Stmt::ContinueStmtClass:
     case Stmt::NullStmtClass:
     default:
      llvm::errs() << "Warning; Unexpected Parent Stmt Type\n";
    }

  }
  
  return NULL;
  
}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      Stmt * Parent,
                                                      Stmt * Top) {

  if (Parent) {
    switch(Parent->getStmtClass()) {

     case Stmt::CompoundStmtClass:
      return GenerateWritePairs(Base, dyn_cast<CompoundStmt>(Parent), Top);
     case Stmt::ForStmtClass:
      return GenerateWritePairs(Base, dyn_cast<ForStmt>(Parent), Top);
     case Stmt::IfStmtClass:
      return GenerateWritePairs(Base, dyn_cast<IfStmt>(Parent), Top);
     case Stmt::WhileStmtClass:
      return GenerateWritePairs(Base, dyn_cast<WhileStmt>(Parent), Top);
     case Stmt::DoStmtClass:
      return GenerateWritePairs(Base, dyn_cast<DoStmt>(Parent), Top);
     case Stmt::SwitchStmtClass:
      return GenerateWritePairs(Base, dyn_cast<SwitchStmt>(Parent), Top);
     default:
      llvm::errs() << "Error; Unexpected Parent Stmt Type\n";
      return vector<LocalStmtPair>();
    }
  } else {

    vector<LocalStmtPair> Pairs;
    LocalStmtPair Pair;

    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

    return Pairs;

  }

}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      CompoundStmt * Parent,
                                                      Stmt * Top) {

  vector<LocalStmtPair> Pairs;
  LocalStmtPair Pair;
  
  Pair.insertAfter = false;
  Pair.stmt = Base;
  Pairs.push_back(Pair);

  return Pairs;

}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      ForStmt * Parent,
                                                      Stmt * Top) {

  vector<LocalStmtPair> Pairs;
  LocalStmtPair Pair;
  
  // Init
  if (IsChild(Base, Parent->getInit())) {

    Pair.insertAfter = false;
    Pair.stmt = Top;
    Pairs.push_back(Pair);

  // Cond
  } else if (IsChild(Base, Parent->getCond())) {

    Pair.insertAfter = false;
    Pair.stmt = Top;
    Pairs.push_back(Pair);

    Pair.insertAfter = true;
    Pair.stmt = Parent->getBody();
    Pairs.push_back(Pair);

  // Inc
  } else if (IsChild(Base, Parent->getInc())) {

    Pair.insertAfter = true;
    Pair.stmt = Parent->getBody();
    Pairs.push_back(Pair);

  // Body
  } else {

    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

  }

  return Pairs;

}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      IfStmt * Parent,
                                                      Stmt * Top) {

  vector<LocalStmtPair> Pairs;
  LocalStmtPair Pair;
  
  // Cond
  if (IsChild(Base, Parent->getCond())) {

    Pair.insertAfter = false;
    Pair.stmt = Top;
    Pairs.push_back(Pair);

  // Body or Else
  } else {
  
    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

  }

  return Pairs;

}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      WhileStmt * Parent,
                                                      Stmt * Top) {

  vector<LocalStmtPair> Pairs;
  LocalStmtPair Pair;
  
  // Cond
  if (IsChild(Base, Parent->getCond())) {

    Pair.insertAfter = false;
    Pair.stmt = Top;
    Pairs.push_back(Pair);

    Pair.insertAfter = true;
    Pair.stmt = Parent->getBody();
    Pairs.push_back(Pair);

  // Body
  } else {

    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

  }

  return Pairs;

}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      DoStmt * Parent,
                                                      Stmt * Top) {

  vector<LocalStmtPair> Pairs;
  LocalStmtPair Pair;
  
  // Cond
  if (IsChild(Base, Parent->getCond())) {

    Pair.insertAfter = true;
    Pair.stmt = Parent->getBody();
    Pairs.push_back(Pair);

  // Body
  } else {

    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

  }

  return Pairs;

}

vector<LocalStmtPair> FakeDirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      SwitchStmt * Parent,
                                                      Stmt * Top) {

  vector<LocalStmtPair> Pairs;
  LocalStmtPair Pair;
  
  // Cond
  if (IsChild(Base, Parent->getCond())) {

    Pair.insertAfter = false;
    Pair.stmt = Top;
    Pairs.push_back(Pair);

  // Body
  } else {

    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

  }

  return Pairs;

}

void FakeDirectiveHandler::WalkUpExpr(DeclRefExpr * Original,
                                  Expr * Var,
                                  vector<LocalStmtPair> WritePairs,
                                  bool ActualVar,
                                  string Struct) {

  Expr * Next = GetRelevantParent(Var, PM);


  if (Next) {
  
    switch(Next->getStmtClass()) {
    
     case Stmt::BinaryOperatorClass:
     case Stmt::CompoundAssignOperatorClass:
      
      HandleBinaryOperator(Original, Var, Next, WritePairs, ActualVar, Struct);
      break;
      
     case Stmt::UnaryOperatorClass:

      HandleUnaryOperator(Original, Var, Next, WritePairs, ActualVar, Struct);
      break;

     case Stmt::ArraySubscriptExprClass:

      HandleArraySubscriptExpr(Original, Var, Next, WritePairs, ActualVar, Struct);
      break;
      
     case Stmt::MemberExprClass:

      HandleMemberExpr(Original, Var, Next, WritePairs, ActualVar, Struct);
      break;

     default:
      assert(false && "Error: Relevant Parent returned something unexpected!");
    }
  
  } else {
  
    // No Relevant Parent, so just a read?
    // Even if it's a pointer type, we don't do anything
    // else with it, so don't care!
    
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    
  }

}

void FakeDirectiveHandler::HandleBinaryOperator(DeclRefExpr * Original,
                                            Expr * Var,
                                            Expr * Next,
                                            vector<LocalStmtPair> WritePairs,
                                            bool ActualVar,
                                            string Struct) {

  BinaryOperator * BO = cast<BinaryOperator>(Next);
  
  CompilerInstance &CI = FullDirectives->GetCI(Original->getLocStart());

  // a = b;
  if (BO->isAssignmentOp()) {
  
    // Var = b;
    if (IsWrite(BO, Var, CI)) {
      InsertAccess(Var, Original, true, WritePairs, ActualVar, Struct);
      
      // Var += b;
      if (BO->isCompoundAssignmentOp()) {
        InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
      }
      
    // a = Var;
    } else {
      InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    }

    ActualVar = false;

  // a + b;
  } else {
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    ActualVar = false;
  }
  
  if (IsPointerType(Var) && IsPointerType(BO)) {
    WalkUpExpr(Original, BO, WritePairs, ActualVar, Struct);
  }
  
}

void FakeDirectiveHandler::HandleUnaryOperator(DeclRefExpr * Original,
                                           Expr * Var,
                                           Expr * Next,
                                           vector<LocalStmtPair> WritePairs,
                                           bool ActualVar,
                                           string Struct) {

  UnaryOperator * UO = dyn_cast<UnaryOperator>(Next);

  // Var++ etc.
  if (UO->isIncrementDecrementOp()) {

    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    InsertAccess(Var, Original, true, WritePairs, ActualVar, Struct);

  // *Var
  } else if (UO->getOpcode() == clang::UO_Deref) {

    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    ActualVar = true;
    
  } else {
  
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    ActualVar = false;
  
  }
  
  if (IsPointerType(Var)) {
    WalkUpExpr(Original, UO, WritePairs, ActualVar, Struct);
  }

}


void FakeDirectiveHandler::HandleArraySubscriptExpr(DeclRefExpr * Original,
                                                Expr * Var,
                                                Expr * Next,
                                                vector<LocalStmtPair> WritePairs,
                                                bool ActualVar,
                                                string Struct) {

  CompilerInstance &CI = FullDirectives->GetCI(Original->getLocStart());

  ArraySubscriptExpr * Array = dyn_cast<ArraySubscriptExpr>(Next);

  if (IsArrayIndex(Array, Var, CI)) {
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
  }

  if (IsPointerType(Var)) {
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
    ActualVar = true;
    WalkUpExpr(Original, Array, WritePairs, ActualVar, Struct);
  }

}

void FakeDirectiveHandler::HandleMemberExpr(DeclRefExpr * Original,
                                        Expr * Var,
                                        Expr * Next,
                                        vector<LocalStmtPair> WritePairs,
                                        bool ActualVar,
                                        string Struct) {

  MemberExpr * Member = dyn_cast<MemberExpr>(Next);

  if (Member->isArrow()) {
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
  }

  Struct = GetType(Member);

  WalkUpExpr(Original, Member, WritePairs, ActualVar, Struct);

}



void FakeDirectiveHandler::InsertAccess(Expr * Current,
                                    DeclRefExpr * Original,
                                    bool Write,
                                    vector<LocalStmtPair> WritePairs,
                                    bool ActualVar,
                                    string Struct) {

  if (!ActualVar) return;
  
  const Type * T = Current->getType().getTypePtr();
  
  if (FullDirectives->IsPrivate(Original, Struct, T, Original->getLocStart())) {
    return;
  }

  vector<LocalStmtPair>::iterator it;

  CompilerInstance &CI = FullDirectives->GetCI(Current->getLocStart());

  for (it = WritePairs.begin(); it != WritePairs.end(); it++) {
  
    bool insertAfter = it->insertAfter;
    Stmt * curStmt = it->stmt;

    bool isBracket = false;

    SourceLocation start = curStmt->getLocStart();
    SourceLocation end = FindSemiAfterLocation(curStmt->getLocEnd(),
                                               CI.getASTContext());

    if (end.isInvalid()) {
      end = curStmt->getLocEnd();
      isBracket = true;
    }
    
    SourceLocation loc;
    
    if (insertAfter) {
      if (isBracket) {
        loc = end;
      } else {
        loc = end.getLocWithOffset(1);
      }
    } else {
      loc = start;
    }
    
    loc = tools::UnpackMacroLoc(loc, CI);

    if (GetOrSetAccessed(loc, Current, Write)) {
      continue;
    }

    FullDirectives->InsertDeclAccess(Original->getFoundDecl(), Write);

  }

}

bool FakeDirectiveHandler::GetOrSetAccessed(SourceLocation Loc,
                                       Expr * Current,
                                       bool Write) {

    SourceLocationMultimap::iterator it;
    pair<SourceLocationMultimap::iterator,SourceLocationMultimap::iterator> ret;
    string CurrentStr = GetStmtString(Current);
    
    if (Write) {
      ret = WriteLocs.equal_range(Loc);
    } else {
      ret = ReadLocs.equal_range(Loc);
    }
    
    for (it = ret.first; it != ret.second; it++) {
      if (it->second == CurrentStr) {
        return true;
      }
    }
    
    if (Write) {
      WriteLocs.insert(make_pair(Loc, CurrentStr));
    } else {
      ReadLocs.insert(make_pair(Loc, CurrentStr));      
    }
    
    return false;

}

string FakeDirectiveHandler::GetStmtString(Stmt * Current) {

    string SStr;
    llvm::raw_string_ostream S(SStr);

    CompilerInstance &CI = FullDirectives->GetCI(Current->getLocStart());

    NoEditStmtPrinter P(S, 0, PrintingPolicy(CI.getLangOpts()), 0);
    P.Visit(const_cast<Stmt*>(cast<Stmt>(Current)));

    return S.str();

}

} // End namespace speculation

