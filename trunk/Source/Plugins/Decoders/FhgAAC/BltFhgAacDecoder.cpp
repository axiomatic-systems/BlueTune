/*****************************************************************
|
|   BlueTune - AAC Decoder Module
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltFhgAacDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"

#include "aacdecoder_lib.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.aac.fhg")

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
const unsigned int BLT_AAC_DECODER_OBJECT_TYPE_MPEG2_AAC_LC = 0x67;
const unsigned int BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO  = 0x40;
const unsigned int BLT_FHG_AAC_DECODER_MAX_PCM_CHANNELS     = 8;
const unsigned int BLT_FHG_AAC_DECODER_MAX_PCM_BUFFER_SIZE  = 2048*BLT_FHG_AAC_DECODER_MAX_PCM_CHANNELS;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 mp4es_type_id;
} FhgAacDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_MediaPacket*       packet;
    BLT_Mp4AudioMediaType* media_type;
} FhgAacDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_PcmMediaType media_type;
    short            pcm_buffer[BLT_FHG_AAC_DECODER_MAX_PCM_BUFFER_SIZE];
    ATX_UInt64       packet_count;
} FhgAacDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    FhgAacDecoderInput  input;
    FhgAacDecoderOutput output;
    BLT_UInt32          mp4es_type_id;
    
    HANDLE_AACDECODER   aac_decoder;
} FhgAacDecoder;

/*----------------------------------------------------------------------
|   FhgAacDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FhgAacDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                             BLT_MediaPacket*    packet)
{
    FhgAacDecoder* self = ATX_SELF_M(input, FhgAacDecoder, BLT_PacketConsumer);

    // check the packet type
    const BLT_MediaType* media_type = NULL;
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (media_type->id != self->mp4es_type_id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    /* retain this packet */
    if (self->input.packet) {
        BLT_MediaPacket_Release(self->input.packet);
    }
    self->input.packet = packet;
    BLT_MediaPacket_AddReference(packet);
        
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FhgAacDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(FhgAacDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(FhgAacDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(FhgAacDecoderInput, BLT_PacketConsumer)
    FhgAacDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FhgAacDecoderInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(FhgAacDecoderInput, BLT_MediaPort)
    FhgAacDecoderInput_GetName,
    FhgAacDecoderInput_GetProtocol,
    FhgAacDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   FhgAacDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FhgAacDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                              BLT_MediaPacket**   packet)
{
    FhgAacDecoder* self = ATX_SELF_M(output, FhgAacDecoder, BLT_PacketProducer);

    // default return
    *packet = NULL;
    
    // create a decoder if we don't already have one
    if (!self->aac_decoder) {
        // if we don't have data, there's nothing to do now
        if (self->input.packet == NULL) return BLT_ERROR_PORT_HAS_NO_DATA;
        
        // create a decoder object
        ATX_LOG_FINE("creating AAC decoder");
        self->aac_decoder = aacDecoder_Open(TT_MP4_RAW, 1);
        
        // setup the decoder
        BLT_Mp4AudioMediaType* mp4_type = NULL;
        BLT_MediaPacket_GetMediaType(self->input.packet, (const BLT_MediaType**)&mp4_type);
        UCHAR* aac_config = mp4_type->decoder_info;
        UINT   aac_config_length = mp4_type->decoder_info_length;
        AAC_DECODER_ERROR status = aacDecoder_ConfigRaw(self->aac_decoder, &aac_config, &aac_config_length);
        if (status != AAC_DEC_OK) {
            ATX_LOG_WARNING_1("aacDecoder_ConfigRaw failed (0x%x)", status);
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
    }
    
    // try to decode an audio packet
    for (;;) {
        INT_PCM* output_buffer      = self->output.pcm_buffer;
        INT      output_buffer_size = sizeof(self->output.pcm_buffer);
        AAC_DECODER_ERROR status = aacDecoder_DecodeFrame(self->aac_decoder, output_buffer, output_buffer_size, 0);
        if (status != AAC_DEC_OK) {
            if (status == AAC_DEC_NOT_ENOUGH_BITS) {
                // we need more data
                if (self->input.packet) {
                    UCHAR* input = (UCHAR*)BLT_MediaPacket_GetPayloadBuffer(self->input.packet);
                    UINT   input_size = BLT_MediaPacket_GetPayloadSize(self->input.packet);
                    UINT   bytes_valid = input_size;
                    
                    // check if we still have some data left in this packet
                    if (input_size == 0) {
                        BLT_MediaPacket_Release(self->input.packet);
                        self->input.packet = NULL;
                        return BLT_ERROR_PORT_HAS_NO_DATA;
                    }
                    
                    status = aacDecoder_Fill(self->aac_decoder, &input, &input_size, &bytes_valid);
                    if (status != AAC_DEC_OK) {
                        ATX_LOG_WARNING_1("aacDecoder_Fill failed (0x%x)", status);
                        return BLT_ERROR_INTERNAL;
                    }
                    BLT_MediaPacket_SetPayloadOffset(self->input.packet, input_size-bytes_valid);
                    continue;
                } else {
                    return BLT_ERROR_PORT_HAS_NO_DATA;
                }
            }
            if (IS_DECODE_ERROR(status)) {
                ATX_LOG_WARNING_1("aacDecoder_DecodeFrame decoding error (0x%x)", status);
                continue;
            }
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        
        // check the output
        CStreamInfo* aac_info = aacDecoder_GetStreamInfo(self->aac_decoder);
        if (aac_info->sampleRate  != (INT)self->output.media_type.sample_rate ||
            aac_info->numChannels != (INT)self->output.media_type.channel_count) {
            self->output.media_type.channel_count   = aac_info->numChannels;
            self->output.media_type.sample_rate     = aac_info->sampleRate;
            if (ATX_BASE(self, BLT_BaseMediaNode).context) {
                BLT_StreamInfo stream_info;
                stream_info.data_type     = "MPEG-4 AAC";
                stream_info.sample_rate   = aac_info->sampleRate;
                stream_info.channel_count = aac_info->numChannels;
                stream_info.mask = BLT_STREAM_INFO_MASK_DATA_TYPE    |
                                   BLT_STREAM_INFO_MASK_SAMPLE_RATE  |
                                   BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
                BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);
            }
        }
        
        // create a PCM packet for the output
        BLT_Size   output_packet_size = aac_info->frameSize*aac_info->numChannels*2;
        BLT_Result result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                                       output_packet_size,
                                                       (BLT_MediaType*)&self->output.media_type,
                                                       packet);
        if (BLT_FAILED(result)) return result;
        BLT_MediaPacket_SetPayloadSize(*packet, output_packet_size);
        NPT_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(*packet),
                       output_buffer,
                       output_packet_size);
        if (self->output.packet_count++ == 0) {
            BLT_MediaPacket_SetFlags(*packet, BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
        }
        return BLT_SUCCESS;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FhgAacDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(FhgAacDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(FhgAacDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FhgAacDecoderOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(FhgAacDecoderOutput, BLT_MediaPort)
    FhgAacDecoderOutput_GetName,
    FhgAacDecoderOutput_GetProtocol,
    FhgAacDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(FhgAacDecoderOutput, BLT_PacketProducer)
    FhgAacDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    FhgAacDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FhgAacDecoder_Destroy(FhgAacDecoder* self)
{ 
    ATX_LOG_FINE("FhgAacDecoder::Destroy");

    /* release input resources */
    if (self->input.media_type) {
        BLT_MediaType_Free((BLT_MediaType*)self->input.media_type);
    }
    if (self->input.packet) {
        BLT_MediaPacket_Release(self->input.packet);
    }
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   FhgAacDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FhgAacDecoder_GetPortByName(BLT_MediaNode*  _self,
                            BLT_CString     name,
                            BLT_MediaPort** port)
{
    FhgAacDecoder* self = ATX_SELF_EX(FhgAacDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    FhgAacDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
FhgAacDecoder_Seek(BLT_MediaNode* _self,
                   BLT_SeekMode*  mode,
                   BLT_SeekPoint* point)
{
    FhgAacDecoder* self = ATX_SELF_EX(FhgAacDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);
    
    // reset the decoder by destroying it
    if (self->aac_decoder) {
        aacDecoder_Close(self->aac_decoder);
    }
    
    // free any pending input packet
    if (self->input.packet) {
        BLT_MediaPacket_Release(self->input.packet);
        self->input.packet = NULL;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FhgAacDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(FhgAacDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(FhgAacDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(FhgAacDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    FhgAacDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    FhgAacDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(FhgAacDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|    FhgAacDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
FhgAacDecoder_Create(BLT_Module*              module,
                     BLT_Core*                core,
                     BLT_ModuleParametersType parameters_type,
                     ATX_AnyConst             parameters,
                     BLT_MediaNode**          object)
{
    FhgAacDecoder*       self;
    FhgAacDecoderModule* aac_decoder_module = (FhgAacDecoderModule*)module;

    ATX_LOG_FINE("FhgAacDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = (FhgAacDecoder*)ATX_AllocateZeroMemory(sizeof(FhgAacDecoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup the input and output ports */
    self->mp4es_type_id = aac_decoder_module->mp4es_type_id;
    BLT_PcmMediaType_Init(&self->output.media_type);
    self->output.media_type.bits_per_sample = 16;
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, FhgAacDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, FhgAacDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  FhgAacDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  FhgAacDecoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, FhgAacDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, FhgAacDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FhgAacDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
FhgAacDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    FhgAacDecoderModule* self = ATX_SELF_EX(FhgAacDecoderModule, BLT_BaseModule, BLT_Module);
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
    
    ATX_LOG_FINE_1("FhgAacDecoderModule::Attach (" BLT_MP4_AUDIO_ES_MIME_TYPE " = %d)", self->mp4es_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FhgAacDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FhgAacDecoderModule_Probe(BLT_Module*              _self, 
                          BLT_Core*                core,
                          BLT_ModuleParametersType parameters_type,
                          BLT_AnyConst             parameters,
                          BLT_Cardinal*            match)
{
    FhgAacDecoderModule* self = ATX_SELF_EX(FhgAacDecoderModule, BLT_BaseModule, BLT_Module);
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
                if (media_type->base.format_or_object_type_id != BLT_AAC_DECODER_OBJECT_TYPE_MPEG2_AAC_LC &&
                    media_type->base.format_or_object_type_id != BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO) {
                    return BLT_FAILURE;
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
                if (ATX_StringsEqual(constructor->name, "com.axiosys.decoders.aac.fhg")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 9;
            }

            ATX_LOG_FINE_1("FhgAacDecoderModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FhgAacDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(FhgAacDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(FhgAacDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FhgAacDecoderModule, FhgAacDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(FhgAacDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    FhgAacDecoderModule_Attach,
    FhgAacDecoderModule_CreateInstance,
    FhgAacDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FhgAacDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(FhgAacDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(FhgAacDecoderModule,
                                         "AAC Audio Decoder (FHG)",
                                         "com.axiosys.decoder.aac.fhg",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
BLT_Result 
BLT_AacDecoderModule_GetModuleObject(BLT_Module** module)
{
    return BLT_FhgAacDecoderModule_GetModuleObject(module);
}