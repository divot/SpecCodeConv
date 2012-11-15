//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "DeclExtractor.h"
#include "DeclTracker.h"
#include "DirectiveList.h"
#include "Globals.h"
#include "PragmaDirective.h"
#include "Tools.h"
#include "VarTraverser.h"
#include "NoEditStmtPrinter.h"

using clang::dyn_cast;
using clang::ForStmt;
using clang::FieldDecl;
using clang::QualType;
using clang::Qualifiers;

using speculation::tools::FindLocationAfterSemi;
using speculation::tools::GetChildRange;
using speculation::tools::GetLocation;
using speculation::tools::GetType;
using speculation::tools::InsideRange;
using speculation::tools::IsChild;

namespace speculation {

// Public
DirectiveList::DirectiveList(DeclTracker * TrackedVars)
  : TrackedVars(TrackedVars),
    AllDirectives(),
    AllCalls(),
    AllSpeculativeFunctions(),
    TopLevelDirectives(),
    CurrentDirectives(),
    Changed(false) {

  assert(TrackedVars);

}

// Private
void DirectiveList::TrackDecl(VarDecl * VD, DeclMap &TrackedDecls) {

  assert(VD);

  // Do we really need to check that this decl hasn't been added before?
  DeclMap::iterator TrackIt = TrackedDecls.find(VD);

  if (TrackIt == TrackedDecls.end()) {
    TrackIt = TrackedDecls.insert(make_pair(VD, TypeMap())).first;
  }

  TypeMap &Types = TrackIt->second;

  // Loop over this decl's type and its derivate types
  // Adding them as private
  const Type * T = VD->getType()->getUnqualifiedDesugaredType();

  do {

    // Do we really need to check that this type hasn't been added before?
    TypeMap::iterator TypeIt = Types.find(GetType(T));
    
    if (TypeIt == Types.end()) {
      Types.insert(make_pair(GetType(T), true));
    }
    
    if (T->isPointerType()) {
      T = T->getPointeeType()->getUnqualifiedDesugaredType();
    } else if (T->isArrayType()) {
      T = T->getAsArrayTypeUnsafe()->getElementType()->getUnqualifiedDesugaredType();
    } else {

      if (T->isStructureType()) {
        TrackStructDecl(GetType(T), T->getAsStructureType()->getDecl(), Types);
      } else if (T->isUnionType()) {
        TrackStructDecl(GetType(T), T->getAsUnionType()->getDecl(), Types);
      }

      T = NULL;

    }

  } while (T);

}

// Private
void DirectiveList::TrackStructDecl(string BaseType,
                                    const RecordDecl * RD,
                                    TypeMap &Types) {

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

      // Do we really need to check that this type hasn't been added before?
      TypeMap::iterator TypeIt = Types.find(GetType(T));

      if (TypeIt == Types.end()) {
        stringstream TypeString;
        TypeString << BaseType << "." << FD->getName().str() << ":"
                   << GetType(T);

        Types.insert(make_pair(string(TypeString.str()), true));
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
          TrackStructDecl(string(TypeString.str()), T->getAsStructureType()->getDecl(), Types);
        } else if (T->isUnionType()) {
          stringstream TypeString;
          TypeString << BaseType << "." << FD->getName().str() << ":"
                     << GetType(T);
          TrackStructDecl(string(TypeString.str()), T->getAsUnionType()->getDecl(), Types);
        }

        T = NULL;

      }

    } while (T);

  }

}

// Private
FullDirective * DirectiveList::CreateFullDirective(PragmaDirective *Directive,
                                                   CompoundStmt *Header,
                                                   Stmt *S,
                                                   CompilerInstance &CI,
                                                   StackItem * Parent) {

  // Generate the new directive
  FullDirective *D = new FullDirective;
  D->Directive = Directive;
  D->Header = Header;
  D->ChildRange = GetChildRange(S, CI);
  D->S = S;
  D->CI = &CI;
  D->Parent = Parent;
  
  // Extract the decls from the header ready to be tracked
  // DeclExtractor ensures they are globally used versions
  set<NamedDecl *> Decls;
  DeclExtractor DE(Decls);
  DE.TraverseStmt(Header);

  // For each of the private declarations in the header
  // Start tracking them
  set<NamedDecl *>::iterator DeclIt;
  for (DeclIt = Decls.begin(); DeclIt != Decls.end(); DeclIt++) {
    VarDecl * VD = dyn_cast<VarDecl>(*DeclIt);
    assert(VD);
    TrackDecl(VD, D->TrackedDecls);
  }

  llvm::errs() << "--- Created Directive ---\n";
  printStackItem(D);
  llvm::errs() << "-------------------------\n";

  return D;

}

// Private
FunctionCall * DirectiveList::CreateFunction(CallExpr *TheCall,
                                             FunctionDecl *TheFunction,
                                             CompilerInstance *CI,
                                             StackItem * Parent) {

  // Generate the new directive
  FunctionCall *F = new FunctionCall;
  F->TheCall = TheCall;
  F->TheFunction = TheFunction;
  F->ChildRange = GetChildRange(TheFunction->getBody(), *CI);
  F->S = TheFunction->getBody();
  F->CI = CI;
  F->Parent = Parent;
  
  // For each of the parameters in the function call (assume private at first)
  // Start tracking them
  unsigned i = 0;
  
  // First get the set of params, and link them to passed in vars
  FunctionDecl::param_iterator DeclIt;
  for (DeclIt = TheFunction->param_begin(); 
       DeclIt != TheFunction->param_end();
       DeclIt++) {
    
    VarDecl * TheDecl = *DeclIt;

    TrackDecl(TheDecl, F->TrackedDecls);

    const Type * T = TheDecl->getType()->getUnqualifiedDesugaredType();
    
    if (T->isPointerType() || T->isArrayType()) {
      
      // Get Dominant DeclRef and its expr
      DeclRefExpr * DominantRef = NULL;
      Expr * DominantExpr = NULL;

      VarTraverser vt(TheCall->getArg(i), DominantRef, DominantExpr, *CI);
      vt.TraverseStmt(TheCall->getArg(i));

      assert(DominantRef);
      assert(DominantExpr);
      
      NamedDecl * DominantDeclPreTrans = dyn_cast<VarDecl>(DominantRef->getDecl());

      NamedDecl * DominantDecl = globals::GetNamedDecl(DominantDeclPreTrans);
      
      F->ParamTranslations.insert(make_pair(TheDecl, DominantDecl));
      
    }
    
    i++;
    
  }
  
  llvm::errs() << "--- Created Function Call ---\n";
  printStackItem(F);
  llvm::errs() << "-----------------------------\n";

  return F;

}

