#######################################################################
#
#    Makefile Variables for the x86-unknown-linux targets
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

##########################################################################
#    tools
##########################################################################
BLT_CROSS_PREFIX=arm-linux-

BLT_COMPILE_C    = $(BLT_CROSS_PREFIX)gcc
BLT_FLAGS_C      = 
BLT_DEFINES_C    = -D_REENTRANT -D_BSD_SOURCE -D_POSIX_SOURCE -D_GNU_SOURCE -DBLT_TARGET=$(BLT_TARGET)
BLT_WARNINGS_C   = -Wall -W -Wundef -Wmissing-prototypes -Wmissing-declarations -Wno-long-long
BLT_AUTODEP_C    = $(BLT_CROSS_PREFIX)gcc -MM
BLT_LINK_C       = $(BLT_CROSS_PREFIX)gcc

BLT_COMPILE_CPP  =$(BLT_CROSS_PREFIX)g++
BLT_FLAGS_CPP    = -ansi -pedantic
BLT_DEFINES_CPP  = -D_REENTRANT -D_BSD_SOURCE -D_POSIX_SOURCE -DBLT_TARGET=$(BLT_TARGET)
BLT_WARNINGS_CPP = -Werror -Wall -W -Wundef -Wmissing-prototypes -Wno-long-long
BLT_AUTODEP_CPP  = $(BLT_CROSS_PREFIX)gcc -MM
BLT_LINK_CPP     = $(BLT_CROSS_PREFIX)g++
BLT_LIBRARIES_CPP = -lpthread

BLT_ARCHIVE_O      = $(BLT_CROSS_PREFIX)ld -r -o
BLT_ARCHIVE_A      = $(BLT_CROSS_PREFIX)ar rs

BLT_COPY_IF_NEW  = cp -u
BLT_MAKE_FLAGS   = --no-print-directory 

### dmalloc support
ifneq ($(DMALLOC_OPTIONS),)
BLT_DEFINES_C     += -DDMALLOC
BLT_DEFINES_CPP   += -DDMALLOC
BLT_LIBRARIES_C   += -ldmalloc
BLT_LIBRARIES_CPP += -ldmallocthcxx
BLT_FLAGS_CPP     := -ansi
endif
