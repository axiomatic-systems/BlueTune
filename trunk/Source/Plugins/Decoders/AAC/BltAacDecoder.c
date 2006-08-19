/*****************************************************************
|
|   AAC Decoder Module
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "Fluo.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltAacDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"

#include "faad.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.aac")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_BITRATE_AVERAGING_SHORT_SCALE     7
#define BLT_BITRATE_AVERAGING_SHORT_WINDOW    32
#define BLT_BITRATE_AVERAGING_LONG_SCALE      7
#define BLT_BITRATE_AVERAGING_LONG_WINDOW     4096
#define BLT_BITRATE_AVERAGING_PRECISION       4000

#define BLT_AAC_DECODER_INPUT_BUFFER_SIZE     8192

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderModule)
static const BLT_ModuleInterface AacDecoderModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoder)
static const BLT_MediaNodeInterface AacDecoder_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderInputPort)

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderOutputPort)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule  base;
    BLT_MediaTypeId aac_type_id;
} AacDecoderModule;

typedef struct {
    BLT_Boolean eos;
    ATX_List*   packets;
    struct {
        unsigned char* data;
        unsigned long  size;
    } buffer;
} AacDecoderInputPort;

typedef struct {
    BLT_Boolean      eos;
    BLT_PcmMediaType media_type;
    ATX_Int64        sample_count;
    BLT_TimeStamp    time_stamp;
} AacDecoderOutputPort;

typedef struct {
    BLT_BaseMediaNode    base;
    BLT_Module           module;
    AacDecoderInputPort  input;
    AacDecoderOutputPort output;
    faacDecHandle        handle;
    BLT_Boolean          initialized;
    struct {
        BLT_Cardinal nominal_bitrate;
        BLT_Cardinal average_bitrate;
        ATX_Int64    average_bitrate_accumulator;
        BLT_Cardinal instant_bitrate;
        ATX_Int64    instant_bitrate_accumulator;
    }                    stream_info;
} AacDecoder;

/*----------------------------------------------------------------------
|    AacDecoderInputPort_Flush
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoderInputPort_Flush(AacDecoder* decoder)
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
|    AacDecoderInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AacDecoderInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                              BLT_MediaPacket*            packet)
{
    AacDecoder* decoder = (AacDecoder*)instance;
    ATX_Result  result;

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
|    AacDecoderInputPort_BytesUsed
+---------------------------------------------------------------------*/
static void
AacDecoderInputPort_BytesUsed(AacDecoder* decoder, unsigned int size)
{
    if (size) {
        ATX_MoveMemory(decoder->input.buffer.data,
                        decoder->input.buffer.data + size,
                        decoder->input.buffer.size - size);
        decoder->input.buffer.size -= size;
    }
}