// Private
SpeculativeFunction * DirectiveList::CreateSpecFunction(FunctionCall *TheCall) {

  SpeculativeFunction *S = new SpeculativeFunction;

  S->TheFunction = TheCall->TheFunction;
  S->ChildRange = TheCall->ChildRange;
  S->S = TheCall->S;
  S->CI = TheCall->CI;
  S->TrackedDecls = TheCall->TrackedDecls;
  S->Parent = NULL;

  return S;

}

// Private
void DirectiveList::ContaminateSpecFunction(SpeculativeFunction *TheFunction,
                                            FunctionCall *TheCall) {

  DeclMap &CallMap = TheCall->TrackedDecls;
  DeclMap &FuncMap = TheFunction->TrackedDecls;

  DeclMap::iterator CallIt;
  DeclMap::iterator FuncIt;

  // For Each Tracked Decl Within a Call
  for (CallIt = CallMap.begin(); CallIt != CallMap.end(); CallIt++) {

    FuncIt = FuncMap.find(CallIt->first);
    assert(FuncIt != FuncMap.end());

    TypeMap &CallTypes = CallIt->second;
    TypeMap &FuncTypes = FuncIt->second;

    TypeMap::iterator CallTypeIt;
    TypeMap::iterator FuncTypeIt;

    // Loop Over its Type Hierarchy
    for (CallTypeIt = CallTypes.begin();
         CallTypeIt != CallTypes.end();
         CallTypeIt++) {

      // If it's marked as speculative
      if (!CallTypeIt->second) {

        FuncTypeIt = FuncTypes.find(CallTypeIt->first);
        assert(FuncTypeIt != FuncTypes.end());

        // Make sure the Function Calls is also marked as speculative
        if (FuncTypeIt->second) {

          FuncTypes.erase(FuncTypeIt);
          FuncTypes.insert(make_pair(CallTypeIt->first, CallTypeIt->second));

        }

      }

    }

  }

}

// Private
void DirectiveList::RemoveToParent(SourceLocation Loc) {

  while (!CurrentDirectives.empty()
         && !InsideRange(Loc,
                         CurrentDirectives.front()->ChildRange,
                         *(CurrentDirectives.front()->CI))) {
    CurrentDirectives.pop_front();
  }
  
}

// Public
void DirectiveList::CreateTopLevel(PragmaDirective *Directive,
                                   CompoundStmt *Header,
                                   Stmt *S,
                                   CompilerInstance &CI) {

  assert(Directive);
  assert(Header);
  assert(S);

  map<PragmaDirective *, FullDirective *>::iterator DMapIt;
  DMapIt = AllDirectives.find(Directive);
  assert(DMapIt == AllDirectives.end());
  
  Changed = true;

  FullDirective * D = CreateFullDirective(Directive, Header, S, CI, NULL);

  AllDirectives.insert(make_pair(Directive, D));
  TopLevelDirectives.push_back(D);

}

// Public
FullDirective * DirectiveList::Push(PragmaDirective *Directive,
                                    CompoundStmt *Header,
                                    Stmt *S,
                                    CompilerInstance &CI) {
                         
  assert(Directive);
  assert(Header);
  assert(S);

  RemoveToParent(S->getLocStart());

  map<PragmaDirective *, FullDirective *>::iterator DMapIt;
  DMapIt = AllDirectives.find(Directive);
  
  // If we haven't encountered this directive before
  // then we need to add it to our list, and start tracking
  // the private variables
  if (DMapIt == AllDirectives.end()) {
  
    Changed = true;

    FullDirective * FD = CreateFullDirective(Directive,
                                             Header,
                                             S,
                                             CI,
                                             CurrentDirectives.front());

    DMapIt = AllDirectives.insert(make_pair(Directive, FD)).first;
  
  }
  
  FullDirective *D = DMapIt->second;

  CurrentDirectives.push_front(D);
  
  return D;

}

// Public
FunctionCall * DirectiveList::Push(CallExpr *TheExpr) {

  assert(TheExpr);

  RemoveToParent(TheExpr->getLocStart());

  map<CallExpr *, FunctionCall *>::iterator FMapIt = AllCalls.find(TheExpr);
  
  // If we haven't encountered this call before
  // then we need to add it to our list, and start tracking
  // the private variables
  if (FMapIt == AllCalls.end()) {

    FunctionDecl * TheFunction = TheExpr->getDirectCallee();
    TheFunction = globals::GetFunctionDecl(TheFunction);

    if (!TheFunction->isDefined()) {
      return NULL;
    }

    CompilerInstance *CI = globals::GetCompilerInstance(TheFunction);

    FunctionCall * FC = CreateFunction(TheExpr,
                                       TheFunction,
                                       CI,
                                       CurrentDirectives.front());

    Changed = true;
    FMapIt = AllCalls.insert(make_pair(TheExpr, FC)).first;
  
  }

  FunctionCall *F = FMapIt->second;

  map<NamedDecl *, NamedDecl *> &ParamTranslations = F->ParamTranslations;
  FunctionDecl * TheFunction = F->TheFunction;
  CallExpr * TheCall = F->TheCall;

  CompilerInstance &CI = GetCI(TheExpr->getLocStart());
  CurrentDirectives.push_front(F);

  // Loop over params
  // For those with translations
  // Copy over any contamination (as far up the stack as normal)
  for (unsigned i = 0; i < TheFunction->param_size(); i++) {

    NamedDecl * TheParam = TheFunction->getParamDecl(i);

    map<NamedDecl *, NamedDecl *>::iterator Translation;
    Translation = ParamTranslations.find(TheParam);

    // If there isn't a translation for this parameter, then it's local only
    // and cannot be contaminated by anything outside.
    if (Translation == ParamTranslations.end()) {
      continue;
    }

    NamedDecl * TranslatedParam = Translation->second;

    Expr * TheArg = TheCall->getArg(i);

    DeclRefExpr * DomRef = NULL;
    Expr * DomExpr = NULL;

    VarTraverser VT(TheArg, DomRef, DomExpr, CI, false);
    VT.TraverseStmt(TheArg);

    assert(DomRef);
    assert(DomExpr);

    string TARG = GetType(DomExpr);

    ContaminateDecl(TheParam,
                    string(),
                    TranslatedParam,
                    TARG,
                    TheArg->getType().getTypePtr());

  }

  return F;

}

