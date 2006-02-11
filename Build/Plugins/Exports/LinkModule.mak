#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# plugins
#######################################################################
ifneq ($(findstring MpegAudioDecoder, $(BLT_PLUGINS)),)
BLT_LIBRARIES_C   += -lm
BLT_LIBRARIES_CPP += -lm
endif

ifneq ($(findstring VorbisDecoder, $(BLT_PLUGINS)),)
BLT_LIBRARIES_C   += -L$(BLT_THIRDPARTY_ROOT)/Vorbis/Targets/$(BLT_TARGET)/lib -lvorbisfile -lvorbis -logg
BLT_LIBRARIES_CPP += -L$(BLT_THIRDPARTY_ROOT)/Vorbis/Targets/$(BLT_TARGET)/lib -lvorbisfile -lvorbis -logg
endif

ifneq ($(findstring FlacDecoder, $(BLT_PLUGINS)),)
BLT_LIBRARIES_C += -L$(BLT_THIRDPARTY_ROOT)/FLAC/Targets/$(BLT_TARGET)/lib -lFLAC -lm
BLT_LIBRARIES_CPP += -L$(BLT_THIRDPARTY_ROOT)/FLAC/Targets/$(BLT_TARGET)/lib -lFLAC -lm
endif

ifneq ($(findstring NeutrinoOutput, $(BLT_PLUGINS)),)
BLT_LIBRARIES_C   += -lasound
BLT_LIBRARIES_CPP += -lasound
endif

ifneq ($(findstring AlsaOutput, $(BLT_PLUGINS)),)
BLT_LIBRARIES_C   += -lasound
BLT_LIBRARIES_CPP += -lasound
endif
