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
#if defined(BLT_CONFIG_MODULES_ENABLE_TEST_INPUT)
#include "BltTestInput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_INPUT)
#include "BltFileInput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_NETWORK_INPUT)
#include "BltNetworkInput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_CALLBACK_INPUT)
#include "BltCallbackInput.h"
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

#if defined(BLT_CONFIG_MODULES_ENABLE_DCF_PARSER)
#include "BltDcfParser.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_ADTS_PARSER)
#include "BltAdtsParser.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_DDPLUS_PARSER)
#include "BltDolbyDigitalPlusParser.h"
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

#if defined(BLT_CONFIG_MODULES_ENABLE_ALAC_DECODER)
#include "BltAlacDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_AAC_DECODER)
#include "BltAacDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_WMA_DECODER)
#include "BltWmaDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_FFMPEG_DECODER)
#include "BltFfmpegDecoder.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_DDPLUS_DECODER)
#include "BltDolbyDigitalPlusDecoder.h"
#endif

/* outputs */
#if defined(BLT_CONFIG_MODULES_ENABLE_WIN32_AUDIO_OUTPUT)
#include "BltWin32AudioOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_DX9_VIDEO_OUTPUT)
#include "BltDx9VideoOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_OSS_OUTPUT)
#include "BltOssOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_OSX_AUDIO_UNITS_OUTPUT)
#include "BltOsxAudioUnitsOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_OSX_AUDIO_QUEUE_OUTPUT)
#include "BltOsxAudioQueueOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_OSX_VIDEO_OUTPUT)
#include "BltOsxVideoOutput.h"
#endif

#if defined(BLT_CONFIG_MODULES_ENABLE_SDL_OUTPUT)
#include "BltSdlOutput.h"
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

#if defined(BLT_CONFIG_MODULES_ENABLE_CALLBACK_OUTPUT)
#include "BltCallbackOutput.h"
#endif

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#if !defined(BLT_CONFIG_MODULES_DEFAULT_AUDIO_OUTPUT_NAME)
#define BLT_BUILTINS_DEFAULT_AUDIO_OUTPUT_NAME "null"
#endif
#if !defined(BLT_CONFIG_MODULES_DEFAULT_AUDIO_OUTPUT_TYPE)
#define BLT_BUILTINS_DEFAULT_AUDIO_OUTPUT_TYPE "audio/pcm"
#endif

#if !defined(BLT_CONFIG_MODULES_DEFAULT_VIDEO_OUTPUT_NAME)
#define BLT_BUILTINS_DEFAULT_VIDEO_OUTPUT_NAME "null"
#endif
#if !defined(BLT_CONFIG_MODULES_DEFAULT_VIDEO_OUTPUT_TYPE)
#define BLT_BUILTINS_DEFAULT_VIDEO_OUTPUT_TYPE "video/raw"
#endif

/*----------------------------------------------------------------------
|    BLT_Builtins_RegisterModules
+---------------------------------------------------------------------*/
BLT_Result
BLT_Builtins_RegisterModules(BLT_Core* core)
{
    BLT_Result  result = ATX_SUCCESS;
    BLT_Module* module = NULL;

    ATX_LOG_FINE("registering builtin modules");

    /* in the case where there are no builtin modules, avoid compiler warnings */
    ATX_COMPILER_UNUSED(core);
    ATX_COMPILER_UNUSED(result);
    ATX_COMPILER_UNUSED(module);

    /* test input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_TEST_INPUT)
    ATX_LOG_FINE("registering BLT_TestInputModule");
    result = BLT_TestInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* file input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_INPUT)
    ATX_LOG_FINE("registering BLT_FileInputModule");
    result = BLT_FileInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* network input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_NETWORK_INPUT)
    ATX_LOG_FINE("registering BLT_NetworkInputModule");
    result = BLT_NetworkInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* cdda input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_CDDA_INPUT)
    ATX_LOG_FINE("registering BLT_CddaInputModule");
    result = BLT_CddaInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* alsa input module */
#if defined(BLT_CONFIG_MODULES_ENABLE_ALSA_INPUT)
    ATX_LOG_FINE("registering BLT_AlsaInputModule");
    result = BLT_AlsaInputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* stream packetizer */
#if defined(BLT_CONFIG_MODULES_ENABLE_STREAM_PACKETIZER)
    ATX_LOG_FINE("registering BLT_StreamPacketizerModule");
    result = BLT_StreamPacketizerModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* packet streamer */
#if defined(BLT_CONFIG_MODULES_ENABLE_PACKET_STREAMER)
    ATX_LOG_FINE("registering BLT_PacketStreamerModule");
    result = BLT_PacketStreamerModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* cross fader */
#if defined(BLT_CONFIG_MODULES_ENABLE_CROSS_FADER)
    ATX_LOG_FINE("registering BLT_CrossFaderModule");
    result = BLT_CrossFaderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* silence remover */
#if defined(BLT_CONFIG_MODULES_ENABLE_SILENCE_REMOVER)
    ATX_LOG_FINE("registering BLT_SilenceRemoverModule");
    result = BLT_SilenceRemoverModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* wave parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_WAVE_PARSER)
    ATX_LOG_FINE("registering BLT_WaveParserModule");
    result = BLT_WaveParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* aiff parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_AIFF_PARSER)
    ATX_LOG_FINE("registering BLT_AiffParserModule");
    result = BLT_AiffParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* tag parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_TAG_PARSER)
    ATX_LOG_FINE("registering BLT_TagParserModule");
    result = BLT_TagParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* mp4 parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_MP4_PARSER)
    ATX_LOG_FINE("registering BLT_Mp4ParserModule");
    result = BLT_Mp4ParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* dcf parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_DCF_PARSER)
    ATX_LOG_FINE("registering BLT_DcfParserModule");
    result = BLT_DcfParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* adts parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_ADTS_PARSER)
    ATX_LOG_FINE("registering BLT_AdtsParserModule");
    result = BLT_AdtsParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* ddplus parser */
