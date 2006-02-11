/*****************************************************************
|
|      File: BltMpegAudioDecoder.c
|
|      MpegAudio Decoder Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "Fluo.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMpegAudioDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltReplayGain.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_BITRATE_AVERAGING_SHORT_SCALE     7
#define BLT_BITRATE_AVERAGING_SHORT_WINDOW    32
#define BLT_BITRATE_AVERAGING_LONG_SCALE      7
#define BLT_BITRATE_AVERAGING_LONG_WINDOW     4096
#define BLT_BITRATE_AVERAGING_PRECISION       4000

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderModule)
static const BLT_ModuleInterface MpegAudioDecoderModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoder)
static const BLT_MediaNodeInterface MpegAudioDecoder_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderInputPort)

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderOutputPort)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
    BLT_UInt32     mpeg_audio_type_id;
} MpegAudioDecoderModule;

typedef struct {
    BLT_Boolean eos;
    ATX_List*   packets;
} MpegAudioDecoderInputPort;

typedef struct {
    BLT_Boolean      eos;
    BLT_PcmMediaType media_type;
    BLT_TimeStamp    time_stamp;
    ATX_Int64        sample_count;
} MpegAudioDecoderOutputPort;

typedef struct {
    BLT_BaseMediaNode          base;
    BLT_Module                 module;
    MpegAudioDecoderInputPort  input;
    MpegAudioDecoderOutputPort output;
    FLO_Decoder*               fluo;
    struct {
        BLT_Cardinal nominal_bitrate;
        BLT_Cardinal average_bitrate;
        ATX_Int64    average_bitrate_accumulator;
        BLT_Cardinal instant_bitrate;
        ATX_Int64    instant_bitrate_accumulator;
    }                          stream_info;
    struct {
        ATX_Flags flags;
        ATX_Int32 track_gain;
        ATX_Int32 album_gain;
    }                          replay_gain_info;
    struct {
        unsigned int level;
        unsigned int layer;
    }                          mpeg_info;
} MpegAudioDecoder;

/*----------------------------------------------------------------------
|    MpegAudioDecoderInputPort_Flush
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoderInputPort_Flush(MpegAudioDecoder* decoder)
{
    ATX_ListItem* item;
    while ((item = ATX_List_GetFirstItem(decoder->input.packets))) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) BLT_MediaPacket_Release(packet);
        ATX_List_RemoveItem(decoder->input.packets, item);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MpegAudioDecoderInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
MpegAudioDecoderInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                                    BLT_MediaPacket*            packet)
{
    MpegAudioDecoder* decoder = (MpegAudioDecoder*)instance;
    ATX_Result        result;

    /* check to see if this is the end of a stream */
    if (BLT_MediaPacket_GetFlags(packet) & 
        BLT_MEDIA_PACKET_FLAG_END_OF_STREAM) {
        decoder->input.eos = BLT_TRUE;
    }

    /* add the packet to the input list */
    result = ATX_List_AddData(decoder->input.packets, packet);
    if (ATX_SUCCEEDED(result)) {
        BLT_MediaPacket_AddReference(packet);
    }

    return result;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(MpegAudioDecoderInputPort, 
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
MpegAudioDecoderInputPort_BLT_MediaPortInterface = {
    MpegAudioDecoderInputPort_GetInterface,
    MpegAudioDecoderInputPort_GetName,
    MpegAudioDecoderInputPort_GetProtocol,
    MpegAudioDecoderInputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
MpegAudioDecoderInputPort_BLT_PacketConsumerInterface = {
    MpegAudioDecoderInputPort_GetInterface,
    MpegAudioDecoderInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderInputPort)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoderInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoderInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderInputPort)

/*----------------------------------------------------------------------
|    MpegAudioDecoder_UpdateInfo
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoder_UpdateInfo(MpegAudioDecoder* decoder,     
                            FLO_FrameInfo*    frame_info)
{
    /* check if the media format has changed */
    if (frame_info->sample_rate   != decoder->output.media_type.sample_rate   ||
        frame_info->channel_count != decoder->output.media_type.channel_count ||
        frame_info->level         != decoder->mpeg_info.level             ||
        frame_info->layer         != decoder->mpeg_info.layer) {

        if (decoder->output.media_type.sample_rate   != 0 ||
            decoder->output.media_type.channel_count != 0) {
            /* format change, discard the packet */
            BLT_Debug("MpegAudioDecoder::UpdateInfo - "
                      "format change, discarding frame\n");
            FLO_Decoder_SkipFrame(decoder->fluo);
            return FLO_ERROR_NOT_ENOUGH_DATA;
        }

        /* keep the new info */
        decoder->mpeg_info.layer = frame_info->layer;
        decoder->mpeg_info.level = frame_info->level;

        /* set the output type extensions */
        BLT_PcmMediaType_Init(&decoder->output.media_type);
        decoder->output.media_type.channel_count   = (BLT_UInt16)frame_info->channel_count;
        decoder->output.media_type.sample_rate     = frame_info->sample_rate;
        decoder->output.media_type.bits_per_sample = 16;
        decoder->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
        
        {
            BLT_StreamInfo info;
            char           data_type[32] = "MPEG-X Layer X";
            data_type[ 5] = '0' + (frame_info->level > 0 ?
                                   frame_info->level : 2);
            data_type[13] = '0' + frame_info->layer;
            info.data_type       = data_type;
            info.sample_rate     = frame_info->sample_rate;
            info.channel_count   = (BLT_UInt16)frame_info->channel_count;

            if (!ATX_OBJECT_IS_NULL(&decoder->base.context)) {
                info.mask = 
                    BLT_STREAM_INFO_MASK_DATA_TYPE    |
                    BLT_STREAM_INFO_MASK_SAMPLE_RATE  |
                    BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
                BLT_Stream_SetInfo(&decoder->base.context, &info);
            }
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_UpdateBitrateAverage
+---------------------------------------------------------------------*/
static BLT_Cardinal
MpegAudioDecoder_UpdateBitrateAverage(BLT_Cardinal previous_bitrate,
                                      BLT_Cardinal current_bitrate,
                                      ATX_Int64*   accumulator,
                                      BLT_Cardinal window,
                                      BLT_Cardinal scale)
{
    BLT_Cardinal new_bitrate;
    long         diff_bitrate;

    if (previous_bitrate == 0) {
        ATX_Int64_Set_Int32(*accumulator, current_bitrate << scale);
        return current_bitrate;
    }

    ATX_Int64_Mul_Int32(*accumulator, window-1);
    ATX_Int64_Add_Int32(*accumulator, current_bitrate << scale);
    ATX_Int64_Div_Int32(*accumulator, window);
    
    new_bitrate = ATX_Int64_Get_Int32(*accumulator);
    new_bitrate = (new_bitrate + (1<<(scale-1))) >> scale;
    new_bitrate = new_bitrate /
        BLT_BITRATE_AVERAGING_PRECISION *
        BLT_BITRATE_AVERAGING_PRECISION;

    /* only update if the difference is more than 1/32th of the previous one */
    diff_bitrate = new_bitrate - previous_bitrate;
    if (diff_bitrate < 0) diff_bitrate = -diff_bitrate;
    if (diff_bitrate > (long)(previous_bitrate>>5)) {
        return new_bitrate;
    } else {
        return previous_bitrate;
    }
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_UpdateDurationAndBitrate
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoder_UpdateDurationAndBitrate(MpegAudioDecoder*  decoder,  
                                          FLO_DecoderStatus* fluo_status,
                                          FLO_FrameInfo*     frame_info)
{
    BLT_StreamInfo info;
    BLT_Result     result;
    
    if (ATX_OBJECT_IS_NULL(&decoder->base.context)) return BLT_SUCCESS;

    /* get the decoder status */
    result = FLO_Decoder_GetStatus(decoder->fluo, &fluo_status);
    if (BLT_FAILED(result)) return result;

    /* get current info */
    BLT_Stream_GetInfo(&decoder->base.context, &info);
    info.mask = 
        BLT_STREAM_INFO_MASK_DURATION        |
        BLT_STREAM_INFO_MASK_AVERAGE_BITRATE;

    /* update the info */
    if (fluo_status->flags & FLO_DECODER_STATUS_STREAM_IS_VBR) {
        info.mask |= BLT_STREAM_INFO_MASK_FLAGS;
        info.flags = BLT_STREAM_INFO_FLAG_VBR;
    }
    if (fluo_status->flags & FLO_DECODER_STATUS_STREAM_HAS_INFO) {
        /* the info was contained in the stream (VBR header) */
        info.nominal_bitrate = fluo_status->stream_info.bitrate;
        info.average_bitrate = fluo_status->stream_info.bitrate;
        info.duration        = fluo_status->stream_info.duration_ms;
        info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;
    } else {
        /* nominal bitrate */
        if (decoder->stream_info.nominal_bitrate == 0) {
            info.nominal_bitrate = frame_info->bitrate;
            decoder->stream_info.nominal_bitrate = frame_info->bitrate;
            info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;
        } 
        
        /* average bitrate */
        {
            info.average_bitrate = MpegAudioDecoder_UpdateBitrateAverage(
                decoder->stream_info.average_bitrate,
                frame_info->bitrate,
                &decoder->stream_info.average_bitrate_accumulator,
                BLT_BITRATE_AVERAGING_LONG_WINDOW,
                BLT_BITRATE_AVERAGING_LONG_SCALE);
            decoder->stream_info.average_bitrate = info.average_bitrate;
        }   

        /* compute the duration */
        if (info.size && info.average_bitrate) {
            ATX_Int64 duration;
            ATX_Int64_Set_Int32(duration, info.size);
            ATX_Int64_Mul_Int32(duration, 8*1000);
            ATX_Int64_Div_Int32(duration, info.average_bitrate);
            info.duration = ATX_Int64_Get_Int32(duration);
        } else {
            info.duration = 0;
        }
    }

    /* the instant bitrate is a short window average */
    {
        info.instant_bitrate = MpegAudioDecoder_UpdateBitrateAverage(
            decoder->stream_info.instant_bitrate,
            frame_info->bitrate,
            &decoder->stream_info.instant_bitrate_accumulator,
            BLT_BITRATE_AVERAGING_SHORT_WINDOW,
            BLT_BITRATE_AVERAGING_SHORT_SCALE);
        decoder->stream_info.instant_bitrate = info.instant_bitrate;
        info.mask |= BLT_STREAM_INFO_MASK_INSTANT_BITRATE;
    }

    /* update the stream info */
    BLT_Stream_SetInfo(&decoder->base.context, &info);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_UpdateReplayGainInfo
+---------------------------------------------------------------------*/
static void
MpegAudioDecoder_UpdateReplayGainInfo(MpegAudioDecoder*  decoder,  
                                      FLO_DecoderStatus* fluo_status)
{
    BLT_Boolean update = BLT_FALSE;

    if (fluo_status->flags & FLO_DECODER_STATUS_STREAM_HAS_REPLAY_GAIN) {
        /* the stream has replay gain info */
        if (decoder->replay_gain_info.flags != 
            fluo_status->replay_gain_info.flags) {
            /* those are new values */
            update = BLT_TRUE;

            /* set the track gain value */
            decoder->replay_gain_info.track_gain = 
                10 * fluo_status->replay_gain_info.track_gain;

            /* set the album gain value */
            decoder->replay_gain_info.album_gain = 
                10 * fluo_status->replay_gain_info.album_gain;

            /* copy the flags */
            decoder->replay_gain_info.flags = fluo_status->replay_gain_info.flags;
        }
    } else {
        /* the stream has no replay gain info */
        if (decoder->replay_gain_info.flags != 0) {
            /* we had values, clear them */
            update = BLT_TRUE;
        }
        decoder->replay_gain_info.flags      = 0; 
        decoder->replay_gain_info.album_gain = 0;
        decoder->replay_gain_info.track_gain = 0;
    }

    /* update the stream properties if necessary */
    if (update == BLT_TRUE) {
        ATX_Properties properties;

        /* get a reference to the stream properties */
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(&decoder->base.context, 
                                                   &properties))) {
            ATX_PropertyValue property_value;
            if (decoder->replay_gain_info.flags &
                FLO_REPLAY_GAIN_HAS_TRACK_VALUE) {
                property_value.integer = decoder->replay_gain_info.track_gain;
                ATX_Properties_SetProperty(&properties,
                                           BLT_REPLAY_GAIN_PROPERTY_TRACK_GAIN,
                                           ATX_PROPERTY_TYPE_INTEGER,
                                           &property_value);
            } else {
                ATX_Properties_UnsetProperty(&properties,
                                             BLT_REPLAY_GAIN_PROPERTY_TRACK_GAIN);
            }
            if (decoder->replay_gain_info.flags &
                FLO_REPLAY_GAIN_HAS_ALBUM_VALUE) {
                property_value.integer = decoder->replay_gain_info.album_gain;
                ATX_Properties_SetProperty(&properties,
                                           BLT_REPLAY_GAIN_PROPERTY_ALBUM_GAIN,
                                           ATX_PROPERTY_TYPE_INTEGER,
                                           &property_value);
            } else {
                ATX_Properties_UnsetProperty(&properties,
                                             BLT_REPLAY_GAIN_PROPERTY_ALBUM_GAIN);
            }
        }
    }
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_DecodeFrame
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoder_DecodeFrame(MpegAudioDecoder* decoder,
                             BLT_MediaPacket** packet)
{
    FLO_SampleBuffer   sample_buffer;
    FLO_FrameInfo      frame_info;        
    FLO_DecoderStatus* fluo_status;
    FLO_Cardinal       samples_skipped = 0;
    FLO_Result         result;
    
    /* try to find a frame */
    result = FLO_Decoder_FindFrame(decoder->fluo, &frame_info);
    if (FLO_FAILED(result)) return result;

    /* setup default return value */
    *packet = NULL;

    /* update the stream info */
    result = MpegAudioDecoder_UpdateInfo(decoder, &frame_info);
    if (BLT_FAILED(result)) return result;

    /* get the decoder status */
    result = FLO_Decoder_GetStatus(decoder->fluo, &fluo_status);
    if (BLT_FAILED(result)) return result;

    /* update the bitrate */
    result = MpegAudioDecoder_UpdateDurationAndBitrate(decoder, fluo_status, &frame_info);
    if (BLT_FAILED(result)) return result;

    /* update the replay gain info */
    MpegAudioDecoder_UpdateReplayGainInfo(decoder, fluo_status);

    /* get a packet from the core */
    sample_buffer.size = frame_info.sample_count*frame_info.channel_count*2;
    result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                        sample_buffer.size,
                                        (const BLT_MediaType*)&decoder->output.media_type,
                                        packet);
    if (BLT_FAILED(result)) return result;

    /* get the address of the packet payload */
    sample_buffer.samples = BLT_MediaPacket_GetPayloadBuffer(*packet);

    /* decode the frame */
    result = FLO_Decoder_DecodeFrame(decoder->fluo, 
                                     &sample_buffer,
                                     &samples_skipped);
    if (FLO_FAILED(result)) {
        /* check fluo result */
        if (result == FLO_ERROR_NO_MORE_SAMPLES) {
            /* we have already decoded everything */
            decoder->output.eos = BLT_TRUE;
        }

        /* release the packet */
        BLT_MediaPacket_Release(*packet);
        *packet = NULL;
        return result;
    }

    /* adjust for skipped samples */
    if (samples_skipped) {
        BLT_Offset offset = samples_skipped*sample_buffer.format.channel_count*2;
        BLT_MediaPacket_SetPayloadWindow(*packet, offset, sample_buffer.size);
    } else {
        /* set the packet payload size */
        BLT_MediaPacket_SetPayloadSize(*packet, sample_buffer.size);
    }

    /* update the sample count */
    decoder->output.sample_count = fluo_status->sample_count;

    /* set start of stream packet flags */
    {
        ATX_Int64 zero;
        ATX_Int64_Zero(zero);
        if (ATX_Int64_Equal(zero, decoder->output.sample_count)) {
            BLT_MediaPacket_SetFlags(*packet, 
                                     BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
        }
    }

    /* update the timestamp */
    if (frame_info.channel_count             != 0 && 
        frame_info.sample_rate               != 0 &&
        sample_buffer.format.bits_per_sample != 0) {
        /* compute time stamp */
        BLT_TimeStamp_FromSamples(&decoder->output.time_stamp, 
                                  decoder->output.sample_count,
                                  frame_info.sample_rate);
        BLT_MediaPacket_SetTimeStamp(*packet, decoder->output.time_stamp);
    } 

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MpegAudioDecoderOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
MpegAudioDecoderOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                                     BLT_MediaPacket**           packet)
{
    MpegAudioDecoder* decoder = (MpegAudioDecoder*)instance;
    ATX_ListItem*     item;
    BLT_Any           payload_buffer;
    BLT_Size          payload_size;
    BLT_Boolean       try_again;
    BLT_Result        result;

    /* default return */
    *packet = NULL;

    /* check for EOS */
    if (decoder->output.eos) {
        return BLT_ERROR_EOS;
    }

    do {
        /* try to decode a frame */
        result = MpegAudioDecoder_DecodeFrame(decoder, packet);
        if (BLT_SUCCEEDED(result)) return BLT_SUCCESS;
        if (FLO_ERROR_IS_FATAL(result)) {
            return result;
        }             

        /* not enough data, try to feed some more */
        try_again = BLT_FALSE;
        if ((item = ATX_List_GetFirstItem(decoder->input.packets))) {
            BLT_MediaPacket* input = ATX_ListItem_GetData(item);
            BLT_Size         feed_size;
            FLO_Flags        flags = 0;

            /* get the packet payload */
            payload_buffer = BLT_MediaPacket_GetPayloadBuffer(input);
            payload_size   = BLT_MediaPacket_GetPayloadSize(input);

            /* compute the flags */
            if (ATX_List_GetItemCount(decoder->input.packets) == 0) {
                /* no more packets */
                if (decoder->input.eos) {
                    /* end of stream */
                    flags |= FLO_DECODER_BUFFER_IS_END_OF_STREAM;
                }
            }
            if (BLT_MediaPacket_GetFlags(input) & 
                BLT_MEDIA_PACKET_FLAG_END_OF_STREAM) {
                flags |= FLO_DECODER_BUFFER_IS_END_OF_STREAM;
            }

            /* feed the decoder */
            feed_size = payload_size;
            result = FLO_Decoder_Feed(decoder->fluo, 
                                      payload_buffer, 
                                      &feed_size, flags);
            if (BLT_FAILED(result)) return result;

            if (feed_size == payload_size) {
                /* we're done with the packet */
                ATX_List_RemoveItem(decoder->input.packets, item);
                BLT_MediaPacket_Release(input);
            } else {
                /* we can't feed anymore, there's some leftovers */
                BLT_Offset offset = BLT_MediaPacket_GetPayloadOffset(input);
                BLT_MediaPacket_SetPayloadOffset(input, offset+feed_size);
            }
            if (feed_size != 0 || flags) {
                try_again = BLT_TRUE;
            }
        } 
    } while (try_again);

    /* if we've reached the end of stream, generate an empty packet with */
    /* a flag to indicate that situation                                 */
    if (decoder->input.eos) {
        result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                            0,
                                            (const BLT_MediaType*)&decoder->output.media_type,
                                            packet);
        if (BLT_FAILED(result)) return result;
        BLT_MediaPacket_SetFlags(*packet, BLT_MEDIA_PACKET_FLAG_END_OF_STREAM);
        BLT_MediaPacket_SetTimeStamp(*packet, decoder->output.time_stamp);
        decoder->output.eos = BLT_TRUE;
        return BLT_SUCCESS;
    }
    
    return BLT_ERROR_PORT_HAS_NO_DATA;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(MpegAudioDecoderOutputPort, 
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
MpegAudioDecoderOutputPort_BLT_MediaPortInterface = {
    MpegAudioDecoderOutputPort_GetInterface,
    MpegAudioDecoderOutputPort_GetName,
    MpegAudioDecoderOutputPort_GetProtocol,
    MpegAudioDecoderOutputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
MpegAudioDecoderOutputPort_BLT_PacketProducerInterface = {
    MpegAudioDecoderOutputPort_GetInterface,
    MpegAudioDecoderOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderOutputPort)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoderOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoderOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderOutputPort)

/*----------------------------------------------------------------------
|    MpegAudioDecoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoder_SetupPorts(MpegAudioDecoder* decoder)
{
    ATX_Result result;

    /* init the input port */
    decoder->input.eos = BLT_FALSE;

    /* create a list of input packets */
    result = ATX_List_Create(&decoder->input.packets);
    if (ATX_FAILED(result)) return result;
    
    /* setup the output port */
    decoder->output.eos = BLT_FALSE;
    BLT_PcmMediaType_Init(&decoder->output.media_type);
    ATX_Int64_Set_Int32(decoder->output.sample_count, 0);
    BLT_TimeStamp_Set(decoder->output.time_stamp, 0, 0);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoder_Create(BLT_Module*              module,
                        BLT_Core*                core, 
                        BLT_ModuleParametersType parameters_type,
                        BLT_CString              parameters, 
                        ATX_Object*              object)
{
    MpegAudioDecoder* decoder;
    BLT_Result        result;

    BLT_Debug("MpegAudioDecoder::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    decoder = ATX_AllocateZeroMemory(sizeof(MpegAudioDecoder));
    if (decoder == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&decoder->base, module, core);

    /* create the fluo decoder */
    result = FLO_Decoder_Create(&decoder->fluo);
    if (FLO_FAILED(result)) {
        ATX_FreeMemory(decoder);
        ATX_CLEAR_OBJECT(object);
        return result;
    }

    /* setup the input and output ports */
    result = MpegAudioDecoder_SetupPorts(decoder);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(decoder);
        ATX_CLEAR_OBJECT(object);
        return result;
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)decoder;
    ATX_INTERFACE(object) = (ATX_Interface*)&MpegAudioDecoder_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
MpegAudioDecoder_Destroy(MpegAudioDecoder* decoder)
{ 
    ATX_ListItem* item;

    BLT_Debug("MpegAudioDecoder::Destroy\n");

    /* release any packet we may hold */
    item = ATX_List_GetFirstItem(decoder->input.packets);
    while (item) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) {
            BLT_MediaPacket_Release(packet);
        }
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Destroy(decoder->input.packets);
    
    /* destroy the fluo decoder */
    FLO_Decoder_Destroy(decoder->fluo);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&decoder->base);

    /* free the object memory */
    ATX_FreeMemory(decoder);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|       MpegAudioDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
MpegAudioDecoder_GetPortByName(BLT_MediaNodeInstance* instance,
                               BLT_CString            name,
                               BLT_MediaPort*         port)
{
    MpegAudioDecoder* decoder = (MpegAudioDecoder*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &MpegAudioDecoderInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &MpegAudioDecoderOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    MpegAudioDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
MpegAudioDecoder_Seek(BLT_MediaNodeInstance* instance,
                      BLT_SeekMode*          mode,
                      BLT_SeekPoint*         point)
{
    MpegAudioDecoder* decoder = (MpegAudioDecoder*)instance;

    /* flush pending input packets */
    MpegAudioDecoderInputPort_Flush(decoder);

    /* clear the eos flag */
    decoder->input.eos  = BLT_FALSE;
    decoder->output.eos = BLT_FALSE;

    /* flush and reset the decoder */
    FLO_Decoder_Flush(decoder->fluo);
    FLO_Decoder_Reset(decoder->fluo);

    /* estimate the seek point in time_stamp mode */
    if (ATX_OBJECT_IS_NULL(&decoder->base.context)) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(&decoder->base.context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_SAMPLE)) {
        return BLT_FAILURE;
    }

    /* update the decoder's sample position */
    decoder->output.sample_count = point->sample;
    decoder->output.time_stamp = point->time_stamp;
    FLO_Decoder_SetSample(decoder->fluo, point->sample);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
MpegAudioDecoder_BLT_MediaNodeInterface = {
    MpegAudioDecoder_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    MpegAudioDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    MpegAudioDecoder_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(MpegAudioDecoder, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoder)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoder, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoder, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoder)

/*----------------------------------------------------------------------
|       MpegAudioDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
MpegAudioDecoderModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    MpegAudioDecoderModule* module = (MpegAudioDecoderModule*)instance;
    BLT_Registry            registry;
    BLT_Result              result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the .mp2, .mp1, .mp3 .mpa and .mpg file extensions */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".mp3",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".mp2",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".mp1",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".mpa",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".mpg",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/mpeg" type */
    result = BLT_Registry_RegisterName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mpeg",
        &module->mpeg_audio_type_id);
    if (BLT_FAILED(result)) return result;
    
    BLT_Debug("MpegAudioDecoderModule::Attach (audio/mpeg type = %d)\n", 
              module->mpeg_audio_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       MpegAudioDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
MpegAudioDecoderModule_Probe(BLT_ModuleInstance*      instance, 
                             BLT_Core*                core,
                             BLT_ModuleParametersType parameters_type,
                             BLT_AnyConst             parameters,
                             BLT_Cardinal*            match)
{
    MpegAudioDecoderModule* module = (MpegAudioDecoderModule*)instance;
    BLT_COMPILER_UNUSED(core);
    
    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* the input and output protocols should be PACKET */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be audio/mpeg */
            if (constructor->spec.input.media_type->id != 
                module->mpeg_audio_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == 
                  BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.output.media_type->id ==
                  BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "MpegAudioDecoder")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 10;
            }

            BLT_Debug("MpegAudioDecoderModule::Probe - Ok [%d]\n", *match);
            return BLT_SUCCESS;
        }    
        break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|       template instantiations
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(MpegAudioDecoder)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(MpegAudioDecoder, 
                                        "MPEG Audio Decoder", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface MpegAudioDecoderModule_BLT_ModuleInterface = {
    MpegAudioDecoderModule_GetInterface,
    BLT_BaseModule_GetInfo,
    MpegAudioDecoderModule_Attach,
    MpegAudioDecoderModule_CreateInstance,
    MpegAudioDecoderModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define MpegAudioDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(MpegAudioDecoderModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderModule) 
ATX_INTERFACE_MAP_ADD(MpegAudioDecoderModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(MpegAudioDecoderModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(MpegAudioDecoderModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_MpegAudioDecoderModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return MpegAudioDecoderModule_Create(object);
}
