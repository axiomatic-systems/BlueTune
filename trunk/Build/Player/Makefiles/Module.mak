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
BLT_INCLUDE_MODULES := Atomix Neptune
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak
BLT_IMPORT_MODULES = Decoder
include $(BLT_BUILD_INCLUDES)/ImportModuleDeps.mak

#######################################################################
# sources and object files
#######################################################################
BLT_MODULE_SOURCES = \
	BltPlayer.cpp BltDecoderClient.cpp BltDecoderServer.cpp

BLT_MODULE_OBJECTS = $(BLT_MODULE_SOURCES:.cpp=.o)

#######################################################################
# paths
#######################################################################
VPATH += $(BLT_SOURCE_ROOT)/Player

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a

#######################################################################
# targets
#######################################################################
libBltPlayer.a: $(BLT_IMPORTED_MODULE_LIBS) $(BLT_MODULE_OBJECTS) $(BLT_MODULE_LIBRARIES)

