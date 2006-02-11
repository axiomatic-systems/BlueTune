#######################################################################
#
#    BtPlay Module Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# module dependencies
#######################################################################
BLUETUNE_CLIENT_TYPE = C
BLT_IMPORT_MODULES = Atomix BlueTune
include $(BLT_BUILD_INCLUDES)/ImportModuleDeps.mak

#######################################################################
# sources and object files
#######################################################################
BLT_MODULE_SOURCES = BtPlay.c
BLT_MODULE_OBJECTS = $(BLT_MODULE_SOURCES:.c=.o)

#######################################################################
# paths
#######################################################################
VPATH += $(BLT_SOURCE_ROOT)/Apps/BtPlay

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a btplay

#######################################################################
# targets
#######################################################################
btplay: $(BLT_IMPORTED_MODULE_LIBS) $(BLT_MODULE_OBJECTS) $(BLT_MODULE_LIBRARIES)
	$(BLT_MAKE_BANNER_START)
	$(BLT_MAKE_EXECUTABLE_COMMAND_C)
	$(BLT_MAKE_BANNER_END)