#if defined(BLT_CONFIG_MODULES_ENABLE_DDPLUS_PARSER)
    ATX_LOG_FINE("registering BLT_DolbyDigitalPlusParserModule");
    result = BLT_DolbyDigitalPlusParserModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* wave formatter */
#if defined(BLT_CONFIG_MODULES_ENABLE_WAVE_FORMATTER)
    ATX_LOG_FINE("registering BLT_WaveFormatterModule");
    result = BLT_WaveFormatterModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* gain control filter */
#if defined(BLT_CONFIG_MODULES_ENABLE_GAIN_CONTROL_FILTER)
    ATX_LOG_FINE("registering BLT_GainControlFilterModule");
    result = BLT_GainControlFilterModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* filter host */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILTER_HOST)
    ATX_LOG_FINE("registering BLT_FilterHostModule");
    result = BLT_FilterHostModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* pcm adapter */
#if defined(BLT_CONFIG_MODULES_ENABLE_PCM_ADAPTER)
    ATX_LOG_FINE("registering BLT_PcmAdapterModule");
    result = BLT_PcmAdapterModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* mpeg audio decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_MPEG_AUDIO_DECODER)
    ATX_LOG_FINE("registering BLT_MpegAudioDecoderModule");
    result = BLT_MpegAudioDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* vorbis decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_VORBIS_DECODER)
    ATX_LOG_FINE("registering BLT_VorbisDecoderModule");
    result = BLT_VorbisDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* flac decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_FLAC_DECODER)
    ATX_LOG_FINE("registering BLT_FlacDecoderModule");
    result = BLT_FlacDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* alac decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_ALAC_DECODER)
    ATX_LOG_FINE("registering BLT_AlacDecoderModule");
    result = BLT_AlacDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* aac decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_AAC_DECODER)
    ATX_LOG_FINE("registering BLT_AacDecoderModule");
    result = BLT_AacDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* wma decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_WMA_DECODER)
    ATX_LOG_FINE("registering BLT_WmaDecoderModule");
    result = BLT_WmaDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* ffmpeg decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_FFMPEG_DECODER)
    ATX_LOG_FINE("registering BLT_FfmpegDecoderModule");
    result = BLT_FfmpegDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* ddplus decoder */
#if defined(BLT_CONFIG_MODULES_ENABLE_DDPLUS_DECODER)
    ATX_LOG_FINE("registering BLT_DolbyDigitalPlusDecoderModule");
    result = BLT_DolbyDigitalPlusDecoderModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* win32 audio output */
#if defined(BLT_CONFIG_MODULES_ENABLE_WIN32_AUDIO_OUTPUT)
    ATX_LOG_FINE("registering BLT_Win32AudioOutputModule");
    result = BLT_Win32AudioOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* directx9 video output */
#if defined(BLT_CONFIG_MODULES_ENABLE_DX9_VIDEO_OUTPUT)
	ATX_LOG_FINE("registering BLT_Dx9VideoOutputModule");
	result = BLT_Dx9VideoOutputModule_GetModuleObject(&module);
	if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* oss output */
#if defined(BLT_CONFIG_MODULES_ENABLE_OSS_OUTPUT)
    ATX_LOG_FINE("registering BLT_OssOutputModule");
    result = BLT_OssOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* alsa output */
