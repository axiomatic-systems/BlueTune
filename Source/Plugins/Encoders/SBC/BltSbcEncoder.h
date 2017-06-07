/*****************************************************************
|
|   SBC Encoder Module
|
|   (c) 2002-2016 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_SBC_ENCODER_H_
#define _BLT_SBC_ENCODER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_encoder_modules
 * @defgroup sbc_encoder_module SBC Encoder Module
 * Plugin module that creates media nodes capable of encoding SBC
 * audio. 
 * These media nodes expect media buffers with PCM audio and
 * and produces media packets with SBC audio frames.
 * @{ 
 */

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_SBC_ENCODER_BITRATE_PROPERTY         "SbcEncoder.Bitrate"
#define BLT_SBC_ENCODER_ALLOCATION_MODE_PROPERTY "SbcEncoder.AllocationMode"

/*----------------------------------------------------------------------
|   module
+---------------------------------------------------------------------*/
/**
 * Returns a pointer to the SBC Encoder module.
 */
BLT_Result BLT_SbcEncoderModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_SBC_ENCODER_H_ */
