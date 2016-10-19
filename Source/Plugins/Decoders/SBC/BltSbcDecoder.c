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

#include "oi_codec_sbc.h"

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
    BLT_MediaTypeId sbc_type_id;
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
    OI_CODEC_SBC_DECODER_CONTEXT   decoder;
    OI_CODEC_SBC_CODEC_DATA_STEREO decoder_data;
    OI_INT16                       pcm_buffer[SBC_MAX_SAMPLES_PER_FRAME*SBC_MAX_CHANNELS];
    SbcDecoderInput                input;
    SbcDecoderOutput               output;
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
    SbcDecoder*      self          = ATX_SELF_M(input, SbcDecoder, BLT_PacketConsumer);
    const OI_BYTE*   frame_data    = BLT_MediaPacket_GetPayloadBuffer(packet);
    OI_UINT32        frame_size    = BLT_MediaPacket_GetPayloadSize(packet);
    OI_INT16*        pcm_data      = self->pcm_buffer;
    OI_UINT32        pcm_size      = sizeof(self->pcm_buffer);
    BLT_MediaPacket* output_packet = NULL;
    BLT_StreamInfo   stream_info;
    OI_STATUS        status;
    BLT_Result       result;
    
    /* decode a frame */
    status = OI_CODEC_SBC_DecodeFrame(&self->decoder,
                                      &frame_data,
                                      &frame_size,
                                      pcm_data,
                                      &pcm_size);
    if (status != OI_STATUS_SUCCESS) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* see if the media type has changed */
    stream_info.mask = 0;
    if (self->output.media_type.sample_rate != self->decoder.common.frameInfo.frequency) {
        self->output.media_type.sample_rate  = self->decoder.common.frameInfo.frequency;
        stream_info.mask       |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;
        stream_info.sample_rate = self->output.media_type.sample_rate;
    }
    if (self->output.media_type.channel_count != self->decoder.common.frameInfo.nrof_channels) {
        self->output.media_type.channel_count  = self->decoder.common.frameInfo.nrof_channels;
        stream_info.mask         |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
        stream_info.channel_count = self->output.media_type.channel_count;
    }
    if (self->output.media_type.sample_rate != self->decoder.common.frameInfo.frequency) {
        self->output.media_type.sample_rate  = self->decoder.common.frameInfo.frequency;
        stream_info.mask       |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;
        stream_info.sample_rate = self->output.media_type.sample_rate;
    }
    if (ATX_BASE(self, BLT_BaseMediaNode).context && stream_info.mask) {
        stream_info.mask     |= BLT_STREAM_INFO_MASK_DATA_TYPE | BLT_STREAM_INFO_MASK_DATA_TYPE;
        stream_info.type      = BLT_STREAM_TYPE_AUDIO;
        stream_info.data_type = "SBC";
        BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);
    }
    
    /* create a PCM packet for the output */
    result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                        pcm_size,
                                        (BLT_MediaType*)&self->output.media_type,
                                        &output_packet);
    if (BLT_FAILED(result)) return result;
    BLT_MediaPacket_SetPayloadSize(output_packet, pcm_size);
    ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(output_packet),
                   pcm_data,
                   pcm_size);
    
    /* copy the timestamp */
    BLT_MediaPacket_SetTimeStamp(output_packet, BLT_MediaPacket_GetTimeStamp(packet));
    
    /* copy the flags */
    BLT_MediaPacket_SetFlags(output_packet, BLT_MediaPacket_GetFlags(packet));
    
    /* add to the output packet list */
    ATX_List_AddData(self->output.packets, output_packet);
    
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
SbcDecoder_SetupPorts(SbcDecoder* self)
{
    ATX_Result result;

    /* create a list of input packets */
    result = ATX_List_Create(&self->output.packets);
    if (ATX_FAILED(result)) return result;
    
    /* setup the output port */
    BLT_PcmMediaType_Init(&self->output.media_type);
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    self->output.media_type.bits_per_sample = 16;
    
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
    result = SbcDecoder_SetupPorts(self);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(self);
        *object = NULL;
        return result;
    }

    /* init the decoder */
    OI_CODEC_SBC_DecoderReset(&self->decoder,
                              self->decoder_data.data,
                              sizeof(self->decoder_data.data),
                              2 /* maxChannels */,
                              2 /* pcmStride   */,
                              0 /* enhanced    */);

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

    /* release any packet we may hold */
    SbcDecoderOutput_Flush(self);
    ATX_List_Destroy(self->output.packets);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

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
    //self->input.eos  = BLT_FALSE;

    /* remove any packets in the output list */
    SbcDecoderOutput_Flush(self);

    /* reset the decoder */
    //if (self->melo) MLO_Decoder_Reset(self->melo);

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
        "audio/SBC",
        &self->sbc_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("SbcDecoderModule::Attach (audio/SBC = %d)", self->sbc_type_id);

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
            if (constructor->spec.input.media_type->id != self->sbc_type_id) {
                return BLT_FAILURE;
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
