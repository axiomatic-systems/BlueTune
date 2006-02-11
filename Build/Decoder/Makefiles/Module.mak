#######################################################################
#
#    Module Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# module dependencies
#######################################################################
BLT_INCLUDE_MODULES := Atomix
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak
BLT_IMPORT_MODULES := Core Plugins
include $(BLT_BUILD_INCLUDES)/ImportModuleDeps.mak

#######################################################################
# sources and object files
#######################################################################
BLT_MODULE_SOURCES = \
	BltDecoder.c

BLT_MODULE_OBJECTS = $(BLT_MODULE_SOURCES:.c=.o)

#######################################################################
# paths
#######################################################################
VPATH += $(BLT_SOURCE_ROOT)/Decoder

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a

#######################################################################
# targets
#######################################################################
libBltDecoder.a: $(BLT_IMPORTED_MODULE_LIBS) $(BLT_MODULE_OBJECTS) $(BLT_MODULE_LIBRARIES)

