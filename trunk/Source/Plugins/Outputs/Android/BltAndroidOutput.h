/*****************************************************************
|
|      File: BltAndroidOutput.h
|
|      Android Output Module
|
|      (c) 2002-2012 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_ANDROID_OUTPUT_H_
#define _BLT_ANDROID_OUTPUT_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_output_modules
 * @defgroup android_output_module Android Output Module 
 * Plugin module that creates media nodes that can send PCM audio data
 * to an Adnroid audio output using the OpenSLES interface.
 * These media nodes expect media pakcets with PCM audio.
 * This module responds to probe with the name:
 * 'android:default'
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
extern BLT_Result BLT_AndroidOutputModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_ANDROID_OUTPUT_H_ */
