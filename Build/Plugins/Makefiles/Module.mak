#######################################################################
#
#    Core Module Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# common stuff
#######################################################################
BLT_MODULE_SOURCES += BltBuiltins.c
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Common

BLT_INCLUDE_MODULES :=
BLT_IMPORT_MODULES :=

#######################################################################
# plugins
#######################################################################
ifneq ($(findstring FileInput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Inputs/File
BLT_MODULE_SOURCES += BltFileInput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Inputs/File
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_FILE_INPUT
endif

ifneq ($(findstring NetworkInput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Inputs/Network
BLT_MODULE_SOURCES += BltNetworkInput.c BltTcpNetworkStream.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Inputs/Network
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_NETWORK_INPUT
endif

ifneq ($(findstring CddaInput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Inputs/CDDA
BLT_MODULE_SOURCES += BltCddaInput.c BltCddaDevice.c Blt$(BLT_PLUGINS_CDDA_TYPE)CddaDevice.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Inputs/CDDA
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_CDDA_INPUT
endif

ifneq ($(findstring AlsaInput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Inputs/Alsa
BLT_MODULE_SOURCES += BltAlsaInput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Inputs/Alsa
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_ALSA_INPUT
endif

ifneq ($(findstring WaveParser, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Parsers/Wave
BLT_MODULE_SOURCES += BltWaveParser.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Parsers/Wave
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_WAVE_PARSER
endif

ifneq ($(findstring AiffParser, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Parsers/Aiff
BLT_MODULE_SOURCES += BltAiffParser.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Parsers/Aiff
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_AIFF_PARSER
endif

ifneq ($(findstring StreamPacketizer, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/General/StreamPacketizer
BLT_MODULE_SOURCES += BltStreamPacketizer.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/General/StreamPacketizer
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_STREAM_PACKETIZER
endif

ifneq ($(findstring PacketStreamer, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/General/PacketStreamer
BLT_MODULE_SOURCES += BltPacketStreamer.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/General/PacketStreamer
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_PACKET_STREAMER
endif

ifneq ($(findstring TagParser, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Parsers/Tags
BLT_MODULE_SOURCES += BltTagParser.c BltId3Parser.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Parsers/Tags
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_TAG_PARSER
endif

ifneq ($(findstring WaveFormatter, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Formatters/Wave
BLT_MODULE_SOURCES += BltWaveFormatter.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Formatters/Wave
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_WAVE_FORMATTER
endif

ifneq ($(findstring GainControlFilter, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Filters/GainControl
BLT_MODULE_SOURCES += BltGainControlFilter.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Filters/GainControl
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_GAIN_CONTROL_FILTER
endif

ifneq ($(findstring PcmAdapter, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Adapters/PcmAdapter
BLT_MODULE_SOURCES += BltPcmAdapter.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Adapters/PcmAdapter
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_PCM_ADAPTER
endif

ifneq ($(findstring CrossFader, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/General/CrossFader
BLT_MODULE_SOURCES += BltCrossFader.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/General/CrossFader
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_CROSS_FADER
endif

ifneq ($(findstring SilenceRemover, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/General/SilenceRemover
BLT_MODULE_SOURCES += BltSilenceRemover.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/General/SilenceRemover
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_SILENCE_REMOVER
endif

ifneq ($(findstring MpegAudioDecoder, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Decoders/MpegAudio
BLT_MODULE_SOURCES += BltMpegAudioDecoder.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Decoders/MpegAudio
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_MPEG_AUDIO_DECODER
BLT_IMPORT_MODULES += Fluo
endif

ifneq ($(findstring VorbisDecoder, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Decoders/Vorbis
BLT_MODULE_SOURCES += BltVorbisDecoder.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Decoders/Vorbis
BLT_INCLUDES_C += -I$(BLT_THIRDPARTY_ROOT)/Vorbis/Targets/$(BLT_TARGET)/include
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_VORBIS_DECODER
endif

ifneq ($(findstring FlacDecoder, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Decoders/FLAC
BLT_MODULE_SOURCES += BltFlacDecoder.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Decoders/FLAC
BLT_INCLUDES_C += -I$(BLT_THIRDPARTY_ROOT)/FLAC/Targets/$(BLT_TARGET)/include
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_FLAC_DECODER
endif

ifneq ($(findstring NullOutput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/Null
BLT_MODULE_SOURCES += BltNullOutput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/Null
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_NULL_OUTPUT
endif

ifneq ($(findstring FileOutput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/File
BLT_MODULE_SOURCES += BltFileOutput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/File
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_FILE_OUTPUT
endif

ifneq ($(findstring DebugOutput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/Debug
BLT_MODULE_SOURCES += BltDebugOutput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/Debug
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_DEBUG_OUTPUT
endif

ifneq ($(findstring NeutrinoOutput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/Neutrino
BLT_MODULE_SOURCES += BltNeutrinoOutput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/Neutrino
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_NEUTRINO_OUTPUT
endif

ifneq ($(findstring OssOutput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/OSS
BLT_MODULE_SOURCES += BltOssOutput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/OSS
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_OSS_OUTPUT
endif

ifneq ($(findstring OssOutput, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/Alsa
BLT_MODULE_SOURCES += BltAlsaOutput.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/Alsa
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_ALSA_OUTPUT
endif

ifneq ($(findstring Win32Output, $(BLT_PLUGINS)),)
VPATH += :$(BLT_SOURCE_ROOT)/Plugins/Outputs/Win32
BLT_MODULE_SOURCES += BltWin32Output.c
BLT_INCLUDES_C += -I$(BLT_SOURCE_ROOT)/Plugins/Outputs/Win32
BLT_DEFINES_C += -DBLT_CONFIG_MODULES_ENABLE_WIN32_OUTPUT
endif

#######################################################################
# module dependencies
#######################################################################
BLT_INCLUDE_MODULES += Atomix Core
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak
include $(BLT_BUILD_INCLUDES)/ImportModuleDeps.mak

#######################################################################
# sources and object files
#######################################################################
BLT_MODULE_OBJECTS += $(BLT_MODULE_SOURCES:.c=.o)

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a

#######################################################################
# targets
#######################################################################
libBltPlugins.a: $(BLT_IMPORTED_MODULE_LIBS) $(BLT_MODULE_OBJECTS) $(BLT_MODULE_LIBRARIES)
