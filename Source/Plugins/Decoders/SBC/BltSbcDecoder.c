/*****************************************************************
|
|   BlueTune - SBC Decoder Module
|
|   (c) 2002-2016 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltReplayGain.h"
#include "BltCommonMediaTypes.h"

#include "BltSbcDecoder.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.sbc")

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
} SbcDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
} SbcDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    ATX_List*        packets;
    BLT_PcmMediaType media_type;
} SbcDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    SbcDecoderInput   input;
    SbcDecoderOutput  output;
} SbcDecoder;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(SbcDecoderModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(SbcDecoder, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(SbcDecoder, ATX_Referenceable)

/*----------------------------------------------------------------------
|   SbcDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SbcDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                          BLT_MediaPacket*    packet)
{
    SbcDecoder*                  self = ATX_SELF_M(input, SbcDecoder, BLT_PacketConsumer);
    const MLO_SampleFormat*      sample_format; 
    const BLT_MediaType*         media_type;
    ATX_Result                   result;

#if 0
    /* check to see if this is the end of a stream */
    if (BLT_MediaPacket_GetFlags(packet) & 
        BLT_MEDIA_PACKET_FLAG_END_OF_STREAM) {
        self->input.eos = BLT_TRUE;
    }

    /* look for a change in media type parameters */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (media_type == NULL || media_type->id != self->mp4es_type_id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    mp4_media_type = (const BLT_Mp4AudioMediaType*)media_type;
    if (self->input.media_type) {
        if (!BLT_MediaType_Equals(media_type, (const BLT_MediaType*)self->input.media_type)) {
            ATX_LOG_FINE("change of SBC media type parameters detected");
            MLO_Decoder_Destroy(self->melo);
            self->melo = NULL;
            BLT_MediaType_Free((BLT_MediaType*)self->input.media_type);
            self->input.media_type = NULL;
        }
    }
    if (self->input.media_type == NULL) {
        BLT_MediaType* clone;
        BLT_MediaType_Clone(media_type, &clone);
        self->input.media_type = (BLT_Mp4AudioMediaType*)clone;
    }
    
    /* check to see if we need to create a decoder for this */
    if (self->melo == NULL) {
        MLO_DecoderConfig            decoder_config;
        if (MLO_FAILED(MLO_DecoderConfig_Parse(mp4_media_type->decoder_info, 
                                               mp4_media_type->decoder_info_length, 
                                               &decoder_config))) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        if (decoder_config.object_type != MLO_OBJECT_TYPE_SBC_LC) return BLT_ERROR_UNSUPPORTED_CODEC;
        result = MLO_Decoder_Create(&decoder_config, &self->melo);
        if (MLO_FAILED(result)) return BLT_ERROR_INVALID_MEDIA_FORMAT;

        /* update the stream info */
        if (ATX_BASE(self, BLT_BaseMediaNode).context) {
            BLT_StreamInfo stream_info;
            stream_info.data_type     = "MPEG-4 SBC";
            stream_info.sample_rate   = MLO_DecoderConfig_GetSampleRate(&decoder_config);
            stream_info.channel_count = MLO_DecoderConfig_GetChannelCount(&decoder_config);
            stream_info.mask = BLT_STREAM_INFO_MASK_DATA_TYPE    |
                               BLT_STREAM_INFO_MASK_SAMPLE_RATE  |
                               BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
            BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);
        }
    }

    /* decode the packet as a frame */
    result = MLO_Decoder_DecodeFrame(self->melo, 
                                     BLT_MediaPacket_GetPayloadBuffer(packet),
                                     BLT_MediaPacket_GetPayloadSize(packet),
                                     self->sample_buffer);
    if (MLO_FAILED(result)) {
        ATX_LOG_FINE_1("MLO_Decoder_DecodeFrame failed (%d)", result);
        return BLT_SUCCESS;
    }

    /* update the output media type */
    sample_format = MLO_SampleBuffer_GetFormat(self->sample_buffer);
    self->output.media_type.channel_count   = sample_format->channel_count;
    self->output.media_type.sample_rate     = sample_format->sample_rate;
    self->output.media_type.bits_per_sample = 16;
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    self->output.media_type.channel_mask    = 0;

    /* create a PCM packet for the output */
    {
        BLT_Size         out_packet_size = MLO_SampleBuffer_GetSize(self->sample_buffer);
        BLT_MediaPacket* out_packet;
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                            out_packet_size,
                                            (BLT_MediaType*)&self->output.media_type,
                                            &out_packet);
        if (BLT_FAILED(result)) return result;
        BLT_MediaPacket_SetPayloadSize(out_packet, out_packet_size);
        ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(out_packet), 
                       MLO_SampleBuffer_GetSamples(self->sample_buffer), 
                       out_packet_size);

        /* copy the timestamp */
        BLT_MediaPacket_SetTimeStamp(out_packet, BLT_MediaPacket_GetTimeStamp(packet));

        /* copy the flags */
        BLT_MediaPacket_SetFlags(out_packet, BLT_MediaPacket_GetFlags(packet));
        
        /* add to the output packet list */
        ATX_List_AddData(self->output.packets, out_packet);
    }
