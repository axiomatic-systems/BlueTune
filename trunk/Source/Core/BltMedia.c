/*****************************************************************
|
|      File: BltMedia.c
|
|      BlueTune - Media Utilities
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Media Utilities
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltCore.h"
#include "BltMedia.h"
#include "BltDebug.h"

/*----------------------------------------------------------------------
|       global constants
+---------------------------------------------------------------------*/
const BLT_MediaType BLT_MediaType_None = {
    BLT_MEDIA_TYPE_ID_NONE, /* id             */
    0,                      /* flags          */
    0                       /* extension size */
};

const BLT_MediaType BLT_MediaType_Unknown = {
    BLT_MEDIA_TYPE_ID_UNKNOWN, /* id             */
    0,                         /* flags          */
    0                          /* extension size */
};

/*----------------------------------------------------------------------
|       BLT_MediaType_Init
+---------------------------------------------------------------------*/
BLT_Result 
BLT_MediaType_Init(BLT_MediaType* type, BLT_MediaTypeId id)
{
    type->id             = id;
    type->flags          = 0;
    type->extension_size = 0;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_MediaType_Clone
+---------------------------------------------------------------------*/
BLT_Result 
BLT_MediaType_Clone(const BLT_MediaType* from, BLT_MediaType** to)
{
    if (from == NULL) {
    	*to = NULL;
    	return BLT_SUCCESS;
    } else {
	    BLT_Size size = sizeof(BLT_MediaType)+from->extension_size;
	    *to = (BLT_MediaType*)ATX_AllocateMemory(size);
	    if (*to == NULL) return BLT_ERROR_OUT_OF_MEMORY;
	    ATX_CopyMemory((void*)*to, (const void*)from, size);
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_MediaType_Free
+---------------------------------------------------------------------*/
BLT_Result 
BLT_MediaType_Free(BLT_MediaType* type)
{
    if (type) ATX_FreeMemory(type);
    return BLT_SUCCESS;
}
