#!/bin/bash

rm -rf Makefile.in Makefile libtool ltmain.sh install-sh depcomp configure config.sub config.status config.log config.h.i* compile config.h.in config.h config.guess compile autom4te.cache aclocal.m4 source/Makefile.in source/Makefile board/brcm/Makfile board/brcm/Makefile.in stamp-h1 missing ar-lib doc/Doxyfile
find . -name ".libs" -exec rm -rf {} \;&
find . -name ".deps" -exec rm -rf {} \;&
find . -name "*.lo" -exec rm -rf {} \;&
find . -name "*.o" -exec rm -rf {} \;&
find . -name "*.la" -exec rm -rf {} \;&
find . -name "cscope.out" -exec rm -rf {} \;&
find . -name "tags" -exec rm -rf {} \;&