#if defined(BLT_CONFIG_MODULES_ENABLE_ALSA_OUTPUT)
    ATX_LOG_FINE("registering BLT_AlsaOutputModule");
    result = BLT_AlsaOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* osx audio output */
#if defined(BLT_CONFIG_MODULES_ENABLE_OSX_AUDIO_UNITS_OUTPUT)
    ATX_LOG_FINE("registering BLT_OsxAudioUnitsOutputModule");
    result = BLT_OsxAudioUnitsOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* osx audio queue output */
#if defined(BLT_CONFIG_MODULES_ENABLE_OSX_AUDIO_QUEUE_OUTPUT)
    ATX_LOG_FINE("registering BLT_OsxAudioQueueOutputModule");
    result = BLT_OsxAudioQueueOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) {
        result = BLT_Core_RegisterModule(core, module);
        if (BLT_FAILED(result)) return result;
    }
#endif

    /* osx video output */
#if defined(BLT_CONFIG_MODULES_ENABLE_OSX_VIDEO_OUTPUT)
    ATX_LOG_FINE("registering BLT_OsxVideoOutputModule");
    result = BLT_OsxVideoOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* sdl output */
#if defined(BLT_CONFIG_MODULES_ENABLE_SDL_OUTPUT)
    ATX_LOG_FINE("registering BLT_SdlOutputModule");
    result = BLT_SdlOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* neutrino output */
#if defined(BLT_CONFIG_MODULES_ENABLE_NEUTRINO_OUTPUT)
    ATX_LOG_FINE("registering BLT_NeutrinoOutputModule");
    result = BLT_NeutrinoOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* debug output */
#if defined(BLT_CONFIG_MODULES_ENABLE_DEBUG_OUTPUT)
    ATX_LOG_FINE("registering BLT_DebugOutputModule");
    result = BLT_DebugOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* file output */
#if defined(BLT_CONFIG_MODULES_ENABLE_FILE_OUTPUT)
    ATX_LOG_FINE("registering BLT_FileOutputModule");
    result = BLT_FileOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    /* null output */
#if defined(BLT_CONFIG_MODULES_ENABLE_NULL_OUTPUT)
    ATX_LOG_FINE("registering BLT_NullOutputModule");
    result = BLT_NullOutputModule_GetModuleObject(&module);
    if (BLT_SUCCEEDED(result)) BLT_Core_RegisterModule(core, module);
#endif

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Builtins_GetDefaultOutput
+---------------------------------------------------------------------*/
#define BLT_STRINGIFY_1(x) #x
#define BLT_STRINGIFY(x) BLT_STRINGIFY_1(x)

BLT_Result
BLT_Builtins_GetDefaultAudioOutput(BLT_CString* name, BLT_CString* type)
{
#if defined(BLT_CONFIG_MODULES_DEFAULT_AUDIO_OUTPUT_NAME)
    if (name) *name = BLT_STRINGIFY(BLT_CONFIG_MODULES_DEFAULT_AUDIO_OUTPUT_NAME);
#else
    if (name) *name = BLT_BUILTINS_DEFAULT_AUDIO_OUTPUT_NAME;
#endif

#if defined(BLT_CONFIG_MODULES_DEFAULT_AUDIO_OUTPUT_TYPE)
    if (type) *type = BLT_STRINGIFY(BLT_CONFIG_MODULES_DEFAULT_AUDIO_OUTPUT_TYPE);
#else
    if (type) *type = BLT_BUILTINS_DEFAULT_AUDIO_OUTPUT_TYPE;
#endif

    return BLT_SUCCESS;
}

BLT_Result
BLT_Builtins_GetDefaultVideoOutput(BLT_CString* name, BLT_CString* type)
{
#if defined(BLT_CONFIG_MODULES_DEFAULT_VIDEO_OUTPUT_NAME)
    if (name) *name = BLT_STRINGIFY(BLT_CONFIG_MODULES_DEFAULT_VIDEO_OUTPUT_NAME);
#else
    if (name) *name = BLT_BUILTINS_DEFAULT_VIDEO_OUTPUT_NAME;
#endif

#if defined(BLT_CONFIG_MODULES_DEFAULT_VIDEO_OUTPUT_TYPE)
    if (type) *type = BLT_STRINGIFY(BLT_CONFIG_MODULES_DEFAULT_VIDEO_OUTPUT_TYPE);
#else
    if (type) *type = BLT_BUILTINS_DEFAULT_VIDEO_OUTPUT_TYPE;
#endif

    return BLT_SUCCESS;
}