// Public
SpeculativeFunction * DirectiveList::Push(SpeculativeFunction *TheFunc) {

  assert(TheFunc);

  assert(CurrentDirectives.empty());
  CurrentDirectives.push_back(TheFunc);
  return TheFunc;

}

// Public
list<FullDirective *> DirectiveList::GetTopLevelDirectives() {

  return TopLevelDirectives;

}

// Public
bool DirectiveList::InsideTopLevel(Stmt * S) {

  assert(S);

  list<FullDirective *>::iterator It;

  for (It = TopLevelDirectives.begin();
       It != TopLevelDirectives.end();
       It++) {

    if (IsChild(S, (*It)->S)) {
      return true;
    }

  }

  return false;

}

// Public
list<StackItem *> DirectiveList::GetHandlerStartPoints() {

  list<StackItem *> Output;

  list<FullDirective *>::iterator DirectiveIt;
  map<FunctionDecl *, SpeculativeFunction *>::iterator SpecFuncIt;

  for (SpecFuncIt = AllSpeculativeFunctions.begin();
       SpecFuncIt != AllSpeculativeFunctions.end();
       SpecFuncIt++) {

    Output.push_back(SpecFuncIt->second);

  }

  for (DirectiveIt = TopLevelDirectives.begin();
       DirectiveIt != TopLevelDirectives.end();
       DirectiveIt++) {

    for (SpecFuncIt = AllSpeculativeFunctions.begin();
         SpecFuncIt != AllSpeculativeFunctions.end();
         SpecFuncIt++) {

      if (InsideRange((*DirectiveIt)->Directive->Range.getBegin(),
                      SpecFuncIt->second->ChildRange,
                      *(SpecFuncIt->second->CI))) {
        (*DirectiveIt)->Parent = SpecFuncIt->second;
        break;
      }

    }

    if (SpecFuncIt == AllSpeculativeFunctions.end()) {
      Output.push_back(*DirectiveIt);
    }

  }

  return Output;

}

// Public
bool DirectiveList::IsPrivate(DeclRefExpr * Current,
                              string TS,
                              const Type * T,
                              SourceLocation Loc) {

  assert(Current);
  assert(T);
                              
  T = T->getUnqualifiedDesugaredType();

  NamedDecl * VD = globals::GetNamedDecl(Current->getDecl());
  
  RemoveToParent(Loc);
  
  bool Found = false;
  bool Private = true;
  
  list<StackItem *>::iterator DirIt;
  
  // Search through all current directives up to the uppermost parallel directive
  // Or until the uppermost function call where it is an argument that cannot
  // contaminate externally
  for (DirIt = CurrentDirectives.begin(); 
       DirIt != CurrentDirectives.end(); 
       DirIt++) {
  
    DeclMap::iterator DeclIt = (*DirIt)->TrackedDecls.find(VD);
  
    // The directive in this list
    if (DeclIt != (*DirIt)->TrackedDecls.end()) {

      Found = true;
      
      TypeMap::iterator TypeIt = DeclIt->second.find(TS + GetType(T));
      
      // If we've found an entry, but not for that specific type, we must have
      // encountered an AddrOf op, at the top of a potentially private var.
      // Just check the privacy of the var itself instead of the pointer to it.
      if (TypeIt == DeclIt->second.end()) {
        T = T->getPointeeType()->getUnqualifiedDesugaredType();
        TypeIt = DeclIt->second.find(GetType(T));
      }

      // By now we really should have found the type we're looking for.
      assert(TypeIt != DeclIt->second.end());
      
      // Var isn't private if ANY level has been contaminated
      // Frankly if one level is contaminated, all should be already
      Private = Private && TypeIt->second;

    }

    if (FinishedSearchingStack(*DirIt, VD)) {
      break;
    }
  
  }
  
  if (Found) {
    return Private;
  } else {
    return false;
  }

}

// Public
bool DirectiveList::IsCompletelyPrivate(DeclRefExpr * Current,
                                        SourceLocation Loc) {

  assert(Current);

  NamedDecl * VD = globals::GetNamedDecl(Current->getDecl());
  
  RemoveToParent(Loc);
  
  bool Found = false;
  
  list<StackItem *>::iterator DirIt;
  
  // Search through all current directives up to the uppermost parallel directive
  for (DirIt = CurrentDirectives.begin();
       DirIt != CurrentDirectives.end();
       DirIt++) {
  
    DeclMap::iterator DeclIt = (*DirIt)->TrackedDecls.find(VD);
  
    // Found the VarDecl in the current Directive
    if (DeclIt != (*DirIt)->TrackedDecls.end()) {

      Found = true;
      
      TypeMap &Types = DeclIt->second;
      TypeMap::iterator TypeIt;
      
      for (TypeIt = Types.begin(); TypeIt != Types.end(); TypeIt++) {
        
        if (!TypeIt->second) {
          return false;
        }
        
      }
      
    }

    if (FinishedSearchingStack(*DirIt, VD)) {
      break;
    }

  }
  
  // If we've got here without returning, then either the variable is
  // completely private (Found = true), or was never private in the first place
  // (Found = false)
  return Found;  

}


