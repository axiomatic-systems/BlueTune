/*****************************************************************
|
|      File: BltBuiltins.h
|
|      BlueTune - Builtins API
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Builtins API Header file
 */

#ifndef _BLT_BUILTINS_H_
#define _BLT_BUILTINS_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltCore.h"

/*----------------------------------------------------------------------
|       prototypes
+---------------------------------------------------------------------*/
BLT_Result BLT_Builtins_RegisterModules(BLT_Core* core);
BLT_Result BLT_Builtins_GetDefaultOutput(BLT_CString* name, BLT_CString* type);

#endif /* _BLT_BUILTINS_H_ */
