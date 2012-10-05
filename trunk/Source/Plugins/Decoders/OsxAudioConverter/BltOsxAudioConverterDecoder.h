/*****************************************************************
|
|   Apple AudioConverter Decoder Module
|
|   (c) 2008-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_OSX_AUDIO_CONVERTER_DECODER_H_
#define _BLT_OSX_AUDIO_CONVERTER_DECODER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_decoder_modules
 * @defgroup osx_audio_converter_decoder_module Apple AudioConverter Decoder Module
 * Plugin module creates media nodes capable of decoding the different 
 * media types supported by the Apple AudioConverter API.
 * @{ 
 */

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|   module
+---------------------------------------------------------------------*/
#if defined(__cplusplus)
extern "C" {
#endif

BLT_Result BLT_OsxAudioConverterDecoderModule_GetModuleObject(BLT_Module** module);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_OSX_AUDIO_CONVERTER_DECODER_H_ */