// Public
void DirectiveList::InsertPrivateDecl(VarDecl * VD) {

  // Only ever called from within a function, hence cannot be global!
  // No need to get from globals::GetNamedDecl...

  assert(VD);

  RemoveToParent(VD->getLocStart());

  assert(!CurrentDirectives.empty());
  
  DeclMap &TrackedDecls = CurrentDirectives.front()->TrackedDecls;
  
  DeclMap::iterator TrackIt = TrackedDecls.find(VD);
  
  // If we haven't seen this declaration before
  if (TrackIt == TrackedDecls.end()) {

    Changed = true;

    TrackDecl(VD, TrackedDecls);
    
  }

}

// Public
bool DirectiveList::IsLocDirectlyAfterPragma(SourceLocation Loc) {

  RemoveToParent(Loc);
  
  assert(!CurrentDirectives.empty());
  
  if (CurrentDirectives.front()->TYPE == StackItem::SpeculativeFunctionType) {
    return false;
  }

  return CurrentDirectives.front()->ChildRange.getBegin() == Loc;

}

// Public
CompoundStmt * DirectiveList::GetHeader(SourceLocation Loc) {

  RemoveToParent(Loc);
  
  assert(!CurrentDirectives.empty());
  assert(CurrentDirectives.front()->TYPE == StackItem::FullDirectiveType);

  FullDirective * FD = (FullDirective *) CurrentDirectives.front();
  
  return FD->Header;

}

// Private
bool DirectiveList::FinishedSearchingStack(StackItem * Item,
                                           NamedDecl *& TheDecl) {

  if (FullDirective::ClassOf(Item)) {

    FullDirective * FD = (FullDirective *) Item;

    // Stop searching for "RHS" directives once you've hit a parallel one
    if (FD->Directive->isParallel()) {
      return true;
    }

  } else if (FunctionCall::ClassOf(Item)) {

    FunctionCall * FC = (FunctionCall *) Item;

    // If we're looking for a parameter
    // (Only parameters are tracked at the function call level)
    // If it's not tracked, then it's a global, no translation needs to occur,
    // but the search must continue
    DeclMap::iterator DeclIt = Item->TrackedDecls.find(TheDecl);

    if (DeclIt != FC->TrackedDecls.end()) {

      map<NamedDecl *, NamedDecl *>::iterator Trans =
          FC->ParamTranslations.find(TheDecl);

      //If we have a translation for this, then use that from now on
      if (Trans != FC->ParamTranslations.end()) {
        TheDecl = Trans->second;

      // Otherwise it's guaranteed to be a value param, and we can stop
      // searching
      } else {
        return true;
      }

    }


  } else if (SpeculativeFunction::ClassOf(Item)) {

    return true;

  } else {

    // TODO: Change to actual error
    llvm::errs() << "WTF!? ##################################\n";
    assert(false);

  }

  return false;

}

//Public
bool DirectiveList::ContaminateDecl(NamedDecl * VDLHS,
                                    string TLHS,
                                    NamedDecl * VDRHS,
                                    string TRHS,
                                    const Type * T) {

  assert(VDLHS);
  assert(VDRHS);
  assert(T);
                                    
  // ContaminateDecl looks through all of the current directives for the LHS
  // and RHS.
  // If it cannot find one then it is not a private variable and already
  // completely contaminated.
  // Hence it must appropriately contaminated the other side
  
  // This is done using a nested loop.
  // First stage finding the LHS
  // Second stage finding the RHS
  
  // If both are found then they swap contamination
  
  // If only LHS is found, it is contaminated as much as possible
  // If LHS is not found, RHS is searched for separately and contaminated
  // as much as possible
  
  // If at any stage a function call is found, then it must be checked if
  // we are dealing with a parameter passed in.
  // If so, the next directive up the stack must use the translated version of
  // the argument
  
  // If this happens while searching for the RHS, the next iteration of the LHS
  // must use the original RHS, as the search for RHS effectively restarts.
  
  // Note: If the variable is a parameter in a function call, it will definitely
  // be tracked. In fact the only variables tracked in a function call will be
  // parameters. However, it may not have a translation. If it is a parameter,
  // without a translation then it is a value passed in and the variable only
  // exists within the function. Searching can stop.
  
  T = T->getUnqualifiedDesugaredType();

  VDLHS = globals::GetNamedDecl(VDLHS);
  VDRHS = globals::GetNamedDecl(VDRHS);

  NamedDecl * LHS = VDLHS;
  NamedDecl * RHS = VDRHS;
  
  bool FoundLHS = false;
  bool FoundRHS = false;
  bool Contaminated = false;

  list<StackItem *>::iterator DirItLHS;
  
  // Looking through all the directives, from the most current
  // down to the bottom or the first parallel directive encountered
  for (DirItLHS = CurrentDirectives.begin(); 
       DirItLHS != CurrentDirectives.end(); 
       DirItLHS++) {
       
    DeclMap::iterator DeclItLHS = (*DirItLHS)->TrackedDecls.find(LHS);
  
    // If the "LHS" variable is tracked by this directive
    if (DeclItLHS != (*DirItLHS)->TrackedDecls.end()) {
    
      FoundLHS = true;
    
      TypeMap &TypesLHS = DeclItLHS->second;

      list<StackItem *>::iterator DirItRHS;
      
      // Loop over all the directives in the same fashion
      // looking for the "From" variable.
      for (DirItRHS = CurrentDirectives.begin();
           DirItRHS != CurrentDirectives.end();
           DirItRHS++) {
           
        DeclMap::iterator DeclItRHS = (*DirItRHS)->TrackedDecls.find(RHS);
      
        // If the "RHS" variable is tracked by this directive
        if (DeclItRHS != (*DirItRHS)->TrackedDecls.end()) {
        
          FoundRHS = true;
        
          TypeMap &TypesRHS = DeclItRHS->second;
          
          // We have managed to find the LHS and the RHS!
          Contaminated = ContaminateSwap(TypesLHS, TLHS, TypesRHS, TRHS, T)
                         || Contaminated;
          
        }
           
        if (FinishedSearchingStack(*DirItRHS, RHS)) {
          continue;
        }

      }
      
      if (!FoundRHS) {
        
        // We found an LHS but not an RHS
        Contaminated = ContaminateAll(TypesLHS, TLHS, T) || Contaminated;
        
      }
      
      // Reset RHS so we can start searching from the top again next time
      RHS = VDRHS;

      
    }

    if (FinishedSearchingStack(*DirItLHS, LHS)) {
      continue;
    }

  }
  
  if (!FoundLHS) {
    
    // We haven't found an LHS, so haven't looked for an RHS yet
    // If there is an RHS though, it must be fully contaminated
    
    list<StackItem *>::iterator DirItRHS;
    
    // Loop over all the directives in the same fashion
    // looking for the "From" variable.
    for (DirItRHS = CurrentDirectives.begin();
         DirItRHS != CurrentDirectives.end();
         DirItRHS++) {
         
      DeclMap::iterator DeclItRHS = (*DirItRHS)->TrackedDecls.find(RHS);
    
      // If the "RHS" variable is tracked by this directive
      if (DeclItRHS != (*DirItRHS)->TrackedDecls.end()) {
      
        FoundRHS = true;
        
        TypeMap &TypesRHS = DeclItRHS->second;

        Contaminated = ContaminateAll(TypesRHS, TRHS, T) || Contaminated;
        
      }

      if (FinishedSearchingStack(*DirItRHS, RHS)) {
        continue;
      }

    }

  }

  return Contaminated;

}

