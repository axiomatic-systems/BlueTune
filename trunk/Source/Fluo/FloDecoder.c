/*****************************************************************
|
|   Fluo - Decoder
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "FloConfig.h"
#include "FloTypes.h"
#include "FloBitStream.h"
#include "FloDecoder.h"
#include "FloFrame.h"
#include "FloHeaders.h"

/*----------------------------------------------------------------------
|   external decoder engine
+---------------------------------------------------------------------*/
#define FLO_DECODER_ENGINE_MPG123 1
#define FLO_DECODER_ENGINE_FFMPEG 2

#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
#include "mpg123.h"
#include "mpglib.h"
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
#include <stdio.h>
#include "avcodec.h"
#else
#error FLO_DECODER_ENGINE not defined
#endif

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define FLO_INPUT_BUFFER_SIZE   2048
#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
/*#define FLO_MPG123_OUTPUT_SAMPLE_COUNT 1152*/
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
#define FLO_FFMPEG_OUTPUT_SAMPLE_COUNT 1152
#endif
#define FLO_LAYER3_DECODER_DELAY 528
#define FLO_LAYER2_DECODER_DELAY 240

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef enum {
    /* in this state, we are looking for a frame */
    FLO_DECODER_STATE_NEEDS_FRAME, 

    /* re-sync means that the next frame will be skipped because we */
    /* need at least 2 consecutive frames to re-sync                */
    FLO_DECODER_STATE_NEEDS_FRAME_RESYNC, 

    /* we enter this state when a valid frame has been found */
    FLO_DECODER_STATE_HAS_FRAME
} FLO_DecoderState;

struct FLO_Decoder {
    FLO_DecoderState  state;
    FLO_BitStream     bits;
    FLO_FrameInfo     frame_info;
    FLO_VbrToc        vbr_toc;
    FLO_DecoderStatus status;
    unsigned char     input_buffer[FLO_INPUT_BUFFER_SIZE];
    FLO_Cardinal      samples_to_skip;
#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
    MPSTR /*struct mpstr*/             mpg123_decoder;
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
    AVCodec*          ffmpeg_decoder_module;
    AVCodecContext*   ffmpeg_decoder_context;
#endif
};

