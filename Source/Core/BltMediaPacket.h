/*****************************************************************
|
|   BlueTune - Media Packet Interface
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune MediaPacket Interface Header file
 */

#ifndef _BLT_MEDIA_PACKET_H_
#define _BLT_MEDIA_PACKET_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltMedia.h"
#include "BltTime.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct BLT_MediaPacket BLT_MediaPacket;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
/**
 * This flag indicates that this packet contains the start of a stream
 * (the first byte of the payload must be the start of the stream, or the
 * payload must be empty)
 */
#define BLT_MEDIA_PACKET_FLAG_START_OF_STREAM           0x01

/**
 * This flag indicates that this packet contains the end of a stream
 * (the last byte of the payload must be the end of the stream, or the
 * payload must be empty)
 */
#define BLT_MEDIA_PACKET_FLAG_END_OF_STREAM             0x02

/**
 * This flag indicates that there has been a discontinuity in the stream
 * (packet loss, or seek)
 */
#define BLT_MEDIA_PACKET_FLAG_STREAM_DISCONTINUITY      0x04

/**
 * This flag indicates that the payload of the packet represents stream
 * metadata (such as a stream header). It is not required that the packet
 * payload only contains metadata.
 */
#define BLT_MEDIA_PACKET_FLAG_STREAM_METADATA           0x08

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
#if defined(__cplusplus)
extern "C" {
#endif

BLT_Result BLT_MediaPacket_AddReference(BLT_MediaPacket* packet);
BLT_Result BLT_MediaPacket_Release(BLT_MediaPacket* packet);
BLT_Any    BLT_MediaPacket_GetPayloadBuffer(BLT_MediaPacket* packet);
BLT_Result BLT_MediaPacket_SetPayloadWindow(BLT_MediaPacket* packet,
                                            BLT_Offset       offset,
                                            BLT_Size         size);
BLT_Size   BLT_MediaPacket_GetPayloadSize(BLT_MediaPacket* packet);
BLT_Result BLT_MediaPacket_SetPayloadSize(BLT_MediaPacket* packet,
                                          BLT_Size         size);
BLT_Size   BLT_MediaPacket_GetAllocatedSize(BLT_MediaPacket* packet);
BLT_Result BLT_MediaPacket_SetAllocatedSize(BLT_MediaPacket* packet,
                                            BLT_Size         size);
BLT_Offset BLT_MediaPacket_GetPayloadOffset(BLT_MediaPacket* packet);
BLT_Result BLT_MediaPacket_SetPayloadOffset(BLT_MediaPacket* packet,
                                            BLT_Offset       offset);
BLT_Result BLT_MediaPacket_GetMediaType(BLT_MediaPacket* packet,
                                        const BLT_MediaType** type);
BLT_Result BLT_MediaPacket_SetMediaType(BLT_MediaPacket*     packet,
                                        const BLT_MediaType* type);
BLT_Result BLT_MediaPacket_SetTimeStamp(BLT_MediaPacket* packet,
                                        BLT_TimeStamp    time_stamp);
BLT_TimeStamp BLT_MediaPacket_GetTimeStamp(BLT_MediaPacket* packet);
BLT_Time      BLT_MediaPacket_GetDuration(BLT_MediaPacket* packet);
BLT_Result    BLT_MediaPacket_SetDuration(BLT_MediaPacket* packet,
                                          BLT_Time         duration);
BLT_Result BLT_MediaPacket_SetFlags(BLT_MediaPacket* packet, BLT_Flags flags);
BLT_Result BLT_MediaPacket_ClearFlags(BLT_MediaPacket* packet, 
                                      BLT_Flags        flags);
BLT_Result BLT_MediaPacket_ResetFlags(BLT_MediaPacket* packet);
BLT_Flags  BLT_MediaPacket_GetFlags(BLT_MediaPacket* packet);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_MEDIA_PACKET_H_ */
