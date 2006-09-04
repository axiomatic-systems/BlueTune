#######################################################################
#
#    Core Module Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# module dependencies
#######################################################################
BLT_INCLUDE_MODULES = Atomix
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

#######################################################################
# sources and object files
#######################################################################
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Config
BLT_MODULE_SOURCES = \
	BltInterfaces.c BltModule.c BltCore.c BltStream.c BltMediaNode.c \
	BltMediaPort.c BltRegistry.c BltMedia.c BltPcm.c BltMediaPacket.c \
	BltTime.c 

BLT_MODULE_OBJECTS = $(BLT_MODULE_SOURCES:.c=.o)

#######################################################################
# paths
#######################################################################
VPATH += $(BLT_SOURCE_ROOT)/Core

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a

#######################################################################
# targets
#######################################################################
libBltCore.a: $(BLT_MODULE_OBJECTS)

