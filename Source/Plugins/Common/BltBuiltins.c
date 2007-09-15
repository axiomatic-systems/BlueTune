/*****************************************************************
|
|   BlueTune - Builtins Object
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Builtins
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltBuiltins.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.common")

/* inputs */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_INPUT)
#include "BltFileInput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_NETWORK_INPUT)
#include "BltNetworkInput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_CDDA_INPUT)
#include "BltCddaInput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_ALSA_INPUT)
#include "BltAlsaInput.h"
#endif

/* general */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILTER_HOST)
#include "BltFilterHost.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_STREAM_PACKETIZER)
#include "BltStreamPacketizer.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_PACKET_STREAMER)
#include "BltPacketStreamer.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_CROSS_FADER)
#include "BltCrossFader.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_SILENCE_REMOVER)
#include "BltSilenceRemover.h"
#endif

/* filters */
#if defined(BLT_CONFIG_MODULES_ENABLE_GAIN_CONTROL_FILTER)
#include "BltGainControlFilter.h"
#endif

/* adapters */
#if defined(BLT_CONFIG_MODULES_ENABLE_PCM_ADAPTER)
#include "BltPcmAdapter.h"
#endif

/* parsers */
#if defined(BLT_CONFIG_MODULES_ENABLE_WAVE_PARSER)
#include "BltWaveParser.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_AIFF_PARSER)
#include "BltAiffParser.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_TAG_PARSER)
#include "BltTagParser.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_MP4_PARSER)
#include "BltMp4Parser.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_ADTS_PARSER)
#include "BltAdtsParser.h"
#endif

/* formatters */
#if defined(BLT_CONFIG_MODULES_ENABLE_WAVE_FORMATTER)
#include "BltWaveFormatter.h"
#endif

/* decoders */
#if defined(BLT_CONFIG_MODULES_ENABLE_VORBIS_DECODER)
#include "BltVorbisDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_MPEG_AUDIO_DECODER)
#include "BltMpegAudioDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_FLAC_DECODER)
#include "BltFlacDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_AAC_DECODER)
#include "BltAacDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_WMA_DECODER)
#include "BltWmaDecoder.h"
#endif

/* outputs */
#if defined(BLT_CONFIG_MODULES_ENABLE_WIN32_OUTPUT)
#include "BltWin32Output.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_OSS_OUTPUT)
#include "BltOssOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_MACOSX_OUTPUT)
#include "BltMacOSXOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_ALSA_OUTPUT)
#include "BltAlsaOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_NEUTRINO_OUTPUT)
#include "BltNeutrinoOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_DEBUG_OUTPUT)
#include "BltDebugOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_NULL_OUTPUT)
#include "BltNullOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_OUTPUT)
#include "BltFileOutput.h"
#endif

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#if !defined(BLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME)
#define BLT_BUILTINS_DEFAULT_OUTPUT_NAME "null"
#endif
#if !defined(BLT_CONFIG_MODULES_DEFAULT_OUTPUT_TYPE)
#define BLT_BUILTINS_DEFAULT_OUTPUT_TYPE "audio/pcm"
#endif

