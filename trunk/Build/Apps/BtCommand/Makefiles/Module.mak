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
BLT_CLIENT_TYPE = CPP
BLT_IMPORT_MODULES = Atomix Neptune BlueTune
include $(BLT_BUILD_INCLUDES)/ImportModuleDeps.mak

#######################################################################
# sources and object files
#######################################################################
BLT_MODULE_SOURCES = BtCommand.cpp
BLT_MODULE_OBJECTS = $(BLT_MODULE_SOURCES:.cpp=.o)

#######################################################################
# paths
#######################################################################
VPATH += $(BLT_SOURCE_ROOT)/Apps/BtCommand

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a btcommand

#######################################################################
# targets
#######################################################################
btcommand: $(BLT_IMPORTED_MODULE_LIBS) $(BLT_MODULE_OBJECTS) $(BLT_MODULE_LIBRARIES)
	$(BLT_MAKE_BANNER_START)
	$(BLT_MAKE_EXECUTABLE_COMMAND_CPP)
	$(BLT_MAKE_BANNER_END)

