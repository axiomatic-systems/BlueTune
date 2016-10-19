/*****************************************************************
|
|   SBC Decoder Module
|
|   (c) 2002-2016 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_SBC_DECODER_H_
#define _BLT_SBC_DECODER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_decoder_modules
 * @defgroup sbc_decoder_module SBC Decoder Module
 * Plugin module that creates media nodes capable of decoding SBC
 * audio. 
 * These media nodes expect media buffers with SBC audio frames
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
/**
 * Returns a pointer to the SBC Decoder module.
 */
BLT_Result BLT_SbcDecoderModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_SBC_DECODER_H_ */
