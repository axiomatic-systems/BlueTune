/*****************************************************************
|
|   BlueTune - Network Stream
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
****************************************************************/

#ifndef _BLT_NETWORK_STREAM_H_
#define _BLT_NETWORK_STREAM_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltTypes.h"
#include "BltStream.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct BLT_NetworkStream BLT_NetworkStream;

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkStream_Create(BLT_Size            size,
                         BLT_Size            min_buffer_fullness,
                         ATX_InputStream*    source, 
                         BLT_NetworkStream** stream);

ATX_InputStream*
BLT_NetworkStream_GetInputStream(BLT_NetworkStream* self);

void
BLT_NetworkStream_SetContext(BLT_NetworkStream* self, BLT_Stream* context);

BLT_Result
BLT_NetworkStream_Release(BLT_NetworkStream* self);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_NETWORK_STREAM_H_ */
