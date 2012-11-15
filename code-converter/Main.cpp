//=============================================================================
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//=============================================================================

#include "BaseASTConsumer.h"
#include "DeclLinker.h"
#include "DeclTracker.h"
#include "DirectiveFinder.h"
#include "DirectiveHandler.h"
#include "DirectiveList.h"
#include "FakeDirectiveHandler.h"
#include "Globals.h"
#include "OMPPragmaHandler.h"
#include "PragmaDirective.h"
#include "VarCollector.h"
#include "Tools.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Parse/Parser.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Host.h"

using clang::ASTConsumer;
using clang::ASTContext;
using clang::CompilerInstance;
using clang::DiagnosticBuilder;
using clang::DiagnosticOptions;
using clang::DiagnosticsEngine;
using clang::FileEntry;
using clang::HeaderSearchOptions;
using clang::Parser;
using clang::TargetInfo;
using clang::TargetOptions;
using clang::Token;
using clang::VarDecl;

using clang::FunctionDecl;

using llvm::cast;
using llvm::dyn_cast;

//namespace cl = llvm::cl;

using namespace speculation;

using std::string;

llvm::cl::list<string> InputFilenames(llvm::cl::Positional,
                                      llvm::cl::desc("<Input files>"),
                                      llvm::cl::OneOrMore);

llvm::cl::list<string> IncludeDirectories("I",
                                          llvm::cl::desc("Include a Directory"),
                                          llvm::cl::ZeroOrMore);

string sysincludes[] = {
  "/usr/local/include",
  "/home/s0347677/clang/out/bin/../lib/clang/3.2/include",
  "/home/divot/ssd/clang/out/bin/../lib/clang/3.2/include",
  "/usr/lib/gcc/i686-linux-gnu/4.6/include/"
};

string externcincludes[] = {
  "/usr/include/x86_64-linux-gnu",
  "/usr/include/i386-linux-gnu",
  "/include",
  "/usr/include"
};

template <typename T, size_t N>
inline size_t SizeOfArray( const T(&)[ N ] )
{ return N; }

void addSysIncludes(HeaderSearchOptions &headopts) {

  for (unsigned i = 0; i < SizeOfArray(sysincludes); i++) {
    headopts.AddPath(sysincludes[i], clang::frontend::System, false, 
                     false, false, true);
  }

  for (unsigned i = 0; i < SizeOfArray(externcincludes); i++) {
    headopts.AddPath(externcincludes[i], clang::frontend::System, false, 
                     false, false, true, true);
  }

}