// Private
bool DirectiveList::ContaminateAll(TypeMap &Types, string TS, const Type * T, bool IncFirst) {

  bool Contaminated = false;

  const Type * CurrentT;
  
  if (IncFirst) {
    CurrentT = T;
  } else {
    assert(T->isPointerType());
    CurrentT = T->getPointeeType()->getUnqualifiedDesugaredType();
  }

  while (CurrentT) {

    TypeMap::iterator TypeIt = Types.find(TS + GetType(CurrentT));

    assert(TypeIt != Types.end());
    
    if (!TypeIt->second) {
      break;
    }

    Changed = true;
    Contaminated = true;
    Types.erase(TypeIt);
    Types.insert(make_pair(TS + GetType(CurrentT), false));

    if (CurrentT->isPointerType()) {
      CurrentT = CurrentT->getPointeeType()->getUnqualifiedDesugaredType();
    } else if (CurrentT->isArrayType()) {
      CurrentT = CurrentT->getAsArrayTypeUnsafe()->getElementType()->getUnqualifiedDesugaredType();
    } else {

      if (CurrentT->isStructureType()) {

        RecordDecl * RD = CurrentT->getAsStructureType()->getDecl();
        RecordDecl::field_iterator FieldIt;

        for (FieldIt = RD->field_begin(); FieldIt != RD->field_end(); FieldIt++) {

          FieldDecl * FD = *FieldIt;

          if (!FD) {
            continue;
          }

          const Type * NT = FD->getType()->getUnqualifiedDesugaredType();
          string NTS = TS + GetType(CurrentT) + "." + FD->getNameAsString() + ":";

          Contaminated = ContaminateAll(Types, NTS, NT, true) || Contaminated;

        }

      } else if (CurrentT->isUnionType()) {

        RecordDecl * RD = CurrentT->getAsUnionType()->getDecl();
        RecordDecl::field_iterator FieldIt;

        for (FieldIt = RD->field_begin(); FieldIt != RD->field_end(); FieldIt++) {

          FieldDecl * FD = *FieldIt;

          if (!FD) {
            continue;
          }

          const Type * NT = FD->getType()->getUnqualifiedDesugaredType();
          string NTS = TS + GetType(CurrentT) + "." + FD->getNameAsString() + ":";

          Contaminated = ContaminateAll(Types, NTS, NT, true) || Contaminated;

        }

      }

      CurrentT = NULL;
    }

  }

  return Contaminated;

}

