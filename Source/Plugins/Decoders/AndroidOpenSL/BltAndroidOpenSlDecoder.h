/*****************************************************************
|
|   Android OpenSL ES Decoder Module
|
|   (c) 2002-2014 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_ANDROID_OPENSL_DECODER_H_
#define _BLT_ANDROID_OPENSL_DECODER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_decoder_modules
 * @defgroup android_opensl_decoder_module Android OpenSL ES Decoder Module 
 * Plugin module that creates media nodes capable of decoding AAC
 * audio using the Android native codecs. 
 * These media nodes expect media buffers with AAC audio frames
 * and produces media packets with PCM audio.
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

/**
 * Returns a pointer to the AAC Decoder module.
 */
BLT_Result BLT_AndroidOpenSlDecoderModule_GetModuleObject(BLT_Module** module);

/** @} */

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_ANDROID_OPENSL_DECODER_H_ */
