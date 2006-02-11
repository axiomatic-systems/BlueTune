/*****************************************************************
|
|      File: FloBitStream.h
|
|      Fluo - Bit Streams
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Fluo - Bit Streams
 */

#ifndef _FLO_BIT_STREAM_H_
#define _FLO_BIT_STREAM_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "FloTypes.h"
#include "FloErrors.h"
#include "FloFrame.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/

/* smallest power of 2 that can hold any type of frame */
#define FLO_BITSTREAM_BUFFER_SIZE  2048

/* flags */
#define FLO_BITSTREAM_FLAG_EOS 0x01

/* error codes */
#define FLO_ERROR_NOT_ENOUGH_DATA      (FLO_ERROR_BASE_BITSTREAM - 0)
#define FLO_ERROR_CORRUPTED_BITSTREAM  (FLO_ERROR_BASE_BITSTREAM - 1)

/*----------------------------------------------------------------------
|       types
+---------------------------------------------------------------------*/
typedef struct {
    unsigned char* buffer;
    unsigned int   in;
    unsigned int   out;
    unsigned int   flags;
} FLO_BitStream;

/*----------------------------------------------------------------------
|       prototypes
+---------------------------------------------------------------------*/
FLO_Result   FLO_BitStream_Create(FLO_BitStream* bits);
FLO_Result   FLO_BitStream_Destroy(FLO_BitStream* bits);
FLO_Result   FLO_BitStream_Reset(FLO_BitStream* bits);
FLO_Result   FLO_BitStream_Attach(FLO_BitStream* bits, FLO_BitStream* shadow);
unsigned int FLO_BitStream_GetContiguousBytesFree(FLO_BitStream* bits);
unsigned int FLO_BitStream_GetBytesFree(FLO_BitStream* bits);
FLO_Result   FLO_BitStream_WriteBytes(FLO_BitStream*       bits, 
                                      const unsigned char* bytes, 
                                      unsigned int         byte_count);
unsigned int FLO_BitStream_GetContiguousBytesAvailable(FLO_BitStream* bits);
unsigned int FLO_BitStream_GetBytesAvailable(FLO_BitStream* bits);
FLO_Result   FLO_BitStream_ReadBytes(FLO_BitStream* bits, 
                                     unsigned char* bytes, 
                                     unsigned int   byte_count);
FLO_Result   FLO_BitStream_SkipBytes(FLO_BitStream* bits, 
                                     unsigned int   byte_count);
FLO_Result   FLO_BitStream_FindFrame(FLO_BitStream* bits, 
                                     FLO_FrameInfo* frame_info);

#endif /* _FLO_BIT_STREAM_H_ */
