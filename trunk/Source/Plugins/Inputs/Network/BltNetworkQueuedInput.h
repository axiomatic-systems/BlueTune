/*****************************************************************
|
|   Network Queued Input Module
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_NETWORK_QUEUED_INPUT_H_
#define _BLT_NETWORK_QUEUED_INPUT_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_input_modules
 * @defgroup network_queued_input_module Network Queued Input Module 
 *
 * @{ 
 */

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"
#include "BltNetworkStream.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_NETWORK_QUEUED_INPUT_MODULE_UID "com.axiosys.input.queued-network"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct BLT_NetworkQueuedInputModule BLT_NetworkQueuedInputModule;

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
#if defined(__cplusplus)
extern "C" {
#endif

BLT_Result 
BLT_NetworkQueuedInputModule_GetModuleObject(BLT_Module** module);

BLT_Result 
BLT_NetworkQueuedInputModule_GetFromCore(BLT_Core*                      core, 
                                         BLT_NetworkQueuedInputModule** module);

BLT_Result 
BLT_NetworkQueuedInputModule_Enqueue(BLT_NetworkQueuedInputModule* self,
                                     const char*                   url,
                                     const char**                  entry_id);

BLT_Result 
BLT_NetworkQueuedInputModule_RemoveEntry(BLT_NetworkQueuedInputModule* self,
                                         const char*                   entry_id);

BLT_Result 
BLT_NetworkQueuedInputModule_GetEntryStatus(BLT_NetworkQueuedInputModule*    self,
                                            const char*                      entry_id,
                                            BLT_BufferedNetworkStreamStatus* status);


#if defined(__cplusplus)
}
#endif

/** @} */

#endif /* _BLT_NETWORK_QUEUED_INPUT_H_ */
