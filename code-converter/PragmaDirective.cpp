//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "PragmaDirective.h"

namespace speculation {

PragmaDirective::PragmaDirective() : Parallel(false),
                                     ThreadPrivate(false),
                                     NoWait(false),
                                     ParaConstruct(),
                                     MainConstruct(), 
                                     Clauses() { }

void PragmaDirective::insertConstruct(PragmaConstruct c) {

  if (c.Type == ParallelConstruct) {
    Parallel = true;
    ParaConstruct = c;
  } else if (c.Type == ThreadprivateConstruct) {
    ThreadPrivate = true;
  }
  
  MainConstruct = c;

}

void PragmaDirective::insertClause(PragmaClause c) {

  Clauses.push_back(c);

  if (c.Type == NowaitClause) {
    NoWait = true;
  }

}

void PragmaDirective::setRange(SourceRange Range) {
  this->Range = Range;
}

set<IdentifierInfo *> PragmaDirective::getPrivateIdentifiers() {

  set<IdentifierInfo *> Vars;
  
  vector<PragmaClause>::iterator it;
  
  for (it = Clauses.begin(); it != Clauses.end(); it++) {

    if (it->Type == PrivateClause
        || it->Type == ReductionClause
        || it->Type == ThreadprivateClause) {
      
      vector<IdentifierInfo *>::iterator PrivIt;
      
      for (PrivIt = it->Options.begin(); PrivIt != it->Options.end(); PrivIt++) {
      
        Vars.insert(*PrivIt);
      
      }
      
    }

  }
  
  return Vars;

}

bool PragmaDirective::isParallel() {
  return Parallel;
}

bool PragmaDirective::isThreadprivate() {
  return ThreadPrivate;
}

bool PragmaDirective::isNowait() {
  return NoWait;
}

bool PragmaDirective::supportedConstruct(ConstructType Type) {
  return Type >= ParallelConstruct && Type < AtomicConstruct;
}

bool PragmaDirective::supportedClause(ClauseType Type) {
  return Type >= PrivateClause && Type < FirstprivateClause;
}

string PragmaDirective::getConstructTypeString(ConstructType Type) {

  switch(Type) {
   case ParallelConstruct:
    return "ParallelConstruct";
   case ForConstruct:
    return "ForConstruct";
   case SingleConstruct:
    return "SingleConstruct";
   case MasterConstruct:
    return "MasterConstruct";
   case CriticalConstruct:
    return "CriticalConstruct";
   case BarrierConstruct:
    return "BarrierConstruct";
   case ThreadprivateConstruct:
    return "ThreadprivateConstruct";
   case FlushConstruct:
    return "FlushConstruct";
   case AtomicConstruct:
    return "AtomicConstruct";
   case OrderedConstruct:
    return "OrderedConstruct";
   case SectionConstruct:
    return "SectionConstruct";
   case SectionsConstruct:
    return "SectionsConstruct";
   case TaskConstruct:
    return "TaskConstruct";
   case TaskwaitConstruct:
    return "TaskwaitConstruct";
   case TaskyieldConstruct:
    return "TaskyieldConstruct";
   default:
    return "<Unrecognised Construct>";
  }

}

string PragmaDirective::getClauseTypeString(ClauseType Type) {

  switch(Type) {
   case PrivateClause:
    return "PrivateClause";
   case SharedClause:
    return "SharedClause";
   case ReductionClause:
    return "ReductionClause";
   case NowaitClause:
    return "NowaitClause";
   case CopyinClause:
    return "CopyinClause";
   case ScheduleClause:
    return "ScheduleClause";
   case FirstprivateClause:
    return "FirstprivateClause";
   case LastprivateClause:
    return "LastprivateClause";
   case OrderedClause:
    return "OrderedClause";
   case CollapseClause:
    return "CollapseClause";
   case IfClause:
    return "IfClause";
   case NumThreadsClause:
    return "NumThreadsClause";
   case DefaultClause:
    return "DefaultClause";
   case FinalClause:
    return "FinalClause";
   case UntiedClause:
    return "UntiedClause";
   case MergeableClause:
    return "MergeableClause";
   case CopyprivateClause:
    return "CopyprivateClause";
   case ThreadprivateClause:
    return "ThreadprivateClause";
   default:
    return "<Unrecognised Clause>";
  }

}

ConstructType PragmaDirective::getConstructType(string Type) {

  if (Type == "parallel") {
    return ParallelConstruct;
  } else if (Type == "for") {
    return ForConstruct;
  } else if (Type == "single") {
    return SingleConstruct;
  } else if (Type == "master") {
    return MasterConstruct;
  } else if (Type == "critical") {
    return CriticalConstruct;
  } else if (Type == "barrier") {
    return BarrierConstruct;
  } else if (Type == "threadprivate") {
    return ThreadprivateConstruct;
  } else if (Type == "flush") {
    return FlushConstruct;
  } else if (Type == "atomic") {
    return AtomicConstruct;
  } else if (Type == "ordered") {
    return OrderedConstruct;
  } else if (Type == "section") {
    return SectionConstruct;
  } else if (Type == "sections") {
    return SectionsConstruct;
  } else if (Type == "task") {
    return TaskConstruct;
  } else if (Type == "taskwait") {
    return TaskwaitConstruct;
  } else if (Type == "taskyield") {
    return TaskyieldConstruct;
  } else {
    return UnknownConstruct;
  }

}

ClauseType PragmaDirective::getClauseType(string Type) {

  if (Type == "private") {
    return PrivateClause;
  } else if (Type == "shared") {
    return SharedClause;
  } else if (Type == "reduction") {
    return ReductionClause;
  } else if (Type == "nowait") {
    return NowaitClause;
  } else if (Type == "copyin") {
    return CopyinClause;
  } else if (Type == "schedule") {
    return ScheduleClause;
  } else if (Type == "firstprivate") {
    return FirstprivateClause;
  } else if (Type == "lastprivate") {
    return LastprivateClause;
  } else if (Type == "ordered") {
    return OrderedClause;
  } else if (Type == "collapse") {
    return CollapseClause;
  } else if (Type == "if") {
    return IfClause;
  } else if (Type == "num_threads") {
    return NumThreadsClause;
  } else if (Type == "default") {
    return DefaultClause;
  } else if (Type == "final") {
    return FinalClause;
  } else if (Type == "untied") {
    return UntiedClause;
  } else if (Type == "mergeable") {
    return MergeableClause;
  } else if (Type == "copyprivate") {
    return CopyprivateClause;
  } else if (Type == "threadprivate") {
    return ThreadprivateClause;
  }else {
    return UnknownClause;
  }

}

} // End namespace "speculation"
