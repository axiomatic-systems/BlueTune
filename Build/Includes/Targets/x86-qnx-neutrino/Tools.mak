#######################################################################
#
#    Makefile Variables for the x86-qnx-neutrino target
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

##########################################################################
#    tools
##########################################################################
BLT_COMPILE_C   = gcc
BLT_FLAGS_C     = -ansi -pedantic
BLT_DEFINES_C   = -D_BSD_SOURCE -D_POSIX_SOURCE -D_QNX_SOURCE -DBLT_TARGET=$(BLT_TARGET)
BLT_WARNINGS_C  = -Werror -Wall -W -Wundef -Wmissing-prototypes -Wmissing-declarations -Wno-long-long
BLT_AUTODEP_C   = gcc -MM
BLT_ARCHIVE     = ld -r 
BLT_LINK_C      = gcc -lsocket -lm

BLT_COMPILE_CPP  = g++
BLT_FLAGS_CPP    = -ansi -pedantic
BLT_DEFINES_CPP  = -D_REENTRANT -D_BSD_SOURCE -D_POSIX_SOURCE -DBLT_TARGET=$(BLT_TARGET)
BLT_WARNINGS_CPP = -Werror -Wall -W -Wundef -Wmissing-prototypes -Wmissing-declarations -Wno-long-long
BLT_AUTODEP_CPP  = gcc -MM
BLT_LINK_CPP     = g++ -lsocket -lm

BLT_ARCHIVE_O      = ld -r -o
BLT_ARCHIVE_A      = ar rs

BLT_COPY_IF_NEW = cp -n
BLT_MAKE_FLAGS   = --no-print-directory 
