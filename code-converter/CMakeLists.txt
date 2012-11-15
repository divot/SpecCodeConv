set(LLVM_LINK_COMPONENTS support mc)
set(LLVM_USED_LIBS
    clangFrontend
    clangAST
    clangRewrite
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
                     NoEditStmtPrinter.cpp
                     StmtPrinter.cpp
                     DirectiveList.cpp
                     FunctionCallList.cpp
                     DeclRefExtractor.cpp
                     VarCollector.cpp
                     VarTraverser.cpp
                     DirectiveHandler.cpp
                    )

set_target_properties(SpecCodeConv
                      PROPERTIES
                      LINKER_LANGUAGE CXX
                      PREFIX "")

install(TARGETS SpecCodeConv RUNTIME DESTINATION bin)

