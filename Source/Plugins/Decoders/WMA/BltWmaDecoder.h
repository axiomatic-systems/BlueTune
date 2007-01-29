/*****************************************************************
|
|   WMA Decoder Module
|
|   (c) 2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_WMA_DECODER_H_
#define _BLT_WMA_DECODER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_decoder_modules
 * @defgroup wma_decoder_module WMA Decoder Module 
 * Plugin module that creates media nodes capable of decoding WMA
 * audio. 
 * These media nodes expect as input a byte stream with WMA
 * audio data in an ASF format.
 * This module support Windows Media, Windows Media Pro and
 * Windows Media Lossless encodings.
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
BLT_Result BLT_WmaDecoderModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_WMA_DECODER_H_ */