// Private
bool DirectiveList::ContaminateSwap(TypeMap &TypesLHS,
                                    string TLHS,
                                    TypeMap &TypesRHS,
                                    string TRHS,
                                    const Type * T,
                                    bool IncFirst) {
  
  bool Contaminated = false;

  const Type * CurrentT;

  if (IncFirst) {
    CurrentT = T;
  } else {
    assert(T->isPointerType());
    CurrentT = T->getPointeeType()->getUnqualifiedDesugaredType();
  }

  while (CurrentT) {

    TypeMap::iterator TypeItLHS = TypesLHS.find(TLHS + GetType(CurrentT));
    TypeMap::iterator TypeItRHS = TypesRHS.find(TRHS + GetType(CurrentT));

    assert(TypeItLHS != TypesLHS.end());
    assert(TypeItRHS != TypesRHS.end());
    
    if (!TypeItLHS->second && !TypeItRHS->second) {

      break;

    } else if (!TypeItLHS->second && TypeItRHS->second) {

      Changed = true;
      Contaminated = true;
      TypesRHS.erase(TypeItRHS);
      TypesRHS.insert(make_pair(TRHS + GetType(CurrentT), false));

    } else if (TypeItLHS->second && !TypeItRHS->second) {

      Changed = true;
      Contaminated = true;
      TypesLHS.erase(TypeItLHS);
      TypesLHS.insert(make_pair(TLHS + GetType(CurrentT), false));

    }
    
    if (CurrentT->isPointerType()) {
      CurrentT = CurrentT->getPointeeType()->getUnqualifiedDesugaredType();
    } else if (CurrentT->isArrayType()) {
      CurrentT = CurrentT->getAsArrayTypeUnsafe()->getElementType()->getUnqualifiedDesugaredType();
    } else {

      if (CurrentT->isStructureType()) {

        RecordDecl * RD = CurrentT->getAsStructureType()->getDecl();
        RecordDecl::field_iterator FieldIt;

        for (FieldIt = RD->field_begin(); FieldIt != RD->field_end(); FieldIt++) {

          FieldDecl * FD = *FieldIt;

          if (!FD) {
            continue;
          }

          const Type * NT = FD->getType()->getUnqualifiedDesugaredType();
          string NTLHS = TLHS + GetType(CurrentT) + "." + FD->getNameAsString() + ":";
          string NTRHS = TRHS + GetType(CurrentT) + "." + FD->getNameAsString() + ":";

          Contaminated = ContaminateSwap(TypesLHS, NTLHS, TypesRHS, NTRHS, NT, true)
                         || Contaminated;

        }


      } else if (CurrentT->isUnionType()) {

        RecordDecl * RD = CurrentT->getAsUnionType()->getDecl();
        RecordDecl::field_iterator FieldIt;

        for (FieldIt = RD->field_begin(); FieldIt != RD->field_end(); FieldIt++) {

          FieldDecl * FD = *FieldIt;

          if (!FD) {
            continue;
          }

          const Type * NT = FD->getType()->getUnqualifiedDesugaredType();
          string NTLHS = TLHS + GetType(CurrentT) + "." + FD->getNameAsString() + ":";
          string NTRHS = TRHS + GetType(CurrentT) + "." + FD->getNameAsString() + ":";

          Contaminated = ContaminateSwap(TypesLHS, NTLHS, TypesRHS, NTRHS, NT, true)
                         || Contaminated;

        }

      }

      CurrentT = NULL;
    }
    
  }
  
  return Contaminated;

}

// Public
void DirectiveList::GenerateSpecFunctions() {

  map<CallExpr *, FunctionCall *>::iterator CallIt;

  for (CallIt = AllCalls.begin(); CallIt != AllCalls.end(); CallIt++) {

    FunctionDecl * TheFunction = CallIt->second->TheFunction;
    FunctionCall * TheCall = CallIt->second;

    map<FunctionDecl *, SpeculativeFunction *>::iterator FuncIt;
    FuncIt = AllSpeculativeFunctions.find(TheFunction);

    if (FuncIt == AllSpeculativeFunctions.end()) {

      SpeculativeFunction * TheSpecFunc = CreateSpecFunction(TheCall);
      AllSpeculativeFunctions.insert(make_pair(TheFunction, TheSpecFunc));

    } else {

      ContaminateSpecFunction(FuncIt->second, TheCall);

    }

  }

}

// Public
void DirectiveList::printStack(NamedDecl * VD) {

  assert(VD);

  VD = globals::GetNamedDecl(VD);

  bool Found = false;

  list<StackItem *>::iterator DirIt;

  // Looking through all the directives, from the most current
  // down to the bottom or the first parallel directive encountered
  for (DirIt = CurrentDirectives.begin();
       DirIt != CurrentDirectives.end();
       DirIt++) {

    DeclMap::iterator DeclIt = (*DirIt)->TrackedDecls.find(VD);

    // If the variable is tracked by this directive
    if (DeclIt != (*DirIt)->TrackedDecls.end()) {

      Found = true;

      CompilerInstance &CI = *((*DirIt)->CI);

      llvm::errs() << "\t" << GetLocation((*DirIt)->ChildRange.getBegin(), CI)
                   << " as " << DeclIt->first->getName() << "\n";

      TypeMap::iterator TypeIt;

      for (TypeIt = DeclIt->second.begin();
           TypeIt != DeclIt->second.end();
           TypeIt++) {

        llvm::errs() << "\t\t" << TypeIt->first;
        llvm::errs() << ": " << (TypeIt->second ? "Private" : "Speculative") << "\n";

      }

    }

    if (FinishedSearchingStack(*DirIt, VD)) {
      continue;
    }

  }

  if (!Found) {

    llvm::errs() << "\tNot Found!\n";

  }

}

// Public
void DirectiveList::ResetChangedStatus() {
  Changed = false;
}

// Public
bool DirectiveList::GetChangedStatus() {
  return Changed;
}

// Public
void DirectiveList::ResetStack() {
  CurrentDirectives.clear();
}

// Public
CompilerInstance &DirectiveList::GetCI(SourceLocation Loc) {

  RemoveToParent(Loc);

  assert(!CurrentDirectives.empty());

  return *(CurrentDirectives.front()->CI);

}

// Public
void DirectiveList::printFullStack() {

  list<StackItem *>::iterator AllIt;
  
  for (AllIt = CurrentDirectives.begin(); AllIt != CurrentDirectives.end(); AllIt++) {
    printStackItem(*AllIt);
  }

}

// Public
void DirectiveList::printStackItem(StackItem * I) {

  assert(I);

  if (FullDirective::ClassOf(I)) {

    FullDirective * D = (FullDirective *) I;

    llvm::errs() << "PragmaRange: "
                 << GetLocation(D->Directive->Range.getBegin(), *(D->CI))
                 << " - "
                 << GetLocation(D->Directive->Range.getEnd(), *(D->CI)) << "\n";

  } else if (FunctionCall::ClassOf(I)){

    FunctionCall * C = (FunctionCall *) I;

    llvm::errs() << "FunctionCall: " << C->TheFunction->getName() << ", "
                 << GetLocation(C->TheCall->getLocStart(), *(C->CI)) << "\n";


  } else if (SpeculativeFunction::ClassOf(I)) {

    SpeculativeFunction * F = (SpeculativeFunction *) I;

    llvm::errs() << "SpeculativeFunction: " << F->TheFunction->getName() << ", "
                 << GetLocation(F->TheFunction->getLocStart(), *(F->CI))
                 << "\n";

  } else {

    llvm::errs() << "UnknownStackItem:\n";

  }

  
  printDeclMap(I->TrackedDecls);

}

