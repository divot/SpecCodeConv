SpecCodeConv
============

SpecCodeConv takes C programs marked up with OpenMP syntax containing
potentially unsafely parallelised sections. It performs analysis on the variable
accesses in an attempt to detect which variables/parallel sections are safe. For
those that aren't, runtime tracking code of loads and stores is inserted, and
checks are performed at any barrier to detect any cross-thread dependencies that
may have occurred and handle them appropriately.

Unfortunately due to copyright and anonymous peer review issues, the code for
tracking, dependence detection and handling cannot be shared at this time but
will be released in the future.



The execution sequence is as follows:

  * Build the AST
  - For each source file provided, read and build its AST.
  - Log all OpenMP pragma statements found.
  - Log any global variables defined.
  - Perform basic linking between source files of global variables and 
    functions.
    
  * Analyse Variable and Pointer Accesses for Safety
  - Log which variables have been defined as thread private.
  - Build a list of which variables potentially share pointers.
  - Detect which declared thread private variables might share pointers to other
    threads (private contamination).
  - Detect which variables are read only during parallel regions.

  * Add Speculation Code
  - Insert variable read/write Tracking for shared written variables
  - Insert pre/post region statements
  - Insert speculative checks at any barrier
  - Insert includes and setup code

  
Future Work:

  - Handle recursive functions
  - Checkpointing of program status (backup/restore on detected dependence)
  - Create backup of input files
  - Better diagnostics
  - Modified insertion of tracking code to allow for compiler optimisations
  


As this program is built on top of libClang, the source trees for LLVM and Clang
are required. Due to potential conflicts with differing versions, and minor 
additions to LLVM and Clang, versions expected to be used for SpecCodeConv have
been forked into their own repositories. These follow the main repositories,
however are only synchronized after they have been checked.

At present, the standard LLVM tree has not been modified, however slight
modifications have been made to the Clang repository with respect to the
handling of Pragma statements. See bit.ly/QHrYPu for details.

Installation:

git clone https://github.com/divot/LLVMMirror.git llvm
cd llvm/tools
git clone https://github.com/divot/LLVMMirror.git -b SpecCodeConv clang
cd clang/tools
git clone https://github.com/divot/SpecCodeConv.git
cd ../../..

Then perform the remaining installation process from step 6 onwards of
http://clang.llvm.org/get_started.html

Usage:

  Warning: This program will overwrite the sources provided to it.
  Make backups prior to use.

SpecCodeConv [-I [dir]] file1.c [file2.c ...]

