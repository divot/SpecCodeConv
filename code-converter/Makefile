##===- tools/SpecCodeConv/Makefile -------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

CLANG_LEVEL := ../../..

TOOLNAME = SpecCodeConv
NO_INSTALL = 1

# No plugins, optimize startup time.
TOOL_NO_EXPORTS = 1

include $(CLANG_LEVEL)/../../Makefile.config
LINK_COMPONENTS := all
USEDLIBS = clangFrontend.a clangSerialization.a clangDriver.a \
           clangTooling.a clangParse.a clangSema.a clangAnalysis.a \
           clangEdit.a clangAST.a clangLex.a clangBasic.a \
           clangRewriteFrontend.a clangRewriteCore.a

include $(CLANG_LEVEL)/Makefile

