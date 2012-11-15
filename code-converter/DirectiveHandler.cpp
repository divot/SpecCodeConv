//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "DirectiveHandler.h"

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

DirectiveHandler::DirectiveHandler(DirectiveList *FullDirectives)
                            : RecursiveASTVisitor<DirectiveHandler>(),
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

void DirectiveHandler::Finish() {

  map<CompilerInstance *, set<FileID> >::iterator CIit;

  for (CIit = files.begin(); CIit != files.end(); CIit++) {

    CompilerInstance &CI = *(CIit->first);
    SourceManager &sm = CI.getSourceManager();
    Rewriter &rw = globals::GetRewriter(CI);

    set<FileID> &actualFiles = CIit->second;
    set<FileID>::iterator FileIt;

    for (FileIt = actualFiles.begin(); FileIt != actualFiles.end(); FileIt++) {

      const FileEntry * fe = sm.getFileEntryForID(*FileIt);

      if (fe) {
        SourceLocation start = sm.getLocForStartOfFile(*FileIt);
        SourceLocation end = sm.getLocForEndOfFile(*FileIt);

        rw.InsertTextAfter(start, "#if defined(_OPENMP)\n");
        rw.InsertTextAfter(start, "  #include \"Spec/CPUSpec.h\"\n");
        rw.InsertTextAfter(start, "#endif\n");

        // TODO, Add StopWatches!

        llvm::errs() << "##### " << fe->getName() << " #####\n";
        llvm::errs() << rw.getRewrittenText(SourceRange(start, end));
        llvm::errs() << "\n";
      }

    }

  }

  // Backup Original Files
  for (CIit = files.begin(); CIit != files.end(); CIit++) {

    CompilerInstance &CI = *(CIit->first);
    SourceManager &sm = CI.getSourceManager();
    Rewriter &rw = globals::GetRewriter(CI);

    rw.overwriteChangedFiles();

    set<FileID> &actualFiles = CIit->second;
    set<FileID>::iterator FileIt;

    for (FileIt = actualFiles.begin(); FileIt != actualFiles.end(); FileIt++) {



    }

  }

}

void DirectiveHandler::SetParentMap(Stmt * s) {

  PM = new ParentMap(s);

}

void DirectiveHandler::HandleStackItem(PragmaDirectiveMap *Directives,
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

  InsertCacheAssignments(SI);

  SourceManager &sm = SI->CI->getSourceManager();

  map<CompilerInstance *, set<FileID> >::iterator CIit;
  CIit = files.find(SI->CI);

  if (CIit == files.end()) {

    CIit = files.insert(make_pair(SI->CI,set<FileID>())).first;

  }

  // Log file as modified so requires saving/outputting
  CIit->second.insert(sm.getFileID(SI->S->getLocStart()));

}

bool DirectiveHandler::VisitDeclStmt(DeclStmt *s) {

  DeclGroupRef::iterator it;
  
  for (it = s->decl_begin(); it != s->decl_end(); it++) {
    VarDecl * VD = dyn_cast<VarDecl>(*it);
    assert(VD);
    FullDirectives->InsertPrivateDecl(VD);
  }

  return true;
  
}

// TODO: Handle call expr? Still need this for reads of params etc.
bool DirectiveHandler::VisitDeclRefExpr(DeclRefExpr *S) {

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

  vector<StmtPair> WritePairs = GenerateWritePairs(Base, Parent, Top);
  
  WalkUpExpr(S, Var, WritePairs, true, string());
  
  return true;
  
}