/*----------------------------------------------------------------------
|    BLT_Builtins_RegisterModules
+---------------------------------------------------------------------*/
BLT_Result
BLT_Builtins_RegisterModules(BLT_Core* core)
{
    BLT_Result  result;
    BLT_Module* module;

    /* file input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_INPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_FileInputModule");
    result = BLT_FileInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* network input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_NETWORK_INPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_NetworkInputModule");
    result = BLT_NetworkInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* cdda input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_CDDA_INPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_CddaInputModule");
    result = BLT_CddaInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* alsa input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_ALSA_INPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_AlsaInputModule");
    result = BLT_AlsaInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* stream packetizer */
#if defined(BLT_CONFIG_MODULES_ENABLE_STREAM_PACKETIZER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_StreamPacketizerModule");
    result = BLT_StreamPacketizerModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* packet streamer */
#if defined(BLT_CONFIG_MODULES_ENABLE_PACKET_STREAMER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_PacketStreamerModule");
    result = BLT_PacketStreamerModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* cross fader */
#if defined(BLT_CONFIG_MODULES_ENABLE_CROSS_FADER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_CrossFaderModule");
    result = BLT_CrossFaderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* silence remover */
#if defined(BLT_CONFIG_MODULES_ENABLE_SILENCE_REMOVER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_SilenceRemoverModule");
    result = BLT_SilenceRemoverModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* wave parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_WAVE_PARSER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_WaveParserModule");
    result = BLT_WaveParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* aiff parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_AIFF_PARSER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_AiffParserModule");
    result = BLT_AiffParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* tag parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_TAG_PARSER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_TagParserModule");
    result = BLT_TagParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* mp4 parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_MP4_PARSER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_Mp4ParserModule");
    result = BLT_Mp4ParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* adts parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_ADTS_PARSER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_AdtsParserModule");
    result = BLT_AdtsParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* wave formatter */
#if defined(BLT_CONFIG_MODULES_ENABLE_WAVE_FORMATTER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_WaveFormatterModule");
    result = BLT_WaveFormatterModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* gain control filter */
#if defined(BLT_CONFIG_MODULES_ENABLE_GAIN_CONTROL_FILTER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_GainControlFilterModule");
    result = BLT_GainControlFilterModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* filter host */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILTER_HOST)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_FilterHostModule");
    result = BLT_FilterHostModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* pcm adapter */
#if defined(BLT_CONFIG_MODULES_ENABLE_PCM_ADAPTER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_PcmAdapterModule");
    result = BLT_PcmAdapterModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* mpeg audio decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_MPEG_AUDIO_DECODER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_MpegAudioDecoderModule");
    result = BLT_MpegAudioDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* vorbis decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_VORBIS_DECODER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_VorbisDecoderModule");
    result = BLT_VorbisDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* flac decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_FLAC_DECODER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_FlacDecoderModule");
    result = BLT_FlacDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* aac decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_AAC_DECODER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_AacDecoderModule");
    result = BLT_AacDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* wma decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_WMA_DECODER)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_WmaDecoderModule");
    result = BLT_WmaDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* win32 output */
#if defined(BLT_CONFIG_MODULES_ENABLE_WIN32_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_Win32OutputModule");
    result = BLT_Win32OutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* oss output */
#if defined(BLT_CONFIG_MODULES_ENABLE_OSS_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_OssOutputModule");
    result = BLT_OssOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* alsa output */
#if defined(BLT_CONFIG_MODULES_ENABLE_ALSA_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_AlsaOutputModule");
    result = BLT_AlsaOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* alsa output */
#if defined(BLT_CONFIG_MODULES_ENABLE_MACOSX_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_MacOSXOutputModule");
    result = BLT_MacOSXOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* neutrino output */
#if defined(BLT_CONFIG_MODULES_ENABLE_NEUTRINO_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_NeutrinoOutputModule");
    result = BLT_NeutrinoOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* debug output */
#if defined(BLT_CONFIG_MODULES_ENABLE_DEBUG_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_DebugOutputModule");
    result = BLT_DebugOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* file output */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_FileOutputModule");
    result = BLT_FileOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* null output */
#if defined(BLT_CONFIG_MODULES_ENABLE_NULL_OUTPUT)
    ATX_LOG_FINE("BLT_Builtins_RegisterModules - registering BLT_NullOutputModule");
    result = BLT_NullOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Builtins_GetDefaultOutput
+---------------------------------------------------------------------*/
#define BLT_STRINGIFY_1(x) #x
#define BLT_STRINGIFY(x) BLT_STRINGIFY_1(x)
BLT_Result
BLT_Builtins_GetDefaultOutput(BLT_CString* name, BLT_CString* type)
{
#if defined(BLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME)
    if (name) *name = BLT_STRINGIFY(BLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME);
#else
    if (name) *name = BLT_BUILTINS_DEFAULT_OUTPUT_NAME;
#endif

#if defined(BLT_CONFIG_MODULES_DEFAULT_OUTPUT_TYPE)
    if (type) *type = BLT_STRINGIFY(BLT_CONFIG_MODULES_DEFAULT_OUTPUT_TYPE);
#else
    if (type) *type = BLT_BUILTINS_DEFAULT_OUTPUT_TYPE;
#endif

    return BLT_SUCCESS;
}
