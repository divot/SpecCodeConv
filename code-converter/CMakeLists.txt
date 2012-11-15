# TODO: FIX THIS TO ONLY THE ONES IT NEEDS
set(LLVM_LINK_COMPONENTS
    ${LLVM_TARGETS_TO_BUILD}
    asmparser
    bitreader
    bitwriter
    codegen
    instrumentation
    ipo
    linker
    selectiondag
   )


add_clang_executable(SpecCodeConv
                     Main.cpp
                     Tools.cpp
                     Globals.cpp
                     PragmaDirective.cpp
                     OMPPragmaHandler.cpp
                     BaseASTConsumer.cpp
                     DirectiveFinder.cpp
                     DeclExtractor.cpp
                     DeclTracker.cpp
                     NoEditStmtPrinter.cpp
                     StmtPrinter.cpp
                     DirectiveList.cpp
                     VarCollector.cpp
                     VarTraverser.cpp
                     DirectiveHandler.cpp
                     FakeDirectiveHandler.cpp
                     DeclLinker.cpp
                    )

# TODO: FIX THIS TO ONLY THE ONES IT NEEDS
target_link_libraries(SpecCodeConv
                      clangFrontendTool
                      clangAST
                      clangAnalysis
                      clangBasic
                      clangCodeGen
                      clangDriver
                      clangEdit
                      clangFrontend
                      clangLex
                      clangParse
                      clangEdit
                      clangARCMigrate
                      clangRewriteCore
                      clangRewriteFrontend
                      clangSema
                      clangSerialization
                      clangStaticAnalyzerFrontend
                      clangStaticAnalyzerCheckers
                      clangStaticAnalyzerCore
                     )

set_target_properties(SpecCodeConv
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      PREFIX "")

install(TARGETS SpecCodeConv RUNTIME DESTINATION bin)

