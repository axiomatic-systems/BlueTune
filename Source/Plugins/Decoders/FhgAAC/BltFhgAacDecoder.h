/*****************************************************************
|
|   AAC Decoder Module
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_FHG_AAC_DECODER_H_
#define _BLT_FHG_AAC_DECODER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_decoder_modules
 * @defgroup aac_decoder_module AAC Decoder Module 
 * Plugin module that creates media nodes capable of decoding AAC
 * audio. 
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
BLT_Result BLT_FhgAacDecoderModule_GetModuleObject(BLT_Module** module);
BLT_Result BLT_AacDecoderModule_GetModuleObject(BLT_Module** module);

/** @} */

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_FHG_AAC_DECODER_H_ */
