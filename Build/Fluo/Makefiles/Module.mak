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
BLT_INCLUDE_MODULES = Atomix
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

#######################################################################
# optional thirdparty modules
#######################################################################
ifeq ($(FLO_DECODER_ENGINE),)
FLO_DECODER_ENGINE=mpg123
endif

ifeq ($(FLO_DECODER_ENGINE), mpg123)
FLO_DECODER_ENGINE_SOURCES = common.c dct64_i386.c decode_i386.c interface2.c layer1.c layer2.c layer3.c tabinit.c
BLT_INCLUDES_C += -I$(BLT_ROOT)/ThirdParty/mpg123/mpglib
BLT_DEFINES_C += -DFLO_DECODER_ENGINE=FLO_DECODER_ENGINE_MPG123
VPATH += $(BLT_THIRDPARTY_ROOT)/mpg123/mpglib
else 
ifeq ($(FLO_DECODER_ENGINE), ffmpeg)
FLO_DECODER_ENGINE_SOURCES =
BLT_DEFINES_C += -DFLO_DECODER_ENGINE=FLO_DECODER_ENGINE_FFMPEG
BLT_INCLUDES_C += -I$(BLT_THIRDPARTY_ROOT)/ffmpeg/Targets/$(BLT_TARGET)/include
BLT_MODULE_OBJECTS += libavcodec.o
VPATH += $(BLT_THIRDPARTY_ROOT)/ffmpeg/Targets/$(BLT_TARGET)/lib
endif
endif

#######################################################################
# sources and object files
#######################################################################
BLT_MODULE_SOURCES = \
	FloDecoder.c FloBitStream.c FloFrame.c FloTables.c FloHeaders.c $(FLO_DECODER_ENGINE_SOURCES)

BLT_MODULE_OBJECTS += $(BLT_MODULE_SOURCES:.c=.o)

#######################################################################
# paths
#######################################################################
VPATH += $(BLT_SOURCE_ROOT)/Fluo

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a

#######################################################################
# targets
#######################################################################
libFluo.a: $(BLT_MODULE_OBJECTS)

