/*****************************************************************
|
|   MacOSXOutput Module
|
|   (c) 2002-2007 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_MACOSX_OUTPUT_H_
#define _BLT_MACOSX_OUTPUT_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_output_modules
 * @defgroup macosx_output_module MacOSX Output Module 
 * Plugin module that creates media nodes that can send PCM audio data
 * to a sound card on MacOSX.
 * These media nodes expect media pakcets with PCM audio.
 * This module responds to probe with the name:
 * 'macosx:<n>'
 * (In this version, <n> is ignored, and the default sound card will
 * be used).
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
BLT_Result BLT_MacOSXOutputModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_MACOSX_OUTPUT_H_ */
