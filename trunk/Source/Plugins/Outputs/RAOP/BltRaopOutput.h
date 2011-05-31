/*****************************************************************
|
|   OSX Audio Units Output Module
|
|   (c) 2002-2011 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_RAOP_OUTPUT_H_
#define _BLT_RAOP_OUTPUT_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_output_modules
 * @defgroup raop_output_module RAOP Output Module 
 * Plugin module that creates media nodes that can send PCM audio data
 * to a remote RAOP (AirPlay/AirTunes) device.
 * These media nodes expect media pakcets with PCM audio.
 * This module responds to probe with the name:
 * 'raopt://<hostname>:<port>' for version 1 (TCP), supported by older
 * devices.
 * 'raop://<hostname>:<port>' for version 2 (UDP), supported by 
 * newer devices.
 * @{
 */

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*----------------------------------------------------------------------
|   module
+---------------------------------------------------------------------*/
BLT_Result BLT_RaopOutputModule_GetModuleObject(BLT_Module** module);


#if defined(__cplusplus)
}
#endif

/** @} */

#endif /* _BLT_RAOP_OUTPUT_H_ */