/*----------------------------------------------------------------------
|   FLO_UpdateBufferSize
+---------------------------------------------------------------------*/
static void
FLO_UpdateBufferSize(FLO_SampleBuffer* buffer)
{
    /* assume 16 bits per sample */
    buffer->size = 
        buffer->sample_count *
        buffer->format.channel_count * 2;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_Create
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_Create(FLO_Decoder** decoder)
{
    *decoder = ATX_AllocateZeroMemory(sizeof(FLO_Decoder));
    if (*decoder == NULL) {
        return FLO_ERROR_OUT_OF_MEMORY;
    }

    /* initialize the state */
    (*decoder)->state = FLO_DECODER_STATE_NEEDS_FRAME;

    /* initialize the bitstream */
    FLO_BitStream_Create(&(*decoder)->bits);

#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
    /* initialize the mpg123 library */
    MPGLIB_Init(&(*decoder)->mpg123_decoder);
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
    /* initialize the ffmpeg decoder */
    (*decoder)->ffmpeg_decoder_context = avcodec_alloc_context();
    (*decoder)->ffmpeg_decoder_module = &mp3_decoder;
    avcodec_open((*decoder)->ffmpeg_decoder_context, 
                 (*decoder)->ffmpeg_decoder_module);
#endif

    /* set some default values */
    (*decoder)->status.stream_info.decoder_delay = 0;
    (*decoder)->samples_to_skip = 0;

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_Destroy
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_Destroy(FLO_Decoder* decoder)
{
#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
    MPGLIB_Exit(&decoder->mpg123_decoder);
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
    avcodec_close(decoder->ffmpeg_decoder_context);
    free((void*)decoder->ffmpeg_decoder_context);
#endif
    FLO_BitStream_Destroy(&decoder->bits);
    ATX_FreeMemory(decoder);

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_GetStatus
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_GetStatus(FLO_Decoder* decoder, FLO_DecoderStatus** status)
{
    *status = &decoder->status;
    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_Feed
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_Feed(FLO_Decoder*   decoder, 
                 FLO_ByteBuffer buffer, 
                 FLO_Size*      size,
                 FLO_Flags      flags)
{
    FLO_Size free_space;

    /* set flags */
    if (flags & FLO_DECODER_BUFFER_IS_END_OF_STREAM) {
        decoder->bits.flags |= FLO_BITSTREAM_FLAG_EOS;
    }

    /* possible shortcut */
    if (*size == 0) return FLO_SUCCESS;

    /* see how much data we can write */
    free_space = FLO_BitStream_GetBytesFree(&decoder->bits);
    if (*size > free_space) *size = free_space;

    /* write the data */
    return FLO_BitStream_WriteBytes(&decoder->bits, buffer, *size); 
}

/*----------------------------------------------------------------------
|   FLO_Decoder_Flush
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_Flush(FLO_Decoder* decoder)
{
    /* reset the bitstream */
    FLO_BitStream_Reset(&decoder->bits);

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_SetSample
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_SetSample(FLO_Decoder* decoder, FLO_Int64 sample)
{
    decoder->status.sample_count = sample;
    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_CallExternalEngine
+---------------------------------------------------------------------*/
static FLO_Result
FLO_Decoder_CallExternalEngine(FLO_Decoder* decoder, FLO_SampleBuffer* buffer)
{
    int result;

    /* read the frame in a buffer */
    FLO_BitStream_ReadBytes(&decoder->bits, 
                            decoder->input_buffer, 
                            decoder->frame_info.size);

    /* decode the frame */
#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
    result = MPGLIB_DecodeFrame(&decoder->mpg123_decoder,
                                decoder->input_buffer,
                                buffer->samples);
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
    {   
        int ffmpeg_frame_size;
        avcodec_decode_audio(decoder->ffmpeg_decoder_context,
                             buffer->samples,
                             &ffmpeg_frame_size,
                             decoder->input_buffer,
                             decoder->frame_info.size);
        if (ffmpeg_frame_size == 0) {
            avcodec_decode_audio(decoder->ffmpeg_decoder_context,
                                 buffer->samples,
                                 &ffmpeg_frame_size,
                                 NULL, 0);
            if (ffmpeg_frame_size == 0) {
                result = FLO_FAILURE;
            } else {
                result = FLO_SUCCESS;
            }
        } else {
            result = FLO_SUCCESS;
        }
    }
#endif

    /* set the buffer info */
    buffer->size = 
        decoder->frame_info.channel_count *
        decoder->frame_info.sample_count * 2;
    buffer->sample_count           = decoder->frame_info.sample_count;
    buffer->format.type            = FLO_SAMPLE_TYPE_INTERLACED_SIGNED;
    buffer->format.sample_rate     = decoder->frame_info.sample_rate;
    buffer->format.channel_count   = decoder->frame_info.channel_count;
    buffer->format.bits_per_sample = 16;

    /* return the result */
#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
    if (result != MP3_OK) {
        /*printf("----------------- result = %d\n", result);*/
    }
    if (result == MP3_OK) {
        return FLO_SUCCESS;
    } else if (result == MP3_NEED_MORE) {
        return FLO_ERROR_NOT_ENOUGH_DATA;
    } else if (result == MP3_INVALID_BITS) {
        return FLO_ERROR_CORRUPTED_BITSTREAM;
    } else {
        return FLO_FAILURE;
    }
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
    return result;
#endif
}

/*----------------------------------------------------------------------
|   FLO_Decoder_ResetExternalEngine
+---------------------------------------------------------------------*/
static void
FLO_Decoder_ResetExternalEngine(FLO_Decoder* decoder)
{
#if (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_MPG123)
    MPGLIB_Reset(&decoder->mpg123_decoder);
#elif (FLO_DECODER_ENGINE == FLO_DECODER_ENGINE_FFMPEG)
    avcodec_close(decoder->ffmpeg_decoder_context);
    free((void*)decoder->ffmpeg_decoder_context);
    decoder->ffmpeg_decoder_context = avcodec_alloc_context();
    avcodec_open(decoder->ffmpeg_decoder_context, 
                 decoder->ffmpeg_decoder_module);    
#endif
}

/*----------------------------------------------------------------------
|   FLO_Decoder_FindFrame
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_FindFrame(FLO_Decoder* decoder, FLO_FrameInfo* frame_info)
{
    FLO_Result result;

    /* see if we already have a current frame */
    if (decoder->state == FLO_DECODER_STATE_HAS_FRAME) {
        if (frame_info) *frame_info = decoder->frame_info;
        return FLO_SUCCESS;
    }

    /* find a valid frame header */
    result = FLO_BitStream_FindFrame(&decoder->bits, &decoder->frame_info);
    if (result == ATX_SUCCESS) {
        /*ATX_Debug("FIND-FRAME: [%d] - br=%d ch=%d lev=%d lay=%d mode=%d sr=%d\n",
            decoder->state,
            decoder->frame_info.bitrate,
            decoder->frame_info.channel_count,
            decoder->frame_info.level,
            decoder->frame_info.layer,
            decoder->frame_info.mode,
            decoder->frame_info.sample_rate);*/
    }

    if (result == FLO_ERROR_CORRUPTED_BITSTREAM &&
        decoder->state == FLO_DECODER_STATE_NEEDS_FRAME) {
        /* we lost sync, go into resync mode */
        FLO_Decoder_ResetExternalEngine(decoder);
        decoder->state = FLO_DECODER_STATE_NEEDS_FRAME_RESYNC;
    }
    if (FLO_FAILED(result)) return result;

    /* if requested, return a copy of the frame info */
    if (frame_info) {
        *frame_info = decoder->frame_info;
    }

    /* update our state */
    if (decoder->state == FLO_DECODER_STATE_NEEDS_FRAME_RESYNC) {
        /* ignore this frame, and move to next state */

        /* skip the frame */
        FLO_BitStream_SkipBytes(&decoder->bits, decoder->frame_info.size);

        decoder->state = FLO_DECODER_STATE_NEEDS_FRAME;
        decoder->status.frame_count++;
        return FLO_ERROR_FRAME_SKIPPED;
    } else {
        /* we're resync-ed and have a valid frame */
        decoder->state = FLO_DECODER_STATE_HAS_FRAME;
    }

    /* some things need to be done on the first frame only */
    if (decoder->status.frame_count == 0) {
        /* update decoder delay value */
        if (decoder->frame_info.layer == FLO_MPEG_LAYER_III) {
            decoder->status.stream_info.decoder_delay = 
                FLO_LAYER3_DECODER_DELAY;
        } else {
            decoder->status.stream_info.decoder_delay = 
                FLO_LAYER2_DECODER_DELAY;
        }

        /* check for VBR or other headers */
        result = FLO_Headers_Parse(&decoder->frame_info, 
                                   &decoder->bits, 
                                   &decoder->status,
                                   &decoder->vbr_toc);
        if (FLO_SUCCEEDED(result)) {
            /* use the encoder delay information */
            decoder->samples_to_skip = 
                decoder->status.stream_info.decoder_delay +
                decoder->status.stream_info.encoder_delay + 1;

            /* count the frame, but skip it */
            decoder->status.frame_count++;
            FLO_Decoder_SkipFrame(decoder);
            return FLO_ERROR_FRAME_SKIPPED;
        }
    }

    /* count the frame */
    decoder->status.frame_count++;

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_SkipFrame
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_SkipFrame(FLO_Decoder* decoder)
{
    FLO_Size available;

    /* check our state */
    if (decoder->state != FLO_DECODER_STATE_HAS_FRAME) {
        return FLO_ERROR_INVALID_DECODER_STATE;
    }
    
    /* check that we have enough data to skip */
    available = FLO_BitStream_GetBytesAvailable(&decoder->bits);
    if (available < decoder->frame_info.size) {
        return FLO_ERROR_NOT_ENOUGH_DATA;
    }

    /* skip the frame */
    FLO_BitStream_SkipBytes(&decoder->bits, decoder->frame_info.size);

    /* we need a new frame */
    decoder->state = FLO_DECODER_STATE_NEEDS_FRAME;

    return FLO_SUCCESS;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_DecodeFrame
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_DecodeFrame(FLO_Decoder*      decoder, 
                        FLO_SampleBuffer* buffer,
                        FLO_Cardinal*     samples_skipped)
{
    ATX_Int64  samples_left = 0;
    FLO_Result result;

    /* default values */
    buffer->sample_count = 0;
    buffer->size = 0;
    if (samples_skipped) *samples_skipped = 0;

    /* check that we have not already decoded all the samples */
    if (decoder->status.flags & FLO_DECODER_STATUS_STREAM_HAS_INFO &&
        decoder->status.stream_info.duration_samples != 0) {
        if (decoder->status.sample_count >= 
            decoder->status.stream_info.duration_samples) {
            /* we have already decoded all the frames */
            return FLO_ERROR_NO_MORE_SAMPLES;
        } else {
            /* we may need to decode less than a full frame */
            samples_left =
                decoder->status.stream_info.duration_samples - 
                decoder->status.sample_count;
        }
    }

    /* find a frame if we need one */
    if (decoder->state == FLO_DECODER_STATE_NEEDS_FRAME) {
        result = FLO_Decoder_FindFrame(decoder, NULL);
        if (FLO_FAILED(result)) return result;
    }

    /* decode the frame */
    result = FLO_Decoder_CallExternalEngine(decoder, buffer);

    /* skip samples caused by encoder and decoder delays */
    if (decoder->samples_to_skip != 0) {
        if (buffer->sample_count <= decoder->samples_to_skip) {
            /* skip the entire buffer */
            if (samples_skipped) *samples_skipped = buffer->sample_count;
            decoder->samples_to_skip -= buffer->sample_count;
            buffer->sample_count = 0;
            buffer->size = 0;
            return FLO_ERROR_SAMPLES_SKIPPED;
        } else {
            /* skip part of the buffer */
            if (samples_skipped) *samples_skipped = decoder->samples_to_skip;
            buffer->sample_count -= decoder->samples_to_skip;
            FLO_UpdateBufferSize(buffer);
            buffer->samples = 
                ((short*)buffer->samples) +
                (decoder->samples_to_skip * 
                 buffer->format.channel_count);
            decoder->samples_to_skip = 0;
        }
    }

    /* truncate and update the sample count */
    if (FLO_SUCCEEDED(result)) {
        /* clip the size of the buffer if needed */
        if (samples_left != 0 && samples_left < buffer->sample_count) {
            buffer->sample_count = ATX_Int64_Get_Int32(samples_left);
            FLO_UpdateBufferSize(buffer);
            if (buffer->sample_count == 0) {
                return FLO_ERROR_SAMPLES_SKIPPED;
            }
        }

        /* count the samples */
        ATX_Int64_Add_Int32(decoder->status.sample_count, 
                            buffer->sample_count);
    }

    /* update the state */      
    if (result != FLO_ERROR_NOT_ENOUGH_DATA) {
        decoder->state = FLO_DECODER_STATE_NEEDS_FRAME;
    }

    return result;
}

/*----------------------------------------------------------------------
|   FLO_Decoder_Reset
+---------------------------------------------------------------------*/
FLO_Result 
FLO_Decoder_Reset(FLO_Decoder* decoder)
{
    /* reset the external engine */
    FLO_Decoder_ResetExternalEngine(decoder);

    /* reset some of the decoder state */
    decoder->status.frame_count = 0;
    decoder->status.sample_count = 0;
    if (decoder->state == FLO_DECODER_STATE_HAS_FRAME) {
        decoder->state = FLO_DECODER_STATE_NEEDS_FRAME;
    }

    /* reset the skip count */
    decoder->samples_to_skip = 0;

    return FLO_SUCCESS;
}
