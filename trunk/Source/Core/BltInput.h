/*****************************************************************
|
|   File: BltInput.h
|
|   BlueTune - Input Interface
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Header file for the Input Interface
 */

#ifndef _BLT_INPUT_H_
#define _BLT_INPUT_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"

/*----------------------------------------------------------------------
|   BLT_Input interface
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_Input)
ATX_BEGIN_INTERFACE_DEFINITION(BLT_Input)
    BLT_Result (*GetByteStream)(BLT_InputInstance* instance,
                                ATX_ByteStream*    stream);
ATX_END_INTERFACE_DEFINITION

/*----------------------------------------------------------------------
|   convenience macros
+---------------------------------------------------------------------*/
#define BLT_Input_GetByteStream(object, stream) \
ATX_INTERFACE(object)->GetByteStream(ATX_INSTANCE(object), count)

#endif /* _BLT_INPUT_H_ */
