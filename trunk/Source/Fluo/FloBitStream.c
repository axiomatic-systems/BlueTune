/*****************************************************************
|
|   File: FloBitStream.c
|
|   Fluo - Bit Streams
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   For efficiency reasons, this bitstream library only handles
|   data buffers that are a power of 2 in size
+---------------------------------------------------------------------*/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "FloConfig.h"
#include "FloTypes.h"
#include "FloBitStream.h"
#include "FloFrame.h"

/*----------------------------------------------------------------------
|   macros
+---------------------------------------------------------------------*/
#define FLO_BITSTREAM_POINTER_VAL(offset) \
    ((offset)&(FLO_BITSTREAM_BUFFER_SIZE-1))

#define FLO_BITSTREAM_POINTER_OFFSET(pointer, offset) \
    (FLO_BITSTREAM_POINTER_VAL((pointer)+(offset)))

#define FLO_BITSTREAM_POINTER_ADD(pointer, offset) \
    ((pointer) = FLO_BITSTREAM_POINTER_OFFSET(pointer, offset))

/* mask the bits for header params that should not change */
#define FLO_BITSTREAM_HEADER_COMPAT_MASK 0xFFFF0D0F

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define FLO_FHG_VBRI_OFFSET 36

/*----------------------------------------------------------------------
|   FLO_BitStream_Create
+---------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_Create(FLO_BitStream* bits)
{
    bits->buffer = 
        (unsigned char*)ATX_AllocateMemory(FLO_BITSTREAM_BUFFER_SIZE);
    if (bits->buffer == NULL) return FLO_ERROR_OUT_OF_MEMORY;

    FLO_BitStream_Reset(bits);

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_BitStream_Destroy
+---------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_Destroy(FLO_BitStream* bits)
{
    FLO_BitStream_Reset(bits);
    if (bits->buffer != NULL) {
        ATX_FreeMemory(bits->buffer);
        bits->buffer = 0;
    }

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_BitStream_Reset
+---------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_Reset(FLO_BitStream* bits)
{
    bits->in    = 0;
    bits->out   = 0;
    bits->flags = 0;

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_BitStream_Attach
+---------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_Attach(FLO_BitStream* bits, FLO_BitStream* shadow)
{
    *shadow = *bits;
    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_BitStream_GetContiguousBytesFree
+---------------------------------------------------------------------*/
unsigned int 
FLO_BitStream_GetContiguousBytesFree(FLO_BitStream* bits)
{
    return 
        (bits->in < bits->out) ?
        (bits->out - bits->in - 1) :
        (FLO_BITSTREAM_BUFFER_SIZE - bits->in);
}

/*----------------------------------------------------------------------
|   FLO_BitStream_GetBytesFree
+---------------------------------------------------------------------*/
unsigned int 
FLO_BitStream_GetBytesFree(FLO_BitStream* bits)
{
    return  
        (bits->in < bits->out) ? 
        (bits->out - bits->in - 1) : 
        (FLO_BITSTREAM_BUFFER_SIZE  + (bits->out - bits->in) - 1);
}

/*----------------------------------------------------------------------+
|    FLO_BitStream_WriteBytes
+----------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_WriteBytes(FLO_BitStream*       bits, 
                         const unsigned char* bytes, 
                         unsigned int         byte_count)
{
    if (byte_count == 0) return FLO_SUCCESS;
    if (bytes == NULL) return FLO_ERROR_INVALID_PARAMETERS;

    if (bits->in < bits->out) {
        ATX_CopyMemory(bits->buffer+bits->in, bytes, byte_count);
        FLO_BITSTREAM_POINTER_ADD(bits->in, byte_count);
    } else {
        unsigned int chunk = FLO_BITSTREAM_BUFFER_SIZE - bits->in;
        if (chunk > byte_count) chunk = byte_count;

        ATX_CopyMemory(bits->buffer+bits->in, bytes, chunk);
        FLO_BITSTREAM_POINTER_ADD(bits->in, chunk);

        if (chunk != byte_count) {
            ATX_CopyMemory(bits->buffer+bits->in, 
                           bytes+chunk, byte_count-chunk);
            FLO_BITSTREAM_POINTER_ADD(bits->in, byte_count-chunk);
        }
    }

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_BitStream_GetContiguousBytesAvailable
+---------------------------------------------------------------------*/
unsigned int 
FLO_BitStream_GetContiguousBytesAvailable(FLO_BitStream* bits)
{
    return 
        (bits->out <= bits->in) ? 
        (bits->in - bits->out) :
        (FLO_BITSTREAM_BUFFER_SIZE - bits->out);
}

