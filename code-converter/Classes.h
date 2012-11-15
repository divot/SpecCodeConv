//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#ifndef _CLASSES_H_
#define _CLASSES_H_

#include <assert.h>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using std::list;
using std::make_pair;
using std::map;
using std::multimap;
using std::pair;
using std::set;
using std::string;
using std::stringstream;
using std::vector;

namespace speculation {

class BaseASTConsumer;
class BasePluginASTAction;
class DeclExtractor;
class DeclRefExtractor;
class DeclTracker;
class DirectiveFinder;
class DirectiveHandler;
class DirectiveList;
class FunctionCallList;
class NoEditStmtPrinter;
class OMPPragmaHandler;
class PragmaDirective;
class VarCollector;
class VarTraverser;

struct PragmaClause;
struct PragmaConstruct;

struct StackItem;
struct FullDirective;
struct FunctionCall;
struct SpeculativeFunction;

struct FunctionTracker;
struct FunctionCallTracker;

typedef map<unsigned, PragmaDirective *> PragmaDirectiveMap;

} // End namespace speculation

#endif