// Public
void DirectiveList::printDeclMap(DeclMap &Decls) {

  DeclMap::iterator DeclIt;
  
  for (DeclIt = Decls.begin(); DeclIt != Decls.end(); DeclIt++) {
  
    llvm::errs() << "\tDecl: " << DeclIt->first->getNameAsString()
                 << " (" << (int) DeclIt->first << ")\n";
    
    TypeMap::iterator TypeIt;
    
    for (TypeIt = DeclIt->second.begin();
         TypeIt != DeclIt->second.end();
         TypeIt++) {
    
      llvm::errs() << "\t\t" << TypeIt->first;
      llvm::errs() << ": " << (TypeIt->second ? "Private" : "Speculative") << "\n";
    
    }
  
  }

}

// Public
void DirectiveList::InsertDeclAccess(NamedDecl * D, bool Write) {

  D = globals::GetNamedDecl(dyn_cast<VarDecl>(D));

  list<StackItem *>::iterator StackIt;

  for (StackIt = CurrentDirectives.begin();
       StackIt != CurrentDirectives.end();
       StackIt++) {

    StackItem * I = *StackIt;

    if (Write) {
      I->WriteDecls.insert(D);
    } else {
      I->ReadDecls.insert(D);
    }

    if (FullDirective::ClassOf(I)) {
      continue;
    } else if (SpeculativeFunction::ClassOf(I)) {
      break;
    } else {
      assert(false && "Unexepected Stack Item Type");
    }

  }

}

// Public
map<PragmaDirective *, FullDirective *> DirectiveList::getAllDirectives() {
  return AllDirectives;
}

// Public
map<FunctionDecl *, SpeculativeFunction *> DirectiveList::getAllSpeculativeFunctions() {
  return AllSpeculativeFunctions;
}

// Public
int DirectiveList::getMaxCachesRequired() {

  int max = 0;

  map<FunctionDecl *, SpeculativeFunction *>::iterator FuncIt;
  for (FuncIt = AllSpeculativeFunctions.begin();
       FuncIt != AllSpeculativeFunctions.end();
       FuncIt++) {

    llvm::errs() << "### Handling " << FuncIt->first->getName() << " ###\n";

    int count = getMaxCachesRequired(FuncIt->second);

    if (count > max) {
      max = count;
    }

  }

  list<FullDirective *>::iterator DirIt;
  for (DirIt = TopLevelDirectives.begin();
       DirIt != TopLevelDirectives.end();
       DirIt++) {

    llvm::errs() << "### Handling " << GetLocation((*DirIt)->ChildRange.getBegin(), *(*DirIt)->CI) << " ###\n";

    int count = getMaxCachesRequired(*DirIt);

    if (count > max) {
      max = count;
    }

  }

  return max;

}

// Private
int DirectiveList::getMaxCachesRequired(StackItem * SI) {

  switch(SI->TYPE) {
  case StackItem::FunctionCallType:
    assert(false && "Didn't Expect Function Call Type");
    break;
  case StackItem::SpeculativeFunctionType:
    llvm::errs() << "--- Handling " << ((SpeculativeFunction *)SI)->TheFunction->getName() << " ---\n";
    break;
  case StackItem::FullDirectiveType:
    llvm::errs() << "--- Handling " << GetLocation(SI->ChildRange.getBegin(), *SI->CI) << " ---\n";
    break;
  default:
    assert(false && "Unknown Item Type");
  }

  int total = SI->ReadDecls.size() + SI->WriteDecls.size() - SI->ReadOnlyDecls.size();

  map<CallExpr *, FunctionCall *>::iterator CallIt;

  for (CallIt = AllCalls.begin(); CallIt != AllCalls.end(); CallIt++) {

    CallExpr * TheCall = CallIt->first;
    FunctionCall * TheFunc = CallIt->second;

    if (tools::IsChild(TheCall, SI->S)) {

      if (TheFunc->CachesRequired == -1) {
        map<FunctionDecl *, SpeculativeFunction *>::iterator FuncIt;
        FuncIt = AllSpeculativeFunctions.find(TheFunc->TheFunction);
        assert(FuncIt != AllSpeculativeFunctions.end());
        total += getMaxCachesRequired(FuncIt->second);
      } else {
        total += TheFunc->CachesRequired;
      }

    }

  }

  SI->CachesRequired = total;

  return total;

}

// Public
void DirectiveList::GenerateReadOnly() {


  list<FullDirective *>::iterator DirIt;
  for (DirIt = TopLevelDirectives.begin();
       DirIt != TopLevelDirectives.end();
       DirIt++) {

    set<FunctionDecl *> AllFuncs = globals::GetAllFunctionDecls();
    set<FunctionDecl *>::iterator FuncIt;

    llvm::errs() << "### Handling "
                 << GetLocation((*DirIt)->ChildRange.getBegin(), *(*DirIt)->CI)
                 << " ###\n";

    for (FuncIt = AllFuncs.begin(); FuncIt != AllFuncs.end(); FuncIt++) {
      if (tools::IsChild((*DirIt)->S, (*FuncIt)->getBody())) {
        break;
      }
    }

    assert(FuncIt != AllFuncs.end());

    GenerateReadOnly(*DirIt, *FuncIt);

  }

  map<FunctionDecl *, SpeculativeFunction *>::iterator FuncIt;

  for (FuncIt = AllSpeculativeFunctions.begin();
       FuncIt != AllSpeculativeFunctions.end();
       FuncIt++) {

    llvm::errs() << "### Handling " << FuncIt->first->getNameAsString() << " "
                 << GetLocation(FuncIt->second->ChildRange.getBegin(), *FuncIt->second->CI)
                 << " ###\n";

    GenerateReadOnly(FuncIt->second);

  }

}

