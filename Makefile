##===- tools/extra/Makefile --------------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

CLANG_LEVEL := ../..

include $(CLANG_LEVEL)/../../Makefile.config

PARALLEL_DIRS := code-converter

include $(CLANG_LEVEL)/Makefile

