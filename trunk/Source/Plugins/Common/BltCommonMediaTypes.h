/*****************************************************************
|
|   Common Media Types
|
|   (c) 2002-2007 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Common Media Types
 */

#ifndef _BLT_COMMON_MEDIA_TYPES_H_
#define _BLT_COMMON_MEDIA_TYPES_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_MediaType base;
    unsigned int  object_type_id;
    unsigned int  decoder_info_length;
    /* variable size array follows */
    unsigned char decoder_info[1]; /* could be more than 1 byte */
} BLT_Mpeg4AudioMediaType;

#endif /* _BLT_COMMON_MEDIA_TYPES_H_ */
