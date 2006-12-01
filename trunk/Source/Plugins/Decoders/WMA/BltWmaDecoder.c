/****************************************************************
|
|   WMA Decoder Module
|
|   (c) 2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltWmaDecoder.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltByteStreamUser.h"
#include "BltStream.h"

#include "wmaudio.h"
#include "wmaprodecS_api.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.wma")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 wma_type_id;
} WmaDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_InputStreamUser);

    /* members */
    ATX_InputStream* stream;
    ATX_Position     position;
    BLT_Size         size;
    BLT_MediaTypeId  media_type_id;
} WmaDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_PcmMediaType media_type;
} WmaDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    WmaDecoderInput  input;
    WmaDecoderOutput output;
    void*            wma_handle;
    ATX_DataBuffer*  wma_buffer;
} WmaDecoder;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_WMA_DECODER_PACKET_SAMPLE_COUNT 1024

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(WmaDecoderModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(WmaDecoder, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(WmaDecoder, ATX_Referenceable)

/*----------------------------------------------------------------------
|   WMADebugMessage
+---------------------------------------------------------------------*/
void 
WMADebugMessage(const char* format, ...)
{
    ATX_COMPILER_UNUSED(format);
}

/*----------------------------------------------------------------------
|   WMAFileCBGetData
+---------------------------------------------------------------------*/
tWMA_U32 
WMAFileCBGetData(void*           state,
                 tWMA_U64        position,
                 tWMA_U32        bytes_to_read,
                 unsigned char** data)
{
    tWMAFileHdrState* header = (tWMAFileHdrState*)state;
    WmaDecoder*       self;
    ATX_Result        result;

    ATX_ASSERT(handle != NULL);

    /* default values */
    *data = NULL;

    /* retrieve our self-pointer in the handle */
    self = (WmaDecoder*)header->callbackContext;

    /* seek if needed */
    if (position != self->input.position) {
        result = ATX_InputStream_Seek(self->input.stream, (ATX_Position)position);
        if (ATX_FAILED(result)) return 0;
        self->input.position = (ATX_Position)position;
    }

    /* reserve some space in our internal buffer */
    ATX_DataBuffer_Reserve(self->wma_buffer, bytes_to_read);
    *data = ATX_DataBuffer_UseData(self->wma_buffer);

    /* read from the input stream */
    result = ATX_InputStream_ReadFully(self->input.stream, *data, bytes_to_read);
    if (ATX_FAILED(result)) return 0;

    /* update the position */
    self->input.position += bytes_to_read;

    /* return the number of bytes read */
    return bytes_to_read;
}

/*----------------------------------------------------------------------
|   WmaDecoder_OpenStream
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_OpenStream(WmaDecoder* self)
{
    tWMAFileStatus status;
    tWMAFileHeader header;
    PCMFormat      pcm_format;

    /* check that we have a stream */
    if (self->input.stream == NULL) {
        return BLT_FAILURE;
    }

    /* reset the position */
    self->input.position = 0;

    /* get input stream size */
    ATX_InputStream_GetSize(self->input.stream, &self->input.size);

    /* create a decoder */
    status = WMAFileDecodeCreate(&self->wma_handle);
    if (status != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoder_OpenStream - WMAFileDecodeCreate failed (%d)", status);
        return BLT_FAILURE;
    }

    /* setup the callback info */
    ((tWMAFileHdrState*)self->wma_handle)->callbackContext = self;

    /* initialize the decoder */
    status = WMAFileDecodeInitEx(self->wma_handle, 
                                 0, /* nDecoderFlags  */
                                 0, /* nDRCSetting    */
                                 0, /* bDropPacket    */
                                 0, /* nDstChannelMask */
                                 0, /* nInterpResampRate */
                                 &pcm_format, /* pPCMFormat */
                                 1            /* wTargetAudioStream*/);
    if (status != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoder_OpenStream - WMAFileDecodeInitEx failed (%d)", status);
        return BLT_FAILURE;
    }

    /* get the file info */
    ATX_SetMemory(&header, 0, sizeof(header));
    status = WMAFileDecodeInfo(self->wma_handle, &header);
    if (status != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoder_OpenStream - WMAFileDecodeInfo failed (%d)", status);
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* check that we support this */
    if (header.has_DRM) {
        ATX_LOG_WARNING("WmaDecoder_OpenStream - stream is DRM protected");
        return BLT_ERROR_UNSUPPORTED_FORMAT;
    }

    /* update the output media type */
    self->output.media_type.sample_rate     = header.sample_rate;
    self->output.media_type.channel_count   = header.num_channels;
    self->output.media_type.channel_mask    = header.channel_mask;
    self->output.media_type.bits_per_sample = (BLT_UInt8)header.bits_per_sample;
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* update the stream info */
    if (ATX_BASE(self, BLT_BaseMediaNode).context) {
        BLT_StreamInfo stream_info;

        /* start with no info */
        stream_info.mask = 0;

        /* sample rate */
        stream_info.sample_rate = header.sample_rate;
        stream_info.mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;

        /* channel count */
        stream_info.channel_count = header.num_channels;
        stream_info.mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;

        /* data type */
        if (WMAFileIsLosslessWMA(self->wma_handle)) {
            if (header.version > cWMA_V2) {
                stream_info.data_type = "WMA Pro";
            } else {
                stream_info.data_type = "WMA";
            }
        } else {
            stream_info.data_type = "WMA Lossless";
        }
        stream_info.mask |= BLT_STREAM_INFO_MASK_DATA_TYPE;

        /* nominal bitrate */
        stream_info.nominal_bitrate = header.bitrate;
        stream_info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;

        /* duration */
        stream_info.duration = header.duration;
        stream_info.mask |= BLT_STREAM_INFO_MASK_DURATION;

        BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   WmaDecoderInput_SetStream
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoderInput_SetStream(BLT_InputStreamUser* _self, 
                          ATX_InputStream*     stream,
                          const BLT_MediaType* media_type)
{
    WmaDecoder* self = ATX_SELF_M(input, WmaDecoder, BLT_InputStreamUser);
    BLT_Result     result;

    /* check the stream's media type */
    if (media_type == NULL || 
        media_type->id != self->input.media_type_id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(self->input.stream);
    if (self->wma_handle) {
        WMAFileDecodeClose(&self->wma_handle);
    }

    /* reset counters and flags */
    self->input.size = 0;
    self->input.position = 0;

    /* open the stream */
    self->input.stream = stream;
    result = WmaDecoder_OpenStream(self);
    if (BLT_FAILED(result)) {
        self->input.stream = NULL;
        ATX_LOG_WARNING("WmaDecoderInput::SetStream - failed");
        return result;
    }

    /* keep a reference to the stream */
    ATX_REFERENCE_OBJECT(stream);

    /* get stream size */
    ATX_InputStream_GetSize(stream, &self->input.size);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderInput, BLT_InputStreamUser)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(WmaDecoderInput, BLT_InputStreamUser)
    WmaDecoderInput_SetStream
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WmaDecoderInput,
                                         "input",
                                         STREAM_PULL,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(WmaDecoderInput, BLT_MediaPort)
    WmaDecoderInput_GetName,
    WmaDecoderInput_GetProtocol,
    WmaDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    WmaDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                           BLT_MediaPacket**   packet)
{
    WmaDecoder*    self = ATX_SELF_M(output, WmaDecoder, BLT_PacketProducer);
    tWMAFileStatus rc;
    tWMA_U32       decoded_samples;
    tWMA_U32       pcm_samples;
    tWMA_U64       timestamp;
    BLT_UInt32     buffer_size;
    BLT_Result     result;
    
    /* default value */
    *packet = NULL;

    /* decode one frame */
    rc = WMAFileDecodeData(self->wma_handle, &decoded_samples);
    if (rc == cWMA_NoMoreFrames) {
        ATX_LOG_FINE("WmaDecoderOutput_GetPacket - no more frames, end of stream");
        return BLT_ERROR_EOS;
    } else if (rc != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoderOutput_GetPacket - WMAFileDecodeData failed (%d)", rc);
        return BLT_FAILURE;
    } else if (decoded_samples == 0) {
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }

    /* get a packet from the core */
    buffer_size = decoded_samples*
                  (self->output.media_type.bits_per_sample/8)*
                   self->output.media_type.channel_count;
    result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                        buffer_size,
                                        (BLT_MediaType*)&self->output.media_type,
                                        packet);
    if (BLT_FAILED(result)) return result;

    /* convert to PCM */
    pcm_samples = WMAFileGetPCM(self->wma_handle, 
                                BLT_MediaPacket_GetPayloadBuffer(*packet), NULL, 
                                buffer_size, decoded_samples, &timestamp);

    buffer_size = pcm_samples*
                  (self->output.media_type.bits_per_sample/8)*
                   self->output.media_type.channel_count;
    BLT_MediaPacket_SetPayloadSize(*packet, buffer_size);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WmaDecoderOutput,
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(WmaDecoderOutput, BLT_MediaPort)
    WmaDecoderOutput_GetName,
    WmaDecoderOutput_GetProtocol,
    WmaDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(WmaDecoderOutput, BLT_PacketProducer)
    WmaDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    WmaDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoder_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  BLT_MediaNode**          object)
{
    WmaDecoder* decoder;

    ATX_LOG_FINE("WmaDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    decoder = ATX_AllocateZeroMemory(sizeof(WmaDecoder));
    if (decoder == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(decoder, BLT_BaseMediaNode), module, core);

    /* construct the object */
    decoder->input.media_type_id = ATX_SELF_EX_O(module, WmaDecoderModule, BLT_BaseModule, BLT_Module)->wma_type_id;
    BLT_PcmMediaType_Init(&decoder->output.media_type);
    decoder->wma_handle = NULL;
    ATX_DataBuffer_Create(4096, &decoder->wma_buffer);

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(decoder, WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(decoder, WmaDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&decoder->input,  WmaDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&decoder->input,  WmaDecoderInput,  BLT_InputStreamUser);
    ATX_SET_INTERFACE(&decoder->output, WmaDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&decoder->output, WmaDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(decoder, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WmaDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoder_Destroy(WmaDecoder* self)
{
    ATX_LOG_FINE("WmaDecoder::Destroy");

    /* free the WMA decoder */
    if (self->wma_handle) {
        WMAFileDecodeClose(&self->wma_handle);
    }
    
    /* free the WMA buffer */
    ATX_DataBuffer_Destroy(self->wma_buffer);

    /* release the input stream */
    ATX_RELEASE_OBJECT(self->input.stream);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory((void*)self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|    WmaDecoder_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_Deactivate(BLT_MediaNode* _self)
{
    WmaDecoder* self = ATX_SELF_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_FINER("WmaDecoder::Deactivate");

    /* call the base class method */
    BLT_BaseMediaNode_Deactivate(_self);

    /* release the input stream */
    ATX_RELEASE_OBJECT(self->input.stream);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   WmaDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    WmaDecoder* self = ATX_SELF_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    WmaDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    WmaDecoder* self = ATX_SELF_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    double      time;

    /* estimate the seek point in time_stamp mode */
    if (ATX_BASE(self, BLT_BaseMediaNode).context == NULL) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(ATX_BASE(self, BLT_BaseMediaNode).context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) ||
        !(point->mask & BLT_SEEK_POINT_MASK_SAMPLE)) {
        return BLT_FAILURE;
    }

    /* seek to the target time */
    time = 
        (double)point->time_stamp.seconds +
        (double)point->time_stamp.nanoseconds/1000000000.0f;
    ATX_LOG_FINER_1("WmaDecoder::Seek - sample = %f", time);
    /*ov_result = ov_time_seek(&self->input.vorbis_file, time);*/
    /*if (ov_result != 0) return BLT_FAILURE;*/

    /* set the mode so that the nodes down the chain know the seek has */
    /* already been done on the stream                                 */
    *mode = BLT_SEEK_MODE_IGNORE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    WmaDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    WmaDecoder_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    WmaDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(WmaDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   WmaDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    WmaDecoderModule* self = ATX_SELF_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*     registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".wma" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".wma",
                                            "audio/x-ms-wma");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/x-ms-wma" type */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-ms-wma",
        &self->wma_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("WmaDecoderModule::Attach (audio/x-ms-wma type = %d)", self->wma_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   WmaDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoderModule_Probe(BLT_Module*              _self, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    WmaDecoderModule* self = ATX_SELF_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be STREAM_PULL and the */
            /* output protocol should be PACKET                 */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be audio/x-ms-wma */
            if (constructor->spec.input.media_type->id != 
                self->wma_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == 
                  BLT_MEDIA_TYPE_ID_UNKNOWN) &&
                !(constructor->spec.output.media_type->id == 
                  BLT_MEDIA_TYPE_ID_AUDIO_PCM)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "WmaDecoder")) {
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

            ATX_LOG_FINE_1("WmaDecoderModule::Probe - Ok [%d]", *match); 
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(WmaDecoderModule, WmaDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    WmaDecoderModule_Attach,
    WmaDecoderModule_CreateInstance,
    WmaDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define WmaDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(WmaDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   node constructor
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(WmaDecoderModule, "WMA Decoder", 0)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_WmaDecoderModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return WmaDecoderModule_Create(object);
}
