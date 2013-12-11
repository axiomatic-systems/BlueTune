/*****************************************************************
|
|   Memory Output Module
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_MEMORY_OUTPUT_H_
#define _BLT_MEMORY_OUTPUT_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_output_modules
 * @defgroup memory_output_module Debug Output Module
 * Plugin module that creates media nodes that can be used as an output. 
 * It produces media nodes that buffer media packets in memory. The 
 * stored packets can then be retrieved by the caller.
 * This module responds to probes with the name 'memory'.
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

BLT_Result BLT_MemoryOutputModule_GetModuleObject(BLT_Module** module);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif /* _BLT_MEMORY_OUTPUT_H_ */