// Private
void DirectiveList::GenerateReadOnly(FullDirective * Item,
                                     FunctionDecl * Parent) {

  FunctionTracker * FT = TrackedVars->GetTracker(Parent);

  set<NamedDecl *>::iterator ReadIt;

  for (ReadIt = Item->ReadDecls.begin();
       ReadIt != Item->ReadDecls.end();
       ReadIt++) {

    NamedDecl * D = *ReadIt;
    llvm::errs() << "\tLooking For: " << D->getNameAsString() << "\n";

    if (IsReadOnly(D, FT, Item, TrackedVars)) {
      Item->ReadOnlyDecls.insert(D);
    }

  }

}

// Private
void DirectiveList::GenerateReadOnly(SpeculativeFunction * Item) {

  FunctionTracker * FT = TrackedVars->GetTracker(Item->TheFunction);

  set<NamedDecl *>::iterator ReadIt;

  for (ReadIt = Item->ReadDecls.begin();
       ReadIt != Item->ReadDecls.end();
       ReadIt++) {

    NamedDecl * D = *ReadIt;
    llvm::errs() << "\tLooking For: " << D->getNameAsString() << "\n";

    // Need to find all top level directives for this function
    set<FullDirective *> TopDirectives = GetTopLevelDirectives(Item);

    set<FullDirective *>::iterator Dit;

    for (Dit = TopDirectives.begin();
         Dit != TopDirectives.end();
         Dit++) {

      if (!IsReadOnly(D, FT, Item, TrackedVars)) {
        break;
      }

    }

    if (Dit == TopDirectives.end()) {
      Item->ReadOnlyDecls.insert(D);
    }

  }

}

// Private
bool DirectiveList::IsReadOnly(NamedDecl * D,
                               FunctionTracker * FT,
                               StackItem * Item,
                               DeclTracker * TrackedVars) {

  if (TrackedVars->ContainsMatch(D, FT, Item->WriteDecls)) {
    return false;
  }

  map<CallExpr *, FunctionCall *>::iterator CallIt;

  for (CallIt = AllCalls.begin(); CallIt != AllCalls.end(); CallIt++) {

    CallExpr * TheCall = CallIt->first;
    FunctionCall * TheFunc = CallIt->second;

    if (tools::IsChild(TheCall, Item->S)) {

      map<FunctionDecl *, SpeculativeFunction *>::iterator FuncIt;
      FuncIt = AllSpeculativeFunctions.find(TheFunc->TheFunction);
      assert(FuncIt != AllSpeculativeFunctions.end());

      if (!IsReadOnly(D, FT, FuncIt->second, TrackedVars)) {
        return false;
      }

    }

  }

  return true;

}

// Public
bool DirectiveList::IsReadOnly(NamedDecl * D) {

  assert(D);
  D = globals::GetNamedDecl(D);

  StackItem * Current = CurrentDirectives.front();

  while (Current->Parent != NULL) {
    Current = Current->Parent;
  }

  return Current->ReadOnlyDecls.find(D) != Current->ReadOnlyDecls.end();

}

// Private
set<FullDirective *> DirectiveList::GetTopLevelDirectives(SpeculativeFunction * Item) {

  set<FullDirective *> Output;

  map<CallExpr *, FunctionCall *>::iterator CallIt;

  for (CallIt = AllCalls.begin(); CallIt != AllCalls.end(); CallIt++) {

    if (CallIt->second->TheFunction == Item->TheFunction) {

      StackItem * Current = CallIt->second;

      while (Current->Parent != NULL) {

        Current = Current->Parent;

      }

      if (FullDirective::ClassOf(Current)) {
        Output.insert((FullDirective *) Current);
      }

    }

  }

  return Output;

}

void DirectiveList::printTopLevelDeclAccess() {

  list<StackItem *> StartPoints = GetHandlerStartPoints();
  list<StackItem *>::iterator It;

  for (It = StartPoints.begin(); It != StartPoints.end(); It++) {
    printDeclAccess(*It);
  }


}

void DirectiveList::printDeclAccess(StackItem * D) {

  llvm::errs() << "\t### Displaying ";

  if (SpeculativeFunction::ClassOf(D)) {
    llvm::errs() << ((SpeculativeFunction *) D)->TheFunction->getNameAsString()
                 << " ";
  }

  llvm::errs() << GetLocation(D->ChildRange.getBegin(), *D->CI)
               << " ###\n";

  set<NamedDecl *>::iterator It;

  llvm::errs() << "Reads: ";
  for (It = D->ReadDecls.begin(); It != D->ReadDecls.end(); It++) {
    if (It != D->ReadDecls.begin()) {
      llvm::errs() << ", ";
    }
    llvm::errs() << (*It)->getNameAsString() << " (" << (int) *It << ")";
  }
  llvm::errs() << "\n";

  llvm::errs() << "Writes: ";
  for (It = D->WriteDecls.begin(); It != D->WriteDecls.end(); It++) {
    if (It != D->WriteDecls.begin()) {
      llvm::errs() << ", ";
    }
    llvm::errs() << (*It)->getNameAsString() << " (" << (int) *It << ")";
  }
  llvm::errs() << "\n";

  llvm::errs() << "ReadOnly: ";
  for (It = D->ReadOnlyDecls.begin(); It != D->ReadOnlyDecls.end(); It++) {
    if (It != D->ReadOnlyDecls.begin()) {
      llvm::errs() << ", ";
    }
    llvm::errs() << (*It)->getNameAsString() << " (" << (int) *It << ")";
  }
  llvm::errs() << "\n";

}


} // End namespace speculation