#endif
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(SbcDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SbcDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcDecoderInput, BLT_PacketConsumer)
    SbcDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SbcDecoderInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(SbcDecoderInput, BLT_MediaPort)
    SbcDecoderInput_GetName,
    SbcDecoderInput_GetProtocol,
    SbcDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   SbcDecoderOutput_Flush
+---------------------------------------------------------------------*/
static BLT_Result
SbcDecoderOutput_Flush(SbcDecoder* self)
{
    ATX_ListItem* item;
    while ((item = ATX_List_GetFirstItem(self->output.packets))) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) BLT_MediaPacket_Release(packet);
        ATX_List_RemoveItem(self->output.packets, item);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SbcDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                           BLT_MediaPacket**   packet)
{
    SbcDecoder*   self = ATX_SELF_M(output, SbcDecoder, BLT_PacketProducer);
    ATX_ListItem* packet_item;

    /* default return */
    *packet = NULL;

    /* check if we have a packet available */
    packet_item = ATX_List_GetFirstItem(self->output.packets);
    if (packet_item) {
        *packet = (BLT_MediaPacket*)ATX_ListItem_GetData(packet_item);
        ATX_List_RemoveItem(self->output.packets, packet_item);
        return BLT_SUCCESS;
    }
    
    return BLT_ERROR_PORT_HAS_NO_DATA;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(SbcDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SbcDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SbcDecoderOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(SbcDecoderOutput, BLT_MediaPort)
    SbcDecoderOutput_GetName,
    SbcDecoderOutput_GetProtocol,
    SbcDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcDecoderOutput, BLT_PacketProducer)
    SbcDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   SbcDecoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
SbcDecoder_SetupPorts(SbcDecoder* self, BLT_MediaTypeId mp4es_type_id)
{
    ATX_Result result;

    /* init the input port */
    self->mp4es_type_id = mp4es_type_id;
    self->input.eos = BLT_FALSE;

    /* create a list of input packets */
    result = ATX_List_Create(&self->output.packets);
    if (ATX_FAILED(result)) return result;
    
    /* setup the output port */
    BLT_PcmMediaType_Init(&self->output.media_type);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SbcDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
SbcDecoder_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  BLT_MediaNode**          object)
{
    SbcDecoder*       self;
    SbcDecoderModule* sbc_decoder_module = (SbcDecoderModule*)module;
    BLT_Result        result;

    ATX_LOG_FINE("SbcDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(SbcDecoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup the input and output ports */
    result = SbcDecoder_SetupPorts(self, sbc_decoder_module->mp4es_type_id);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(self);
        *object = NULL;
        return result;
    }

    /* create a sample buffer */
    MLO_SampleBuffer_Create(0, &(self->sample_buffer));

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, SbcDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, SbcDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  SbcDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  SbcDecoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, SbcDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, SbcDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SbcDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
SbcDecoder_Destroy(SbcDecoder* self)
{ 
    ATX_LOG_FINE("SbcDecoder::Destroy");

    /* release input resources */
    if (self->input.media_type) {
        BLT_MediaType_Free((BLT_MediaType*)self->input.media_type);
    }
    
    /* release any packet we may hold */
    SbcDecoderOutput_Flush(self);
    ATX_List_Destroy(self->output.packets);
    
    /* destroy the Melo decoder */
    if (self->melo) MLO_Decoder_Destroy(self->melo);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* destroy the sample buffer */
    //MLO_SampleBuffer_Destroy(self->sample_buffer);

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   SbcDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
SbcDecoder_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    SbcDecoder* self = ATX_SELF_EX(SbcDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(&self->input, BLT_MediaPort);
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        *port = &ATX_BASE(&self->output, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    SbcDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
SbcDecoder_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    SbcDecoder* self = ATX_SELF_EX(SbcDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);
    
    /* clear the eos flag */
    self->input.eos  = BLT_FALSE;

    /* remove any packets in the output list */
    SbcDecoderOutput_Flush(self);

    /* reset the decoder */
    if (self->melo) MLO_Decoder_Reset(self->melo);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SbcDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    SbcDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    SbcDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SbcDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   SbcDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
SbcDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    SbcDecoderModule* self = ATX_SELF_EX(SbcDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*           registry;
    BLT_Result              result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_MP4_AUDIO_ES_MIME_TYPE,
        &self->mp4es_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("SbcDecoderModule::Attach (" BLT_MP4_AUDIO_ES_MIME_TYPE " = %d)", self->mp4es_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
SbcDecoderModule_Probe(BLT_Module*              _self, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    SbcDecoderModule* self = ATX_SELF_EX(SbcDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);
    
    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* the input and output protocols should be PACKET */
            if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be BLT_MP4_ES_MIME_TYPE */
            if (constructor->spec.input.media_type->id != 
                self->mp4es_type_id) {
                return BLT_FAILURE;
            } else {
                /* check the object type id */
                BLT_Mp4AudioMediaType* media_type = (BLT_Mp4AudioMediaType*)constructor->spec.input.media_type;
				if (media_type->base.stream_type != BLT_MP4_STREAM_TYPE_AUDIO) return BLT_FAILURE;
                if (media_type->base.format_or_object_type_id != BLT_SBC_DECODER_OBJECT_TYPE_MPEG2_SBC_LC &&
                    media_type->base.format_or_object_type_id != BLT_SBC_DECODER_OBJECT_TYPE_MPEG4_AUDIO) {
                    return BLT_FAILURE;
                }
                if (media_type->base.format_or_object_type_id == BLT_SBC_DECODER_OBJECT_TYPE_MPEG4_AUDIO) {
                    /* check that this is SBC LC */
                    MLO_DecoderConfig decoder_config;
                    if (MLO_FAILED(MLO_DecoderConfig_Parse(media_type->decoder_info, media_type->decoder_info_length, &decoder_config))) {
                        return BLT_FAILURE;
                    }
                    if (decoder_config.object_type != MLO_OBJECT_TYPE_SBC_LC) return BLT_FAILURE;
                    if (decoder_config.channel_configuration == 0) {
                        ATX_LOG_INFO("SBC channel configuration is 0");
                        return BLT_FAILURE;
                    }
                    if (decoder_config.channel_configuration > 2) {
                        ATX_LOG_INFO_1("SBC channel configuration not supported (%d)", decoder_config.channel_configuration);
                        return BLT_FAILURE;
                    }
                }
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "SbcDecoder")) {
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

            ATX_LOG_FINE_1("SbcDecoderModule::Probe - Ok [%d]", *match);
            return BLT_SUCCESS;
        }    
        break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(SbcDecoderModule, SbcDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SbcDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    SbcDecoderModule_Attach,
    SbcDecoderModule_CreateInstance,
    SbcDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define SbcDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SbcDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(SbcDecoderModule,
                                         "SBC Audio Decoder",
                                         "com.axiosys.decoder.sbc",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