/*----------------------------------------------------------------------
|    AacDecoderInputPort_RefillBuffer
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoderInputPort_RefillBuffer(AacDecoder* decoder)
{
    /* try to fill the input buffer */
    while (decoder->input.buffer.size < BLT_AAC_DECODER_INPUT_BUFFER_SIZE) {
        ATX_ListItem* item;
        if (item = ATX_List_GetFirstItem(decoder->input.packets)) {
            BLT_MediaPacket* packet = (BLT_MediaPacket*)ATX_ListItem_GetData(item);
            BLT_Size space = BLT_AAC_DECODER_INPUT_BUFFER_SIZE - 
                             decoder->input.buffer.size;
            BLT_Size payload_size = BLT_MediaPacket_GetPayloadSize(packet);
            void* payload_buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
            if (payload_size <= space) {
                /* the entire packet fits in the buffer */
                ATX_CopyMemory(decoder->input.buffer.data +
                               decoder->input.buffer.size,
                               payload_buffer, 
                               payload_size);
                decoder->input.buffer.size += payload_size;
                ATX_List_RemoveItem(decoder->input.packets, item);
                BLT_MediaPacket_Release(packet);
            } else {
                /* only a portion of the packet will fit */
                ATX_CopyMemory(decoder->input.buffer.data +
                               decoder->input.buffer.size,
                               payload_buffer,
                               space);
                decoder->input.buffer.size += space;
                BLT_MediaPacket_SetPayloadOffset(
                    packet, 
                    BLT_MediaPacket_GetPayloadOffset(packet) +
                    space);
            }
        } else {
            /* not enough data */
            return BLT_ERROR_PORT_HAS_NO_DATA;
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AacDecoderInputPort,
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
AacDecoderInputPort_BLT_MediaPortInterface = {
    AacDecoderInputPort_GetInterface,
    AacDecoderInputPort_GetName,
    AacDecoderInputPort_GetProtocol,
    AacDecoderInputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
AacDecoderInputPort_BLT_PacketConsumerInterface = {
    AacDecoderInputPort_GetInterface,
    AacDecoderInputPort_PutPacket
};

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderInputPort)
ATX_INTERFACE_MAP_ADD(AacDecoderInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(AacDecoderInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderInputPort)

/*----------------------------------------------------------------------
|    AacDecoder_UpdateInfo
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoder_UpdateInfo(AacDecoder*       decoder,     
                      faacDecFrameInfo* frame_info)
{
    /* check if the media format has changed */
    if (frame_info->samplerate != decoder->output.media_type.sample_rate   ||
        frame_info->channels   != decoder->output.media_type.channel_count) {

        /* set the output type extensions */
        decoder->output.media_type.channel_count   = (BLT_UInt16)frame_info->channels;
        decoder->output.media_type.sample_rate     = frame_info->samplerate;
        decoder->output.media_type.bits_per_sample = 16;
        decoder->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
                
        {
            BLT_StreamInfo info;
            info.data_type       = "AAC";
            info.sample_rate     = frame_info->samplerate;
            info.channel_count   = (BLT_UInt16)frame_info->channels;

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
|    AacDecoder_UpdateBitrateAverage
+---------------------------------------------------------------------*/
static BLT_Cardinal
AacDecoder_UpdateBitrateAverage(BLT_Cardinal previous_bitrate,
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
|    AacDecoder_DecodeFrame
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoder_DecodeFrame(AacDecoder*       decoder,
                       BLT_MediaPacket** packet)
{
    faacDecFrameInfo frame_info;
    void*            sample_buffer;
    BLT_Size         sample_buffer_size;
    BLT_Result       result;

    /* decoder a frame */
    sample_buffer = faacDecDecode(decoder->handle,
                                  &frame_info,
                                  decoder->input.buffer.data,
                                  decoder->input.buffer.size);
    AacDecoderInputPort_BytesUsed(decoder, frame_info.bytesconsumed);

    /* update the stream info */
    result = AacDecoder_UpdateInfo(decoder, &frame_info);
    if (BLT_FAILED(result)) return result;

    /* update the bitrate */
    //result = AacDecoder_UpdateDurationAndBitrate(decoder, &frame_info);
    //if (BLT_FAILED(result)) return result;
    
    /* some frames may be emtpy (initial delay) */
    if (frame_info.samples == 0) {
        *packet = NULL;
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }

    /* get a packet from the core */
    sample_buffer_size = frame_info.samples*2;
    result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                        sample_buffer_size,
                                        (BLT_MediaType*)&decoder->output.media_type,
                                        packet);
    if (BLT_FAILED(result)) return result;

    /* copy the samples */
    ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(*packet),
                   sample_buffer, sample_buffer_size);

    /* set packet flags */
    {
        ATX_Int64 zero;
        ATX_Int64_Zero(zero);
        if (ATX_Int64_Equal(zero, decoder->output.sample_count)) {
            BLT_MediaPacket_SetFlags(*packet, 
                                     BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
        }
    }

    /* update the sample count and timestamp */
    if (frame_info.channels            != 0 && 
        frame_info.samplerate          != 0) {
        /* compute time stamp */
        BLT_TimeStamp_FromSamples(&decoder->output.time_stamp, 
                                  decoder->output.sample_count,
                                  frame_info.samplerate);
        BLT_MediaPacket_SetTimeStamp(*packet, decoder->output.time_stamp);

        /* update sample count */
        ATX_Int64_Add_Int32(decoder->output.sample_count, 
                            frame_info.samples/frame_info.channels);
    } 

    /* set the packet payload size */
    BLT_MediaPacket_SetPayloadSize(*packet, sample_buffer_size);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AacDecoderOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AacDecoderOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                               BLT_MediaPacket**           packet)
{
    AacDecoder* decoder = (AacDecoder*)instance;
    BLT_Result  result;

    /* default return */
    *packet = NULL;

    /* check for EOS */
    if (decoder->output.eos) {
        return BLT_ERROR_EOS;
    }

    /* refill the input buffer */
    result = AacDecoderInputPort_RefillBuffer(decoder);
    if (BLT_FAILED(result)) return result;

    /* init if we have not yet done so */
    if (!decoder->initialized) {
        unsigned long samplerate;
        unsigned char channels;
        unsigned long used;
        used = faacDecInit(decoder->handle, 
                           decoder->input.buffer.data,
                           decoder->input.buffer.size,
                           &samplerate,
                           &channels);
        AacDecoderInputPort_BytesUsed(decoder, used);
        decoder->initialized = BLT_TRUE;
        result = AacDecoderInputPort_RefillBuffer(decoder);
        if (BLT_FAILED(result)) return result;
    }
    
    /* try to decode a frame */
    result = AacDecoder_DecodeFrame(decoder, packet);
    if (BLT_SUCCEEDED(result)) return result;

    /* if we've reached the end of stream, generate an empty packet with */
    /* a flag to indicate that situation                                 */
    if (decoder->input.eos) {
        result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                            0,
                                            (BLT_MediaType*)&decoder->output.media_type,
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AacDecoderOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
AacDecoderOutputPort_BLT_MediaPortInterface = {
    AacDecoderOutputPort_GetInterface,
    AacDecoderOutputPort_GetName,
    AacDecoderOutputPort_GetProtocol,
    AacDecoderOutputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
AacDecoderOutputPort_BLT_PacketProducerInterface = {
    AacDecoderOutputPort_GetInterface,
    AacDecoderOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderOutputPort)
ATX_INTERFACE_MAP_ADD(AacDecoderOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(AacDecoderOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderOutputPort)

/*----------------------------------------------------------------------
|    AacDecoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoder_SetupPorts(AacDecoder* decoder)
{
    ATX_Result result;

    /* init the input port */
    decoder->input.eos = BLT_FALSE;
    decoder->input.buffer.data = 
        ATX_AllocateMemory(BLT_AAC_DECODER_INPUT_BUFFER_SIZE);
    decoder->input.buffer.size = 0;

    /* create a list of input packets */
    result = ATX_List_Create(&decoder->input.packets);
    if (ATX_FAILED(result)) return result;
    
    /* setup the output port */
    decoder->output.eos = BLT_FALSE;
    BLT_PcmMediaType_Init(&decoder->output.media_type);
    decoder->output.media_type.sample_rate     = 0;
    decoder->output.media_type.channel_count   = 0;
    decoder->output.media_type.bits_per_sample = 0;
    decoder->output.media_type.sample_format   = 0;
    ATX_Int64_Set_Int32(decoder->output.sample_count, 0);
    BLT_TimeStamp_Set(decoder->output.time_stamp, 0, 0);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AacDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoder_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  ATX_Object*              object)
{
    AacDecoder* decoder;
    BLT_Result  result;

    ATX_LOG_FINE("AacDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    decoder = ATX_AllocateZeroMemory(sizeof(AacDecoder));
    if (decoder == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&decoder->base, module, core);

    /* create the aac decoder */
    decoder->handle = faacDecOpen();
    decoder->initialized = BLT_FALSE;

    /* initialize the decoder */
    {
        faacDecConfigurationPtr config;
        config = faacDecGetCurrentConfiguration(decoder->handle);
        config->defObjectType = LC;
        config->outputFormat  = FAAD_FMT_16BIT;
        config->downMatrix    = 0;
        faacDecSetConfiguration(decoder->handle, config);
    }

    /* setup the input and output ports */
    result = AacDecoder_SetupPorts(decoder);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(decoder);
        ATX_CLEAR_OBJECT(object);
        return result;
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)decoder;
    ATX_INTERFACE(object) = (ATX_Interface*)&AacDecoder_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AacDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AacDecoder_Destroy(AacDecoder* decoder)
{ 
    ATX_ListItem* item;

    ATX_LOG_FINE("AacDecoder::Destroy");

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
    
    /* destroy the AAC decoder */
    faacDecClose(decoder->handle);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&decoder->base);

    /* free the object memory */
    ATX_FreeMemory(decoder);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   AacDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AacDecoder_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    AacDecoder* decoder = (AacDecoder*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &AacDecoderInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &AacDecoderOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    AacDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AacDecoder_Seek(BLT_MediaNodeInstance* instance,
                BLT_SeekMode*          mode,
                BLT_SeekPoint*         point)
{
    AacDecoder* decoder = (AacDecoder*)instance;

    /* flush pending input packets */
    AacDecoderInputPort_Flush(decoder);

    /* clear the eos flag */
    decoder->input.eos  = BLT_FALSE;
    decoder->output.eos = BLT_FALSE;

    /* flush and reset the decoder */

    /* estimate the seek point in time_stamp mode */
    if (ATX_OBJECT_IS_NULL(&decoder->base.context)) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(&decoder->base.context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_SAMPLE)) {
        return BLT_FAILURE;
    }

    /* update the output sample count */
    decoder->output.sample_count = point->sample;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
AacDecoder_BLT_MediaNodeInterface = {
    AacDecoder_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    AacDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    AacDecoder_Seek
};

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AacDecoder, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoder)
ATX_INTERFACE_MAP_ADD(AacDecoder, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(AacDecoder, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoder)

/*----------------------------------------------------------------------
|   AacDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
AacDecoderModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    AacDecoderModule* module = (AacDecoderModule*)instance;
    BLT_Registry      registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the .aac file extensions */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".aac",
                                            "audio/x-aac");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/x-aac" type */
    result = BLT_Registry_RegisterName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-aac",
        &module->aac_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("AacDecoderModule::Attach (audio/x-aac type = %d)", 
                   module->aac_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AacDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AacDecoderModule_Probe(BLT_ModuleInstance*      instance, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    AacDecoderModule* module = (AacDecoderModule*)instance;
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

            /* the input type should be audio/x-aac */
            if (constructor->spec.input.media_type->id != 
                module->aac_type_id) {
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
                if (ATX_StringsEqual(constructor->name, "AacDecoder")) {
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

            ATX_LOG_FINE_1("AacDecoderModule::Probe - Ok [%d]", *match);
            return BLT_SUCCESS;
        }    
        break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|   template instantiations
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AacDecoder)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(AacDecoder, "AAC Decoder", 0)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface AacDecoderModule_BLT_ModuleInterface = {
    AacDecoderModule_GetInterface,
    BLT_BaseModule_GetInfo,
    AacDecoderModule_Attach,
    AacDecoderModule_CreateInstance,
    AacDecoderModule_Probe
};

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AacDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AacDecoderModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderModule) 
ATX_INTERFACE_MAP_ADD(AacDecoderModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(AacDecoderModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AacDecoderModule)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_AacDecoderModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return AacDecoderModule_Create(object);
}
