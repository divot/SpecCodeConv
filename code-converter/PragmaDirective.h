//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _PRAGMALIST_H_
#define _PRAGMALIST_H_

#include "Classes.h"

#include "clang/AST/AST.h"
#include "clang/Lex/Preprocessor.h"

using clang::IdentifierInfo;
using clang::SourceRange;
using clang::Token;

namespace speculation {

enum ConstructType {
  ParallelConstruct,  // Implied Barrier
  ForConstruct,       // Implicit Barrier
  SingleConstruct,    // Implicit Barrier
  MasterConstruct,    // No barrier!
  CriticalConstruct,  // No barrier!
  BarrierConstruct,   // It already is a barrier!
  ThreadprivateConstruct, // No barrier!
// Ignored
  FlushConstruct, 
// Not Supported
  AtomicConstruct,
  OrderedConstruct,
  SectionConstruct,
  SectionsConstruct, // Implicit Barrier
  TaskConstruct,
  TaskwaitConstruct,
  TaskyieldConstruct,
  UnknownConstruct
};

enum ClauseType {
  PrivateClause,
  SharedClause,
  ReductionClause,
  NowaitClause,
  ThreadprivateClause,
// Ignored
  CopyinClause,
  ScheduleClause,
// Not Supported
  FirstprivateClause,
  LastprivateClause,
  OrderedClause,
  CollapseClause,
  IfClause,
  NumThreadsClause,
  DefaultClause,
  FinalClause,
  UntiedClause,
  MergeableClause,
  CopyprivateClause,
  UnknownClause
};

struct PragmaConstruct {
  ConstructType Type;
  SourceRange Range;
};

struct PragmaClause {
  ClauseType Type;
  SourceRange Range;
  Token Op;
  vector<IdentifierInfo *> Options;
};

class PragmaDirective {

 public:
 
  bool Parallel;
  bool ThreadPrivate;
  bool NoWait;
  PragmaConstruct ParaConstruct;
  PragmaConstruct MainConstruct;
  vector<PragmaClause> Clauses;
  SourceRange Range;
 
  PragmaDirective();
  
  void insertConstruct(PragmaConstruct c);
  void insertClause(PragmaClause c);
  void setRange(SourceRange Range);
  
  set<IdentifierInfo *> getPrivateIdentifiers();
  
  bool isParallel();
  bool isThreadprivate();
  bool isNowait();
  
  static bool supportedConstruct(ConstructType Type);
  static bool supportedClause(ClauseType Type);
  
  static string getConstructTypeString(ConstructType Type);
  static string getClauseTypeString(ClauseType Type);

  static ConstructType getConstructType(string type);
  static ClauseType getClauseType(string type);
  
};

} // End namespace "speculation"

#endif
