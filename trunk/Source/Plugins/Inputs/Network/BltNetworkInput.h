/*****************************************************************
|
|      Network: BltNetworkInput.h
|
|      Network Input Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_NETWORK_INPUT_H_
#define _BLT_NETWORK_INPUT_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|       module
+---------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

extern BLT_Result BLT_NetworkInputModule_GetModuleObject(BLT_Module* module);

#ifdef __cplusplus
}
#endif

#endif /* _BLT_NETWORK_INPUT_H_ */
