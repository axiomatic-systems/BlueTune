/*****************************************************************
|
|   BlueTune - Intel IPP Wrapper Decoder Module
|
|   (c) 2008-2009 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltIppDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltMediaPacket.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"
#include "BltPixels.h"

// IPP includes
#include "ippcore.h"
#define VM_MALLOC_GLOBAL
#include "umc_malloc.h"
#include "umc_video_decoder.h"
#include "umc_video_data.h"
#include "umc_video_processing.h"
#include "umc_data_pointers_copy.h"
#include "umc_h264_dec.h"
#include "umc_h264_timing.h"

using namespace UMC;

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.ipp")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 mp4_video_es_type_id;
    BLT_UInt32 iso_base_video_es_type_id;
} IppDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_Boolean       eos;
} IppDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_Boolean           eos;
    BLT_RawVideoMediaType media_type;
    BLT_MediaPacket*      picture;
} IppDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    IppDecoderModule* module;
    IppDecoderInput   input;
    IppDecoderOutput  output;
    H264VideoDecoder* decoder;
} IppDecoder;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
    
/*----------------------------------------------------------------------
|   IppDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
IppDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                          BLT_MediaPacket*    packet)
{
    IppDecoder* self   = ATX_SELF_M(input, IppDecoder, BLT_PacketConsumer);
    BLT_Result  result = BLT_SUCCESS;

    
    

    if (self->decoder == NULL) {
        ATX_LOG_FINE("instantiating new H.264 decoder");
        self->decoder = new H264VideoDecoder();
        VideoDecoderParams    dec_params;
        VideoProcessing       video_proc;
        VideoProcessingParams post_proc_params;
        post_proc_params.m_DeinterlacingMethod = NO_DEINTERLACING;
        post_proc_params.InterpolationMethod = 0;
        video_proc.SetParams(&post_proc_params);

        dec_params.pPostProcessing  = &video_proc;
        dec_params.info.stream_type = H264_VIDEO;
        dec_params.numThreads = 1;
        dec_params.lFlags = 0;
    }
#if 0
        //dec_params->m_pData = in.get();

            if (UMC_OK != (h264Decoder->Init(params.get())))
            {
                vm_string_printf(__VM_STRING("Video Decoder creation failed\n"));
                return -1;
            }

            H264VideoDecoderParams params;
            if (UMC_OK != h264Decoder->GetInfo(&params))
            {
                vm_string_printf(__VM_STRING("Video Decoder creation failed\n"));
                return -1;
            }
    
    /* allocate a codec if we don't already have one */
    if (self->codec_context == NULL) {
        /* check the media type and find the right codec */
        const BLT_Mp4VideoMediaType* media_type;
        const unsigned char*         decoder_config = NULL;
        unsigned int                 decoder_config_size = 0;
        enum CodecID                 codec_id = CODEC_ID_NONE;
        AVCodec*                     codec = NULL;
        
        /* check the packet type */
        BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
        if ((media_type->base.base.id != self->module->iso_base_video_es_type_id &&
             media_type->base.base.id != self->module->mp4_video_es_type_id) ||
            media_type->base.stream_type != BLT_MP4_STREAM_TYPE_VIDEO) {
            ATX_LOG_FINE("IppDecoderInput::PutPacket - invalid media type");
            return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
        if (media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_AVC1) {
            ATX_LOG_FINE("IppDecoderInput::PutPacket - content type is AVC");
            codec_id            = CODEC_ID_H264;
            decoder_config      = media_type->decoder_info;
            decoder_config_size = media_type->decoder_info_length;
        }
        
        if (codec_id == CODEC_ID_NONE) {
            /* no compatible codec found */
            ATX_LOG_WARNING("IppDecoderInput::PutPacket - no compatible codec found");
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
        
        /* find the codec handle */
        codec = avcodec_find_decoder(codec_id);
        if (codec == NULL) {
            ATX_LOG_WARNING("IppDecoderInput::PutPacket - avcodec_find_decoder failed");
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
        
        /* allocate the codec context */
        self->codec_context = avcodec_alloc_context();
        if (self->codec_context == NULL) {
            ATX_LOG_WARNING("IppDecoderInput::PutPacket - avcodec_alloc_context returned NULL");
            return BLT_ERROR_OUT_OF_MEMORY;
        }
        
        /* setup the codec options */
        self->codec_context->debug_mv          = 0;
        self->codec_context->debug             = 0;
        self->codec_context->workaround_bugs   = 1;
        self->codec_context->lowres            = 0;
        self->codec_context->idct_algo         = FF_IDCT_AUTO;
        self->codec_context->skip_frame        = AVDISCARD_DEFAULT;
        self->codec_context->skip_idct         = AVDISCARD_DEFAULT;
        self->codec_context->skip_loop_filter  = AVDISCARD_DEFAULT;
        self->codec_context->error_resilience  = FF_ER_CAREFUL;
        self->codec_context->error_concealment = 3;
        self->codec_context->thread_count      = 1;
        
        /* set the generic video config */
        self->codec_context->width           = media_type->width;
        self->codec_context->height          = media_type->height;
        self->codec_context->bits_per_sample = media_type->depth;
        
        /* setup the callbacks */
        self->codec_context->get_buffer     = IppDecoder_GetBufferCallback;
        self->codec_context->release_buffer = IppDecoder_ReleaseBufferCallback;
        
        /* set the H.264 decoder config */
        self->codec_context->extradata_size = decoder_config_size;
        if (decoder_config_size) {
            self->codec_context->extradata = av_malloc(decoder_config_size);
            ATX_CopyMemory(self->codec_context->extradata, decoder_config, decoder_config_size);        
        } else {
            self->codec_context->extradata = NULL;
        }
        
        /* open the codec */
        av_result = avcodec_open(self->codec_context, codec);
        if (av_result < 0) {
            ATX_LOG_WARNING_1("IppDecoderInput::PutPacket - avcodec_open returned %d", av_result);
            return BLT_ERROR_INTERNAL;
        }
        
        /* allocate a frame */
        self->frame = avcodec_alloc_frame();
    }
    
    /* check to see if this is the end of a stream */
    if (BLT_MediaPacket_GetFlags(packet) & 
        BLT_MEDIA_PACKET_FLAG_END_OF_STREAM) {
        self->input.eos = BLT_TRUE;
    }

    /* decode a picture */
    {
        int got_picture = 0;

        /* libavcodec wants the input buffers to be padded */
        unsigned int   packet_size   = BLT_MediaPacket_GetPayloadSize(packet);
        unsigned char* packet_buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
        BLT_Size buffer_size_needed  = BLT_MediaPacket_GetPayloadOffset(packet)+packet_size+BLT_FFMPEG_INPUT_PADDING_SIZE;
        if (buffer_size_needed > BLT_MediaPacket_GetAllocatedSize(packet)) {
            BLT_MediaPacket_SetAllocatedSize(packet, buffer_size_needed);
            packet_buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
        }
        ATX_SetMemory(packet_buffer+packet_size, 0, BLT_FFMPEG_INPUT_PADDING_SIZE);
        
        /* set the context opaque pointer for callbacks */
        self->codec_context->opaque = packet;
        
        /* feed the codec */
        ATX_LOG_FINEST_2("decoding frame size=%ld, pts=%f", 
                         packet_size, 
                         (float)BLT_MediaPacket_GetTimeStamp(packet).seconds +
                         (float)BLT_MediaPacket_GetTimeStamp(packet).nanoseconds/1000000000.0f);
        av_result = avcodec_decode_video(self->codec_context, 
                                         self->frame, 
                                         &got_picture, 
                                         packet_buffer, 
                                         packet_size);
        if (av_result < 0) {
            ATX_LOG_FINE_1("avcodec_decode_video returned %d", av_result);
            return BLT_SUCCESS;
        }
        if (got_picture) {
            BLT_MediaPacket* picture = NULL;
            unsigned int   plane_size[3];
            unsigned int   padding_size[3] = {0,0,0};
            unsigned int   picture_size = 0;
            unsigned char* picture_buffer;
            unsigned int   i;
            
            ATX_LOG_FINEST_2("decoded frame width=%d, height=%d",
                              self->codec_context->width, 
                              self->codec_context->height);
            self->output.media_type.width  = self->codec_context->width;
            self->output.media_type.height = self->codec_context->height;
            self->output.media_type.format = BLT_PIXEL_FORMAT_YV12;
            self->output.media_type.flags  = 0;
            for (i=0; i<3; i++) {
                if (i==0) {
                    /* Y' plane */
                    plane_size[i] = self->frame->linesize[i]*self->codec_context->height;
                } else {
                    /* Cb and Cr planes */
                    plane_size[i] = self->frame->linesize[i]*self->codec_context->height/2;
                }
                if (plane_size[i]%16) {
                    padding_size[i] = 16-(plane_size[i]%16);
                }
                self->output.media_type.planes[i].offset = picture_size;
                self->output.media_type.planes[i].bytes_per_line = self->frame->linesize[i];
                picture_size += plane_size[i]+padding_size[i];
            }
            
            result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core, 
                                                picture_size, 
                                                &self->output.media_type.base, 
                                                &picture);
            if (BLT_FAILED(result)) {
                ATX_LOG_WARNING_1("BLT_Core_CreateMediaPacket returned %d", result);
                return result;
            }
            
            /* retrieve the timestamp from the frame */
            if (self->frame->opaque) {
                BLT_TimeStamp* pts = (BLT_TimeStamp*)self->frame->opaque;
                if (pts) BLT_MediaPacket_SetTimeStamp(picture, *pts);
            }

            /* setup the picture buffer */
            BLT_MediaPacket_SetPayloadSize(picture, picture_size);
            picture_buffer = BLT_MediaPacket_GetPayloadBuffer(picture);
            for (i=0; i<3; i++) {
                ATX_CopyMemory(picture_buffer+self->output.media_type.planes[i].offset, 
                               self->frame->data[i], 
                               plane_size[i]);
            }
            if (self->output.picture) {
                BLT_MediaPacket_Release(self->output.picture);
            }
            self->output.picture = picture;
        } 
    }
#endif
    
    return result;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(IppDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(IppDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(IppDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(IppDecoderInput, BLT_PacketConsumer)
    IppDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(IppDecoderInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(IppDecoderInput, BLT_MediaPort)
    IppDecoderInput_GetName,
    IppDecoderInput_GetProtocol,
    IppDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   IppDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
IppDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                              BLT_MediaPacket**   packet)
{
    IppDecoder* self = ATX_SELF_M(output, IppDecoder, BLT_PacketProducer);
    
    if (self->output.picture == NULL) return BLT_ERROR_PORT_HAS_NO_DATA;
    *packet = self->output.picture;
    self->output.picture = NULL;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(IppDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(IppDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(IppDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(IppDecoderOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(IppDecoderOutput, BLT_MediaPort)
    IppDecoderOutput_GetName,
    IppDecoderOutput_GetProtocol,
    IppDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(IppDecoderOutput, BLT_PacketProducer)
    IppDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   IppDecoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
IppDecoder_SetupPorts(IppDecoder* self)
{
    /*ATX_Result result;*/

    /* init the input port */
    self->input.eos = BLT_FALSE;
    
    /* setup the output port */
    self->output.eos = BLT_FALSE;
    BLT_RawVideoMediaType_Init(&self->output.media_type);
    self->output.media_type.format = BLT_PIXEL_FORMAT_YV12;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    IppDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
IppDecoder_Destroy(IppDecoder* self)
{ 

    ATX_LOG_FINE("enter");

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));
    
    /* free any buffered packet */
    if (self->output.picture) BLT_MediaPacket_Release(self->output.picture);

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   IppDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
IppDecoder_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    IppDecoder* self = ATX_SELF_EX(IppDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    IppDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
IppDecoder_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    IppDecoder* self = ATX_SELF_EX(IppDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* clear the eos flags */
    self->input.eos   = BLT_FALSE;
    self->output.eos  = BLT_FALSE;

    /* flush anything that may be pending */
    if (self->output.picture) {
        BLT_MediaPacket_Release(self->output.picture);
        self->output.picture = NULL;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(IppDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(IppDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(IppDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(IppDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    IppDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    IppDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(IppDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
 |    IppDecoder_Create
 +---------------------------------------------------------------------*/
static BLT_Result
IppDecoder_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  const void*              parameters, 
                  BLT_MediaNode**          object)
{
    IppDecoder* self;
    BLT_Result  result;
    
    ATX_LOG_FINE("enter");
    
    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }
    
    /* allocate memory for the object */
    self = (IppDecoder*)ATX_AllocateZeroMemory(sizeof(IppDecoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }
    
    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);
    
    /* construct the object */
    self->module = (IppDecoderModule*)module;
    
    /* setup the input and output ports */
    result = IppDecoder_SetupPorts(self);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(self);
        *object = NULL;
        return result;
    }
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, IppDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, IppDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  IppDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  IppDecoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, IppDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, IppDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);
    
    return BLT_SUCCESS;
}    
    
/*----------------------------------------------------------------------
|   IppDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
IppDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    IppDecoderModule* self = ATX_SELF_EX(IppDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*     registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the type id for BLT_MP4_VIDEO_ES_MIME_TYPE */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_MP4_VIDEO_ES_MIME_TYPE,
        &self->mp4_video_es_type_id);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1(BLT_MP4_VIDEO_ES_MIME_TYPE " type = %d)", self->mp4_video_es_type_id);

    /* register the type id for BLT_ISO_BASE_VIDEO_ES_MIME_TYPE */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_ISO_BASE_VIDEO_ES_MIME_TYPE,
        &self->iso_base_video_es_type_id);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1(BLT_ISO_BASE_VIDEO_ES_MIME_TYPE " type = %d)", self->iso_base_video_es_type_id);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   IppDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
IppDecoderModule_Probe(BLT_Module*              _self, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    IppDecoderModule* self = ATX_SELF_EX(IppDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);
    
    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* the input and output protocols should be PACKET or ANY */
            if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be mp4 or iso base video */
            if (constructor->spec.input.media_type->id != self->mp4_video_es_type_id &&
                constructor->spec.input.media_type->id != self->iso_base_video_es_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or video/raw */
            if (constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_VIDEO_RAW &&
                constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "com.bluetune.decoders.ipp")) {
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

            ATX_LOG_FINE_1("probe Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(IppDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(IppDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(IppDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(IppDecoderModule, IppDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(IppDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    IppDecoderModule_Attach,
    IppDecoderModule_CreateInstance,
    IppDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define IppDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(IppDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   node constructor
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(IppDecoderModule, "Intel IPP Decoder", 0)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_IppDecoderModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return IppDecoderModule_Create(object);
}
