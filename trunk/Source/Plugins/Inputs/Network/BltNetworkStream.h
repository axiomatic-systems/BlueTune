/*****************************************************************
|
|   BlueTune - Network Stream
|
|   (c) 2002-2012 Gilles Boccon-Gibod
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

#define BLT_NETWORK_STREAM_BUFFER_EOS_PROPERTY      "NetworkStream.EndOfStream"
#define BLT_NETWORK_STREAM_BUFFER_SIZE_PROPERTY     "NetworkStream.BufferSize"
#define BLT_NETWORK_STREAM_BUFFER_FULLNESS_PROPERTY "NetworkStream.BufferFullness"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct BLT_NetworkStream BLT_NetworkStream;

typedef struct {
    unsigned int buffer_size;
    unsigned int buffer_fullness;
    unsigned int end_of_stream;
} BLT_BufferedNetworkStreamStatus;

/*----------------------------------------------------------------------
|   BLT_BufferedNetworkStream
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_BufferedNetworkStream)
ATX_BEGIN_INTERFACE_DEFINITION(BLT_BufferedNetworkStream)
    BLT_Result (*FillBuffer)(BLT_BufferedNetworkStream* self);
    BLT_Result (*GetStatus)(BLT_BufferedNetworkStream*       self, 
                            BLT_BufferedNetworkStreamStatus* status);
ATX_END_INTERFACE_DEFINITION

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
BLT_Result 
BLT_BufferedNetworkStream_FillBuffer(BLT_BufferedNetworkStream* self);

BLT_Result 
BLT_BufferedNetworkStream_GetStatus(BLT_BufferedNetworkStream*       self, 
                                    BLT_BufferedNetworkStreamStatus* status);

BLT_Result 
BLT_NetworkStream_Create(BLT_Size            size,
                         BLT_Size            min_buffer_fullness,
                         ATX_InputStream*    source, 
                         BLT_NetworkStream** stream);

ATX_InputStream*
BLT_NetworkStream_GetInputStream(BLT_NetworkStream* self);

BLT_Result
BLT_NetworkStream_Release(BLT_NetworkStream* self);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_NETWORK_STREAM_H_ */