bool DirectiveHandler::VisitCompoundStmt(CompoundStmt *S) {

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

bool DirectiveHandler::VisitStmt(Stmt *S) {

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

bool DirectiveHandler::VisitForStmt(ForStmt *S) {

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



Expr * DirectiveHandler::getVar(Expr * Original) {

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

Stmt * DirectiveHandler::getBase(Expr * Original) {

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

Stmt * DirectiveHandler::getParent(Stmt * Base) {

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

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base,
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
      return vector<StmtPair>();
    }
  } else {

    vector<StmtPair> Pairs;
    StmtPair Pair;

    Pair.insertAfter = false;
    Pair.stmt = Base;
    Pairs.push_back(Pair);

    return Pairs;

  }

}

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base,
                                                      CompoundStmt * Parent,
                                                      Stmt * Top) {

  vector<StmtPair> Pairs;
  StmtPair Pair;
  
  Pair.insertAfter = false;
  Pair.stmt = Base;
  Pairs.push_back(Pair);

  return Pairs;

}

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base, 
                                                      ForStmt * Parent,
                                                      Stmt * Top) {

  vector<StmtPair> Pairs;
  StmtPair Pair;
  
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

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base, 
                                                      IfStmt * Parent,
                                                      Stmt * Top) {

  vector<StmtPair> Pairs;
  StmtPair Pair;
  
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

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base, 
                                                      WhileStmt * Parent,
                                                      Stmt * Top) {

  vector<StmtPair> Pairs;
  StmtPair Pair;
  
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

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base, 
                                                      DoStmt * Parent,
                                                      Stmt * Top) {

  vector<StmtPair> Pairs;
  StmtPair Pair;
  
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

vector<StmtPair> DirectiveHandler::GenerateWritePairs(Stmt * Base, 
                                                      SwitchStmt * Parent,
                                                      Stmt * Top) {

  vector<StmtPair> Pairs;
  StmtPair Pair;
  
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

void DirectiveHandler::WalkUpExpr(DeclRefExpr * Original,
                                  Expr * Var,
                                  vector<StmtPair> WritePairs,
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

void DirectiveHandler::HandleBinaryOperator(DeclRefExpr * Original,
                                            Expr * Var,
                                            Expr * Next,
                                            vector<StmtPair> WritePairs,
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

void DirectiveHandler::HandleUnaryOperator(DeclRefExpr * Original,
                                           Expr * Var,
                                           Expr * Next,
                                           vector<StmtPair> WritePairs,
                                           bool ActualVar,
                                           string Struct) {

  UnaryOperator * UO = dyn_cast<UnaryOperator>(Next);

  // Var++
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


void DirectiveHandler::HandleArraySubscriptExpr(DeclRefExpr * Original,
                                                Expr * Var,
                                                Expr * Next,
                                                vector<StmtPair> WritePairs,
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

void DirectiveHandler::HandleMemberExpr(DeclRefExpr * Original,
                                        Expr * Var,
                                        Expr * Next,
                                        vector<StmtPair> WritePairs,
                                        bool ActualVar,
                                        string Struct) {

  MemberExpr * Member = dyn_cast<MemberExpr>(Next);

  if (Member->isArrow()) {
    llvm::errs() << "Inserting Arrow Access\n";
    InsertAccess(Var, Original, false, WritePairs, ActualVar, Struct);
  }

  Struct = GetType(Member);

  WalkUpExpr(Original, Member, WritePairs, ActualVar, Struct);

}



void DirectiveHandler::InsertAccess(Expr * Current, 
                                    DeclRefExpr * Original,
                                    bool Write,
                                    vector<StmtPair> WritePairs,
                                    bool ActualVar,
                                    string Struct) {

  if (!ActualVar) return;
  
  const Type * T = Current->getType().getTypePtr();
  
  if (FullDirectives->IsPrivate(Original, Struct, T, Original->getLocStart())) {
    return;
  }

  // Insert check to determine if a variable is read only.
  if (FullDirectives->IsReadOnly(globals::GetNamedDecl(dyn_cast<VarDecl>(Original->getFoundDecl())))) {
    return;
  }

  vector<StmtPair>::iterator it;

  CompilerInstance &CI = FullDirectives->GetCI(Current->getLocStart());

  for (it = WritePairs.begin(); it != WritePairs.end(); it++) {
  
    bool insertAfter = it->insertAfter;
    Stmt * curStmt = it->stmt;
    Stmt * cmpStmt = dyn_cast<CompoundStmt>(curStmt);
    Stmt * parStmt = PM->getParent(curStmt);
    Stmt * stmtParent = NULL;
    if (parStmt) {
      stmtParent = dyn_cast<CompoundStmt>(parStmt);
    }

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

    stringstream ss;
    ss <<  "SPEC";
    if (Write) {
      ss << "WRITE(";
    } else {
      ss << "READ(";
    }
    
    DeclRefExpr * dre = dyn_cast<DeclRefExpr>(Original);
    ss << dre->getNameInfo().getName().getAsString() << ", ";
    ss << GetStmtString(Current);

    ss << ");\n";
    
    Rewriter &rw = globals::GetRewriter(CI);

    rw.InsertText(loc, StringRef(ss.str()), !insertAfter, true);

    if (!cmpStmt && !stmtParent) {
      SourceRange range(start, end);
      InsertBrackets(range);
    }
    
  }

}

bool DirectiveHandler::GetOrSetAccessed(SourceLocation Loc,
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

string DirectiveHandler::GetStmtString(Stmt * Current) {

    string SStr;
    llvm::raw_string_ostream S(SStr);

    CompilerInstance &CI = FullDirectives->GetCI(Current->getLocStart());

    NoEditStmtPrinter P(S, 0, PrintingPolicy(CI.getLangOpts()), 0);
    P.Visit(const_cast<Stmt*>(cast<Stmt>(Current)));

    return S.str();

}

void DirectiveHandler::InsertBrackets(SourceRange StmtRange, CompilerInstance *CI) {

  SourceRangeSet::iterator it = BracketLocs.find(StmtRange);
  
  if (it != BracketLocs.end()) {
    return;
  }
  
  BracketLocs.insert(StmtRange);
  
  if (!CI) CI = &FullDirectives->GetCI(StmtRange.getBegin());
  Rewriter &rw = globals::GetRewriter(*CI);

  rw.InsertText(StmtRange.getBegin(), "{\n", false, true);
  rw.InsertText(StmtRange.getEnd().getLocWithOffset(1), "\n}\n", true, true);
  
}

// TODO: Clean all of this up! It's horrid!
void DirectiveHandler::InsertCacheAssignments(StackItem * SI) {

  switch (SI->TYPE) {

   case StackItem::FullDirectiveType:
    InsertCacheAssignments((FullDirective *) SI);
    break;

   case StackItem::SpeculativeFunctionType:
    InsertCacheAssignments((SpeculativeFunction *) SI);
    break;

   default:
    assert(false && "Unexpected Stack Item Type");
  }

}

void DirectiveHandler::InsertCacheAssignments(FullDirective * FD) {

  assert(FD->Directive->Parallel);

  Rewriter &rw = globals::GetRewriter(*(FD->CI));

  if (FD->Directive->MainConstruct.Type == ForConstruct) {

    stringstream ss;

    set<NamedDecl *>::iterator DeclIt;

    for (DeclIt = FD->ReadDecls.begin();
         DeclIt != FD->ReadDecls.end();
         DeclIt++) {

      if (FD->ReadOnlyDecls.find(*DeclIt) == FD->ReadOnlyDecls.end()) {
        ss <<  "SPECREADINIT(" << (*DeclIt)->getNameAsString() << ");\n";
      }
    }

    for (DeclIt = FD->WriteDecls.begin();
         DeclIt != FD->WriteDecls.end();
         DeclIt++) {
      ss <<  "SPECWRITEINIT(" << (*DeclIt)->getNameAsString() << ");\n";
    }

    rw.InsertText(FD->Directive->Range.getBegin(), StringRef(ss.str()), false, true);

    StringRef swstop("stopParallelExe();\n");


    InsertBrackets(SourceRange(FD->Directive->Range.getBegin(), FD->S->getLocEnd()), FD->CI);
    rw.InsertText(FD->S->getLocEnd().getLocWithOffset(1), swstop, true, true);

    stringstream ss2;

    ss2 << "#pragma omp parallel\n";

    rw.InsertText(FD->Directive->Range.getBegin(),StringRef(ss2.str()), false, true);
    rw.RemoveText(FD->Directive->ParaConstruct.Range);




  } else {

    CompoundStmt * CS = dyn_cast<CompoundStmt>(FD->S);

    if (!CS) {

      SourceLocation Begin = FD->S->getLocStart();
      SourceLocation End = FindSemiAfterLocation(FD->S->getLocEnd(),
                                                 FD->CI->getASTContext());

      stringstream ss;

      set<NamedDecl *>::iterator DeclIt;

      for (DeclIt = FD->ReadDecls.begin();
           DeclIt != FD->ReadDecls.end();
           DeclIt++) {

        if (FD->ReadOnlyDecls.find(*DeclIt) == FD->ReadOnlyDecls.end()) {
          ss <<  "SPECREADINIT(" << (*DeclIt)->getNameAsString() << ");\n";
        }

      }

      for (DeclIt = FD->WriteDecls.begin();
           DeclIt != FD->WriteDecls.end();
           DeclIt++) {
        ss <<  "SPECWRITEINIT(" << (*DeclIt)->getNameAsString() << ");\n";
      }

      rw.InsertText(Begin, StringRef(ss.str()), false, true);
      rw.InsertText(Begin, "{\n", false, true);

      StringRef swstop("\nstopParallelExe();");

      rw.InsertText(FD->S->getLocEnd().getLocWithOffset(1), swstop, false, true);

      rw.InsertText(End.getLocWithOffset(1), "\n}\n", true, true);


    } else {

      stringstream ss;

      set<NamedDecl *>::iterator DeclIt;

      for (DeclIt = FD->ReadDecls.begin();
           DeclIt != FD->ReadDecls.end();
           DeclIt++) {

        if (FD->ReadOnlyDecls.find(*DeclIt) == FD->ReadOnlyDecls.end()) {
          ss <<  "\nSPECREADINIT(" << (*DeclIt)->getNameAsString() << ");";
        }

      }

      for (DeclIt = FD->WriteDecls.begin();
           DeclIt != FD->WriteDecls.end();
           DeclIt++) {
        ss <<  "\nSPECWRITEINIT(" << (*DeclIt)->getNameAsString() << ");";
      }

      rw.InsertText(FD->S->getLocStart().getLocWithOffset(1), StringRef(ss.str()), false, true);

      StringRef swstop("\nstopParallelExe();");

      rw.InsertText(FD->S->getLocEnd().getLocWithOffset(1), swstop, false, true);


    }

  }

  StringRef swstart("startParallelExe();\n");

  rw.InsertText(FD->Header->getLBracLoc(), swstart, false, true);


}

void DirectiveHandler::InsertCacheAssignments(SpeculativeFunction * SF) {

  Rewriter &rw = globals::GetRewriter(*(SF->CI));
  CompoundStmt * S = dyn_cast<CompoundStmt>(SF->S);
  assert(S);

  stringstream ss;

  set<NamedDecl *>::iterator DeclIt;

  for (DeclIt = SF->ReadDecls.begin();
       DeclIt != SF->ReadDecls.end();
       DeclIt++) {
    if (SF->ReadOnlyDecls.find(*DeclIt) == SF->ReadOnlyDecls.end()) {
      ss <<  "\nSPECREADINIT(" << (*DeclIt)->getNameAsString() << ");";
    }
  }

  for (DeclIt = SF->WriteDecls.begin();
       DeclIt != SF->WriteDecls.end();
       DeclIt++) {
    ss <<  "\nSPECWRITEINIT(" << (*DeclIt)->getNameAsString() << ");";
  }

  rw.InsertText(S->getLBracLoc().getLocWithOffset(1), StringRef(ss.str()), false, true);

  stringstream ss2;
  ss2 << "releaseCaches(" << SF->ReadDecls.size() + SF->WriteDecls.size() - SF->ReadOnlyDecls.size() << ");\n";

  rw.InsertText(S->getRBracLoc(), StringRef(ss2.str()), false, true);

}

void DirectiveHandler::InsertChecks(CompilerInstance &CI, PragmaDirectiveMap &Directives) {

  PragmaDirectiveMap::iterator DirIt;
  Rewriter &rw = globals::GetRewriter(CI);

  map<PragmaDirective *, FullDirective *> AllDirectives
      = FullDirectives->getAllDirectives();
  map<PragmaDirective *, FullDirective *>::iterator FullDirIt;

  for (DirIt = Directives.begin(); DirIt != Directives.end(); DirIt++) {

    PragmaDirective * D = DirIt->second;
    FullDirective * FD = NULL;

    FullDirIt = AllDirectives.find(D);

    SourceLocation Begin, End;

    if (FullDirIt != AllDirectives.end()) {
      FD = FullDirIt->second;
      Begin = FD->S->getLocStart();
      End = FindSemiAfterLocation(FD->S->getLocEnd(), CI.getASTContext());

      if (End.isInvalid()) {
        End = FD->S->getLocEnd();
      }

      End = End.getLocWithOffset(1);

    }

    StringRef s("\ndetectDependences();\n");

    llvm::errs() << "Found " << PragmaDirective::getConstructTypeString(D->MainConstruct.Type) << "\n";

    switch (D->MainConstruct.Type) {

     case ParallelConstruct:
      assert(FD);
      rw.InsertText(End.getLocWithOffset(-1),s,false,true);
      break;
     case ForConstruct:
      assert(FD);
      if (!D->isNowait()) {
        rw.InsertText(End,s,false,true);
      }
      break;
     case SingleConstruct:
      assert(FD);
      if (!D->isNowait()) {
        rw.InsertText(End,s,true,true);
      }
      break;
     case BarrierConstruct:
      rw.InsertText(D->Range.getEnd(),s,true,true);
      break;
     // No barrier hence no need for check
     case MasterConstruct:
     case CriticalConstruct:
     case ThreadprivateConstruct:
     case FlushConstruct:
      break;
     default:
      assert(false && "Unsupported pragma construct");
    }

  }

}

void DirectiveHandler::InsertInit() {

  set<FunctionDecl *> Functions = globals::GetAllFunctionDecls();
  set<FunctionDecl *>::iterator FuncIt;

  for (FuncIt = Functions.begin(); FuncIt != Functions.end(); FuncIt++) {

    if ((*FuncIt)->getName() == "main") {

      FunctionDecl * TheFunc = *FuncIt;
      CompilerInstance &CI = *globals::GetCompilerInstance(TheFunc);
      Rewriter &rw = globals::GetRewriter(CI);

      assert(TheFunc->hasBody());

      CompoundStmt * CS = cast<CompoundStmt>(TheFunc->getBody());
      assert(CS);

      stringstream ss;
      ss << "createTables(" << FullDirectives->getMaxCachesRequired() << ");\n";

      StringRef s(ss.str());

      StringRef s3("\nomp_set_num_threads(MAX_THREADS);\n");

      rw.InsertText(CS->getLBracLoc().getLocWithOffset(1), s, false, true);
      rw.InsertText(CS->getLBracLoc().getLocWithOffset(1), s3, false, true);

      StringRef s2("printStats();\n"
                   "if (getDependenceCheckResult()) {\n"
                   "  printf(\"\\n ##############################\\n\");\n"
                   "  printf(\"     DEPENDENCE DETECTED!!!\\n\");\n"
                   "  printf(\"     DEPENDENCE DETECTED!!!\\n\");\n"
                   "  printf(\"     DEPENDENCE DETECTED!!!\\n\");\n"
                   "  printf(\" ##############################\\n\");\n"
                   "} else {\n"
                   "  printf(\"\\n No dependences detected\\n\");\n"
                   "}\n");

      rw.InsertText(CS->getRBracLoc(), s2, false, true);


      break;

    }

  }

}



} // End namespace speculation