/*----------------------------------------------------------------------
|   FLO_BitStream_GetBytesAvailable
+---------------------------------------------------------------------*/
unsigned int 
FLO_BitStream_GetBytesAvailable(FLO_BitStream* bits)
{
    return 
        (bits->out <= bits->in) ? 
        (bits->in - bits->out) :
        (bits->in + (FLO_BITSTREAM_BUFFER_SIZE - bits->out));
}

/*----------------------------------------------------------------------+
|    FLO_BitStream_ReadBytes
+----------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_ReadBytes(FLO_BitStream* bits, 
                        unsigned char* bytes, 
                        unsigned int   byte_count)
{
    if (byte_count == 0 || bytes == NULL) {
        return FLO_ERROR_INVALID_PARAMETERS;
    }
    if (bits->in > bits->out) {
        ATX_CopyMemory(bytes, bits->buffer+bits->out, byte_count);
        FLO_BITSTREAM_POINTER_ADD(bits->out, byte_count);
    } else {
        unsigned int chunk = FLO_BITSTREAM_BUFFER_SIZE - bits->out;
        if (chunk >= byte_count) chunk = byte_count;

        ATX_CopyMemory(bytes, bits->buffer+bits->out, chunk);
        FLO_BITSTREAM_POINTER_ADD(bits->out, chunk);

        if (chunk != byte_count) {
            ATX_CopyMemory(bytes+chunk, 
                           bits->buffer+bits->out, 
                           byte_count-chunk);
            FLO_BITSTREAM_POINTER_ADD(bits->out, byte_count-chunk);
        }
    }

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------+
|    FLO_BitStream_SkipBytes
+----------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_SkipBytes(FLO_BitStream* bits, unsigned int byte_count)
{
    FLO_BITSTREAM_POINTER_ADD(bits->out, byte_count);
    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------+
|    FLO_BitStream_AlignToByte
+----------------------------------------------------------------------*/
static FLO_Result
FLO_BitStream_AlignToByte(FLO_BitStream* bits)
{
    ATX_COMPILER_UNUSED(bits);
    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------+
|    FLO_BitStream_FindHeader
+----------------------------------------------------------------------*/
static FLO_Result
FLO_BitStream_FindHeader(FLO_BitStream* bits, unsigned long* header)
{
    unsigned long packed;
    int           available = FLO_BitStream_GetBytesAvailable(bits);
    unsigned int  current   = bits->out;

    /* read the first 32 bits */
    if (available < 4) return FLO_ERROR_NOT_ENOUGH_DATA;
    packed =                 bits->buffer[current];
    FLO_BITSTREAM_POINTER_ADD(current, 1);
    packed = (packed << 8) | bits->buffer[current];
    FLO_BITSTREAM_POINTER_ADD(current, 1);
    packed = (packed << 8) | bits->buffer[current];
    FLO_BITSTREAM_POINTER_ADD(current, 1);
    packed = (packed << 8) | bits->buffer[current];
    FLO_BITSTREAM_POINTER_ADD(current, 1);
    available -= 4;

    /* read until we find a header or run out of data */
    for (;/* ever */;) {
        /* check if we have a sync word */
        if (((packed >> (32 - FLO_SYNTAX_MPEG_SYNC_WORD_BIT_LENGTH)) & 
             ((1<<FLO_SYNTAX_MPEG_SYNC_WORD_BIT_LENGTH)-1)) == 
            FLO_SYNTAX_MPEG_SYNC_WORD) {
            /* rewind to start of header */
            bits->out = FLO_BITSTREAM_POINTER_OFFSET(current, -4);

            /* return the header */
            *header = packed;
            return FLO_SUCCESS;
        }

        /* move on to the next byte */
        if (available > 0) {
            packed = (packed << 8) | bits->buffer[current];
            FLO_BITSTREAM_POINTER_ADD(current, 1);
            available --;
        } else {
            break;
        }
    }

    /* discard all the bytes we've already scanned, except the last 3 */
    bits->out = FLO_BITSTREAM_POINTER_OFFSET(current, -3);

    return FLO_ERROR_NOT_ENOUGH_DATA;
}

/*----------------------------------------------------------------------+
|    FLO_BitStream_FindFrame
+----------------------------------------------------------------------*/
FLO_Result
FLO_BitStream_FindFrame(FLO_BitStream* bits, FLO_FrameInfo* frame_info)
{
    unsigned int    available;
    unsigned long   packed;
    FLO_FrameHeader frame_header;
    FLO_Result      result;

    /* align to the start of the next byte */
    FLO_BitStream_AlignToByte(bits);
    
    /* find a frame header */
    result = FLO_BitStream_FindHeader(bits, &packed);
    if (FLO_FAILED(result)) return result;

    /* unpack the header */
    FLO_FrameHeader_Unpack(packed, &frame_header);

    /* check the header */
    result = FLO_FrameHeader_Check(&frame_header);
    if (FLO_FAILED(result)) {
        /* skip the header and return */
        FLO_BITSTREAM_POINTER_ADD(bits->out, 1);
        return FLO_ERROR_CORRUPTED_BITSTREAM;
    }

    /* get the frame info */
    FLO_FrameHeader_GetInfo(&frame_header, frame_info);

    /* check that we have enough data */
    available = FLO_BitStream_GetBytesAvailable(bits);
    if (bits->flags & FLO_BITSTREAM_FLAG_EOS) {
        /* we're at the end of the stream, we only need the entire frame */
        if (available < frame_info->size) {
            return FLO_ERROR_NOT_ENOUGH_DATA;
        } 
    } else {
        /* peek at the header of the next frame */
        unsigned int    peek_offset;
        unsigned long   peek_packed;
        FLO_FrameHeader peek_header;

        if (available < frame_info->size+4) {
            return FLO_ERROR_NOT_ENOUGH_DATA;
        } 
        peek_offset = FLO_BITSTREAM_POINTER_OFFSET(bits->out, 
                                                   frame_info->size);
        peek_packed =                      bits->buffer[peek_offset];
        FLO_BITSTREAM_POINTER_ADD(peek_offset, 1);
        peek_packed = (peek_packed << 8) | bits->buffer[peek_offset];
        FLO_BITSTREAM_POINTER_ADD(peek_offset, 1);
        peek_packed = (peek_packed << 8) | bits->buffer[peek_offset];
        FLO_BITSTREAM_POINTER_ADD(peek_offset, 1);
        peek_packed = (peek_packed << 8) | bits->buffer[peek_offset];
        /* check the following header */
        FLO_FrameHeader_Unpack(peek_packed, &peek_header);
        result = FLO_FrameHeader_Check(&peek_header);
        if (FLO_FAILED(result) || 
            ((peek_packed & FLO_BITSTREAM_HEADER_COMPAT_MASK) != 
             (packed      & FLO_BITSTREAM_HEADER_COMPAT_MASK))) {
            /* the next header is invalid, or incompatible, so we reject  */
            /* this one, unless it has an FHG VBRI header.                */
            /* it is necessary to check for the VBRI header because the   */
            /* MusicMatch encoder puts the VBRI header in the first frame */
            /* but incorrectly computes the frame size, so there is       */
            /* garbage at the end of the frame, which would cause it to   */
            /* get rejected here...                                       */
            char header[4];
            peek_offset = FLO_BITSTREAM_POINTER_OFFSET(bits->out,
                                                       FLO_FHG_VBRI_OFFSET);
            header[0] = bits->buffer[peek_offset];
            FLO_BITSTREAM_POINTER_ADD(peek_offset, 1);
            header[1] = bits->buffer[peek_offset];
            FLO_BITSTREAM_POINTER_ADD(peek_offset, 1);
            header[2] = bits->buffer[peek_offset];
            FLO_BITSTREAM_POINTER_ADD(peek_offset, 1);
            header[3] = bits->buffer[peek_offset];
            if (header[0] != 'V' || 
                header[1] != 'B' ||
                header[2] != 'R' ||
                header[3] != 'I') {
                FLO_BITSTREAM_POINTER_ADD(bits->out, 1);
                return FLO_ERROR_CORRUPTED_BITSTREAM;
            }
        }
    }

    return FLO_SUCCESS;
}
