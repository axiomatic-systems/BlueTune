/*****************************************************************
|
|   BlueTune - Output Node Interface
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BLT_OutputNode interface
 */

#ifndef _BLT_OUTPUT_NODE_H_
#define _BLT_OUTPUT_NODE_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltTime.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_Time delay;
} BLT_OutputNodeStatus;

/*----------------------------------------------------------------------
|   BLT_OutputNode Interface
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_OutputNode)
ATX_BEGIN_INTERFACE_DEFINITION(BLT_OutputNode)
    BLT_Result (*GetStatus)(BLT_OutputNode*       self, 
                            BLT_OutputNodeStatus* status);
ATX_END_INTERFACE_DEFINITION

/*----------------------------------------------------------------------
|   convenience macros
+---------------------------------------------------------------------*/
#define BLT_OutputNode_GetStatus(object, status) \
ATX_INTERFACE(object)->GetStatus(object, status)

#endif /* _BLT_OUTPUT_NODE_H_ */