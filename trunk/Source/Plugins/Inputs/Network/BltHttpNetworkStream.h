/*****************************************************************
|
|   BlueTune - HTTP Network Stream
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
****************************************************************/

#ifndef _BLT_HTTP_NETWORK_STREAM_H_
#define _BLT_HTTP_NETWORK_STREAM_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"
#include "BltMedia.h"
#include "BltNetworkInputSource.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_HTTP_NETWORK_STREAM_BUFFER_SIZE_PROPERTY      "NetworkStream.BufferSize"
#define BLT_HTTP_NETWORK_STREAM_MINIMUM_FULLNESS_PROPERTY "NetworkStream.MinimumFullness"

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
#if defined(__cplusplus)
extern "C" {
#endif

BLT_Result 
BLT_HttpNetworkStream_Create(const char*              url, 
                             BLT_Core*                core,
                             ATX_InputStream**        stream,
                             BLT_NetworkInputSource** source,
                             BLT_MediaType**          media_type);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_HTTP_NETWORK_STREAM_H_ */