int main(int argc, char *argv[]) {

  llvm::cl::ParseCommandLineOptions(argc, argv);
  
  CompilerInstance *CIs = new CompilerInstance[InputFilenames.size()];

  map<CompilerInstance *, PragmaDirectiveMap> Directives;
  map<CompilerInstance *, vector<Decl *> > AllDecls;
  DeclTracker TrackedVars;
  DirectiveList FullDirectives(&TrackedVars);
  
  map<CompilerInstance *, string> FilenameMap;

  // First load all of the AST's and extract the top level Decls.

  llvm::errs() << "\n";
  llvm::errs() << "#####################\n";
  llvm::errs() << "### Parsing Files ###\n";
  llvm::errs() << "#####################\n";
  llvm::errs() << "\n";

  for (unsigned i = 0; i < InputFilenames.size(); i++) {
    llvm::errs() << "\tParsing: " << InputFilenames[i] << "\n";
    FilenameMap.insert(make_pair(&CIs[i], string(InputFilenames[i])));

    CompilerInstance &CI = CIs[i];

    AllDecls.insert(make_pair(&CI, vector<Decl *>()));
    Directives.insert(make_pair(&CI, PragmaDirectiveMap()));

    CI.createDiagnostics(0,NULL);
    DiagnosticsEngine &Diags = CI.getDiagnostics();

    DiagnosticOptions &DiagOpts = CI.getDiagnosticOpts();
    DiagOpts.ShowColors = 1;
    
    HeaderSearchOptions &headopts = CI.getHeaderSearchOpts();
    addSysIncludes(headopts);

    for (unsigned j = 0; j < IncludeDirectories.size(); j++) {

      headopts.AddPath(StringRef(IncludeDirectories[j]),
                       clang::frontend::Quoted,
                       true,
                       false,
                       false);

    }

    TargetOptions to;
    to.Triple = llvm::sys::getDefaultTargetTriple();
    TargetInfo *pti = TargetInfo::CreateTargetInfo(Diags, to);
    CI.setTarget(pti);

    CI.createFileManager();
    CI.createSourceManager(CI.getFileManager());
    CI.createPreprocessor();
    CI.getPreprocessorOpts().UsePredefines = false;

    OMPPragmaHandler *PH = new OMPPragmaHandler(Directives[&CI], Diags);
    CI.getPreprocessor().AddPragmaHandler(PH);

    BaseASTConsumer *astConsumer = new BaseASTConsumer(AllDecls[&CI],
                                                       Diags);
    CI.setASTConsumer(astConsumer);

    CI.createASTContext();

    globals::RegisterCompilerInstance(CI);

    const FileEntry *pFile = CI.getFileManager().getFile(StringRef(InputFilenames[i]));
    
    if (!pFile) {
      Diags.Report(clang::diag::err_drv_no_such_file)
          << InputFilenames[i];
      continue;
    }
    
    CI.getSourceManager().createMainFileID(pFile);
    CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(),
                                             &CI.getPreprocessor());
    clang::ParseAST(CI.getPreprocessor(), astConsumer, CI.getASTContext());

  }



  llvm::errs() << "\n";
  llvm::errs() << "##########################\n";
  llvm::errs() << "### Extracting Globals ###\n";
  llvm::errs() << "##########################\n";
  llvm::errs() << "\n";

  // Next extract all of the globals and link any shared ones
  // While we're at it, record which of the VarDecls are threadprivate
  map<CompilerInstance *, vector<Decl *> >::iterator CIit;
  vector<Decl *> SkippedSystemDecls;

  // Loop over each CompilerInstance
  for (CIit = AllDecls.begin(); CIit != AllDecls.end(); CIit++) {

    CompilerInstance &CI = *(CIit->first);
    vector<Decl *> &Decls = CIit->second;

    string Filename = FilenameMap.find(&CI)->second;

    llvm::errs() << "\t### Extracting from " << Filename << " ###\n\n";

    // Loop over each decl within that CI
    vector<Decl *>::iterator it;
    for (it = Decls.begin(); it != Decls.end(); it++) {

      Decl * D = *it;
      
      // If it's imported from a system header we ignore it for the time being
      // If it finds a call to any of the methods in these decls, then it won't
      // find the function in the globals and throw some warnings
      if (CI.getSourceManager().isInSystemHeader(D->getLocation())) {
        SkippedSystemDecls.push_back(D);
        continue;
      }

      VarDecl * VD;
      FunctionDecl * FD;
      // Register all Global Vars and Functions with the translation tables
      if ((VD = dyn_cast<VarDecl>(D))) {

        globals::InsertVarDecl(VD, CI);

      } else if ((FD = dyn_cast<FunctionDecl>(D))) {

        globals::InsertFunctionDecl(FD, CI);

      }

    }

  }
  
  llvm::errs() << "\n";
  llvm::errs() << "###########################\n";
  llvm::errs() << "### Linking Extern Vars ###\n";
  llvm::errs() << "###########################\n";
  llvm::errs() << "\n";

  globals::LinkExternDecls();

  llvm::errs() << "\n";
  llvm::errs() << "#########################\n";
  llvm::errs() << "### Linking Functions ###\n";
  llvm::errs() << "#########################\n";
  llvm::errs() << "\n";

  globals::LinkExternFunctions();
  
  llvm::errs() << "\n";
  llvm::errs() << "#################################\n";
  llvm::errs() << "### Extracting Thread Private ###\n";
  llvm::errs() << "#################################\n";
  llvm::errs() << "\n";

  // Loop over each CompilerInstance
  for (CIit = AllDecls.begin(); CIit != AllDecls.end(); CIit++) {

    CompilerInstance &CI = *(CIit->first);

    PragmaDirectiveMap &Pragmas = Directives[&CI];
    PragmaDirectiveMap::iterator PIt;

    // Go through the pragma statements & mark relevant var decls threadprivate
    for (PIt = Pragmas.begin(); PIt != Pragmas.end(); PIt++) {

      if (PIt->second->isThreadprivate()) {
        set<IdentifierInfo *> Idents = PIt->second->getPrivateIdentifiers();

        set<IdentifierInfo *>::iterator IIit;

        for (IIit = Idents.begin(); IIit != Idents.end(); IIit++) {

          globals::InsertThreadPrivate((*IIit)->getName(), CI);

        }

      }

    }

  }

  llvm::errs() << "\n";
  llvm::errs() << "#########################\n";
  llvm::errs() << "### Tracking Pointers ###\n";
  llvm::errs() << "#########################\n";
  llvm::errs() << "\n";

  set<NamedDecl *> GlobalVars = globals::GetAllNamedDecls();
  set<NamedDecl *>::iterator GlobalIt;

  for (GlobalIt = GlobalVars.begin();
       GlobalIt != GlobalVars.end();
       GlobalIt++) {

    TrackedVars.AddGlobalDecl(dyn_cast<VarDecl>(*GlobalIt));

  }

  set<FunctionDecl *> AllFunctions = globals::GetAllFunctionDecls();
  set<FunctionDecl *>::iterator FuncIt;

  for (FuncIt = AllFunctions.begin();
       FuncIt != AllFunctions.end();
       FuncIt++) {

    TrackedVars.AddFunction(*FuncIt);

  }

  DeclLinker DLink(&TrackedVars);

  for (FuncIt = AllFunctions.begin();
       FuncIt != AllFunctions.end();
       FuncIt++) {

    if ((*FuncIt)->hasBody()) {
      DLink.HandleFunction(*FuncIt);
    }

  }

  TrackedVars.PropogateShares();

  TrackedVars.printAllFunctionsTrackers();

  // Now that we've set up translation tables between:
  //   Shared Global Variables
  //   Shared Functions
  //   ThreadPrivate Variables
  //   Compilation Instances
  // We need to go over each function definition looking for OMP statements
  // Possible ways of doing this:
  //   Recursive AST Visitor, finding matching Compound Statements
  //   Just traverse over every function
  //   Finder/Handler for new parallel blocks
  // First need to collect which variables are being tracked
  // And track which variables are being contaminated
  //   Can global threadprivate variables be contaminated permanently?
  //   Probably only if it's a pointer.
  //   But if it is a pointer, how do I go about making sure it's kept
  //   contaminated
  //   I think my global threadprivate status may need to include contamination
  //   tracking at the global level, irrelevant of which omp pragma's are 
  //   currently running
  //   What happens if an omp pragma calls private on a contaminated
  //   threadprivate?
  //   I need to treat threadprivate pointers as contaminated
  //   either that or traverse everything and see if they get contaminated
  
  llvm::errs() << "\n";
  llvm::errs() << "##########################\n";
  llvm::errs() << "### Finding Directives ###\n";
  llvm::errs() << "##########################\n";
  llvm::errs() << "\n";

  set<FunctionDecl *> ImplementedFunctions = globals::GetAllFunctionDecls();
  set<FunctionDecl *>::iterator FunctionIt;
  
  // Loop over each of the functions extracting the top level pragma statements
  for (FunctionIt = ImplementedFunctions.begin();
       FunctionIt != ImplementedFunctions.end();
       FunctionIt++) {
    
    CompilerInstance &CI = *globals::GetCompilerInstance(*FunctionIt);
    
    llvm::errs() << "\t### Searching in: " << (*FunctionIt)->getName() << " ###\n\n";

    if ((*FunctionIt)->hasBody()) {
      DirectiveFinder Finder(Directives[&CI], FullDirectives, CI);
      Finder.TraverseStmt((*FunctionIt)->getBody());
    } else {
      llvm::errs() << "\t\tFunction has no body!\n";
    }

  }
  
  llvm::errs() << "\n";
  llvm::errs() << "##################################\n";
  llvm::errs() << "### Scanning For Contamination ###\n";
  llvm::errs() << "##################################\n";
  llvm::errs() << "\n";

  list<FullDirective *> TopLevelDirectives = 
                                  FullDirectives.GetTopLevelDirectives();
  list<FullDirective *>::iterator DirectiveIt;

  VarCollector VC;
  // Loop over the top level pragma statements recording var contamination
  // Repeat until further contamination doesn't occur
  do {

    FullDirectives.ResetChangedStatus();

    for (DirectiveIt = TopLevelDirectives.begin();
         DirectiveIt != TopLevelDirectives.end();
         DirectiveIt++) {

      VC.HandleDirective(&Directives[(*DirectiveIt)->CI],
                         &FullDirectives,
                         *DirectiveIt);

    }

  } while(FullDirectives.GetChangedStatus());
  
  llvm::errs() << "\n";
  llvm::errs() << "####################################\n";
  llvm::errs() << "### Collating Call Contamination ###\n";
  llvm::errs() << "####################################\n";
  llvm::errs() << "\n";

  FullDirectives.GenerateSpecFunctions();

  llvm::errs() << "\n";
  llvm::errs() << "#####################################\n";
  llvm::errs() << "### Generating Read + Write Lists ###\n";
  llvm::errs() << "#####################################\n";
  llvm::errs() << "\n";

  list<StackItem *> HandlerStartPoints = FullDirectives.GetHandlerStartPoints();
  list<StackItem *>::iterator StartIt;

  FakeDirectiveHandler FH(&FullDirectives);
  for (StartIt = HandlerStartPoints.begin();
       StartIt != HandlerStartPoints.end();
       StartIt++) {

    SourceLocation Loc = (*StartIt)->S->getLocStart();
    CompilerInstance &CI = *((*StartIt)->CI);

    llvm::errs() << "\t### Handling " << tools::GetLocation(Loc, CI)
                 << " ###\n\n";

    FH.HandleStackItem(&Directives[(*StartIt)->CI], *StartIt);

  }

  llvm::errs() << "\n";
  llvm::errs() << "#######################################\n";
  llvm::errs() << "### Discovering Read-Only Variables ###\n";
  llvm::errs() << "#######################################\n";
  llvm::errs() << "\n";

  FullDirectives.GenerateReadOnly();

  llvm::errs() << "\nResults:\n";
  FullDirectives.printTopLevelDeclAccess();

  llvm::errs() << "\n";
  llvm::errs() << "######################################\n";
  llvm::errs() << "### Inserting Speculative Accesses ###\n";
  llvm::errs() << "######################################\n";
  llvm::errs() << "\n";

  DirectiveHandler H(&FullDirectives);
  for (StartIt = HandlerStartPoints.begin();
       StartIt != HandlerStartPoints.end();
       StartIt++) {

    SourceLocation Loc = (*StartIt)->S->getLocStart();
    CompilerInstance &CI = *((*StartIt)->CI);

    llvm::errs() << "\t### Handling " << tools::GetLocation(Loc, CI)
                 << " ###\n\n";

    H.HandleStackItem(&Directives[(*StartIt)->CI], *StartIt);

  }
  
  llvm::errs() << "\n";
  llvm::errs() << "########################\n";
  llvm::errs() << "### Inserting Checks ###\n";
  llvm::errs() << "########################\n";
  llvm::errs() << "\n";

  map<CompilerInstance *, PragmaDirectiveMap>::iterator PragmaIt;

  for (PragmaIt = Directives.begin();
       PragmaIt != Directives.end();
       PragmaIt ++) {

    H.InsertChecks(*PragmaIt->first, PragmaIt->second);

  }

  llvm::errs() << "\n";
  llvm::errs() << "######################\n";
  llvm::errs() << "### Inserting Init ###\n";
  llvm::errs() << "######################\n";
  llvm::errs() << "\n";

  H.InsertInit();

  llvm::errs() << "\n";
  llvm::errs() << "####################\n";
  llvm::errs() << "### Finishing Up ###\n";
  llvm::errs() << "####################\n";
  llvm::errs() << "\n";

  H.Finish();

  // Clean up
  for (unsigned i = 0; i < InputFilenames.size(); i++) {
    CIs[i].getDiagnosticClient().EndSourceFile();
  }


  return 0;
}

