/*****************************************************************
|
|      File: BltQnxOutput.h
|
|      QNX Output Module
|
|      (c) 2002-2013 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_QNX_OUTPUT_H_
#define _BLT_QNX_OUTPUT_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_output_modules
 * @defgroup alsa_output_module QNX Output Module 
 * Plugin module that creates media nodes that can send PCM audio data
 * to a sound card using the QNX Audio API.
 * These media nodes expect media pakcets with PCM audio.
 * This module responds to probe with the name:
 * 'qnx:<name>'
 * where <name> is the name of a QNX output.
 * @{ 
 */

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|       module
+---------------------------------------------------------------------*/
extern BLT_Result BLT_QnxOutputModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_QNX_OUTPUT_H_ */
