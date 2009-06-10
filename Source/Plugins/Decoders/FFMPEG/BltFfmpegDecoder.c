/*****************************************************************
|
|   BlueTune - FFMPEG Wrapper Decoder Module
|
|   (c) 2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltFfmpegDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltMediaPacket.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"
#include "BltPixels.h"

#include "avcodec.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.ffmpeg")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_FFMPEG_INPUT_PADDING_SIZE  256

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 mp4_video_es_type_id;
    BLT_UInt32 iso_base_video_es_type_id;
} FfmpegDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_Boolean eos;
} FfmpegDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_Boolean           eos;
    BLT_RawVideoMediaType media_type;
    BLT_MediaPacket*      picture;
} FfmpegDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    FfmpegDecoderModule* module;
    FfmpegDecoderInput   input;
    FfmpegDecoderOutput  output;
    AVCodecContext*      codec_context;
    AVFrame*             frame;
} FfmpegDecoder;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_FFMPEG_FORMAT_TAG_AVC1 0x61766331    /* 'avc1' */

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(FfmpegDecoderModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(FfmpegDecoder, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(FfmpegDecoder, ATX_Referenceable)

/*----------------------------------------------------------------------
|   FfmpegDecoder_GetBufferCallback
+---------------------------------------------------------------------*/
static int 
FfmpegDecoder_GetBufferCallback(struct AVCodecContext* context, 
                                AVFrame*               pic) 
{
    int ret = avcodec_default_get_buffer(context, pic);
    if (context->opaque) {
        BLT_TimeStamp *pts = av_malloc(sizeof(BLT_TimeStamp));
        *pts = BLT_MediaPacket_GetTimeStamp((BLT_MediaPacket*)context->opaque);
        pic->opaque = pts;
    }
    return ret;
}

/*----------------------------------------------------------------------
|   FfmpegDecoder_ReleaseBufferCallback
+---------------------------------------------------------------------*/
static void 
FfmpegDecoder_ReleaseBufferCallback(struct AVCodecContext* context, 
                                    AVFrame*               pic) 
{
    if (pic && pic->opaque) av_freep(&pic->opaque);
    avcodec_default_release_buffer(context, pic);
}

/*----------------------------------------------------------------------
|   FfmpegDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                             BLT_MediaPacket*    packet)
{
    FfmpegDecoder* self   = ATX_SELF_M(input, FfmpegDecoder, BLT_PacketConsumer);
    BLT_Result     result = BLT_SUCCESS;
    int            av_result;
    
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
            ATX_LOG_FINE("FfmpegDecoderInput::PutPacket - invalid media type");
            return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
        if (media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_AVC1) {
            ATX_LOG_FINE("FfmpegDecoderInput::PutPacket - content type is AVC");
            codec_id            = CODEC_ID_H264;
            decoder_config      = media_type->decoder_info;
            decoder_config_size = media_type->decoder_info_length;
        }
        
        if (codec_id == CODEC_ID_NONE) {
            /* no compatible codec found */
            ATX_LOG_WARNING("FfmpegDecoderInput::PutPacket - no compatible codec found");
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
        
        /* find the codec handle */
        codec = avcodec_find_decoder(codec_id);
        if (codec == NULL) {
            ATX_LOG_WARNING("FfmpegDecoderInput::PutPacket - avcodec_find_decoder failed");
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
        
        /* allocate the codec context */
        self->codec_context = avcodec_alloc_context();
        if (self->codec_context == NULL) {
            ATX_LOG_WARNING("FfmpegDecoderInput::PutPacket - avcodec_alloc_context returned NULL");
            return BLT_ERROR_OUT_OF_MEMORY;
        }
        
        /* setup the codec options */
        avcodec_get_context_defaults(self->codec_context);
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
        self->codec_context->get_buffer     = FfmpegDecoder_GetBufferCallback;
        self->codec_context->release_buffer = FfmpegDecoder_ReleaseBufferCallback;
        
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
            ATX_LOG_WARNING_1("FfmpegDecoderInput::PutPacket - avcodec_open returned %d", av_result);
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
    
    return result;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FfmpegDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(FfmpegDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(FfmpegDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(FfmpegDecoderInput, BLT_PacketConsumer)
    FfmpegDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FfmpegDecoderInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(FfmpegDecoderInput, BLT_MediaPort)
    FfmpegDecoderInput_GetName,
    FfmpegDecoderInput_GetProtocol,
    FfmpegDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   FfmpegDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                              BLT_MediaPacket**   packet)
{
    FfmpegDecoder* self = ATX_SELF_M(output, FfmpegDecoder, BLT_PacketProducer);
    
    if (self->output.picture == NULL) return BLT_ERROR_PORT_HAS_NO_DATA;
    *packet = self->output.picture;
    self->output.picture = NULL;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FfmpegDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(FfmpegDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(FfmpegDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FfmpegDecoderOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(FfmpegDecoderOutput, BLT_MediaPort)
    FfmpegDecoderOutput_GetName,
    FfmpegDecoderOutput_GetProtocol,
    FfmpegDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(FfmpegDecoderOutput, BLT_PacketProducer)
    FfmpegDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   FfmpegDecoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
FfmpegDecoder_SetupPorts(FfmpegDecoder* self)
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
|    FfmpegDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
FfmpegDecoder_Create(BLT_Module*              module,
                     BLT_Core*                core, 
                     BLT_ModuleParametersType parameters_type,
                     BLT_CString              parameters, 
                     BLT_MediaNode**          object)
{
    FfmpegDecoder* self;
    BLT_Result     result;

    ATX_LOG_FINE("FfmpegDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(FfmpegDecoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */
    self->module = (FfmpegDecoderModule*)module;
    
    /* setup the input and output ports */
    result = FfmpegDecoder_SetupPorts(self);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(self);
        *object = NULL;
        return result;
    }

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, FfmpegDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, FfmpegDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  FfmpegDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  FfmpegDecoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, FfmpegDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, FfmpegDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FfmpegDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FfmpegDecoder_Destroy(FfmpegDecoder* self)
{ 

    ATX_LOG_FINE("FfmpegDecoder::Destroy");

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* release avcodec resources */
    if (self->codec_context) {
        avcodec_close(self->codec_context);
        av_free(self->codec_context);
    }
    if (self->frame) av_free(self->frame);
    
    /* free any buffered packet */
    if (self->output.picture) BLT_MediaPacket_Release(self->output.picture);

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   FfmpegDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoder_GetPortByName(BLT_MediaNode*  _self,
                            BLT_CString     name,
                            BLT_MediaPort** port)
{
    FfmpegDecoder* self = ATX_SELF_EX(FfmpegDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    FfmpegDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoder_Seek(BLT_MediaNode* _self,
                   BLT_SeekMode*  mode,
                   BLT_SeekPoint* point)
{
    FfmpegDecoder* self = ATX_SELF_EX(FfmpegDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* clear the eos flags */
    self->input.eos   = BLT_FALSE;
    self->output.eos  = BLT_FALSE;

    /* flush anything that may be pending */
    if (self->codec_context) {
        avcodec_flush_buffers(self->codec_context);
    }
    if (self->output.picture) {
        BLT_MediaPacket_Release(self->output.picture);
        self->output.picture = NULL;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FfmpegDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(FfmpegDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(FfmpegDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(FfmpegDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    FfmpegDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    FfmpegDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(FfmpegDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   FfmpegDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    FfmpegDecoderModule* self = ATX_SELF_EX(FfmpegDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*        registry;
    BLT_Result           result;

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
    ATX_LOG_FINE_1("FfmpegDecoderModule::Attach (" BLT_MP4_VIDEO_ES_MIME_TYPE " type = %d)", self->mp4_video_es_type_id);

    /* register the type id for BLT_ISO_BASE_VIDEO_ES_MIME_TYPE */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_ISO_BASE_VIDEO_ES_MIME_TYPE,
        &self->iso_base_video_es_type_id);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1("FfmpegDecoderModule::Attach (" BLT_ISO_BASE_VIDEO_ES_MIME_TYPE " type = %d)", self->iso_base_video_es_type_id);
    
    /* initialize libavcodec */
    avcodec_init();
    avcodec_register_all();

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FfmpegDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoderModule_Probe(BLT_Module*              _self, 
                          BLT_Core*                core,
                          BLT_ModuleParametersType parameters_type,
                          BLT_AnyConst             parameters,
                          BLT_Cardinal*            match)
{
    FfmpegDecoderModule* self = ATX_SELF_EX(FfmpegDecoderModule, BLT_BaseModule, BLT_Module);
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
                if (ATX_StringsEqual(constructor->name, "com.bluetune.decoders.ffmpeg")) {
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

            ATX_LOG_FINE_1("FfmpegDecoderModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FfmpegDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(FfmpegDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(FfmpegDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FfmpegDecoderModule, FfmpegDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(FfmpegDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    FfmpegDecoderModule_Attach,
    FfmpegDecoderModule_CreateInstance,
    FfmpegDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FfmpegDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(FfmpegDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   node constructor
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(FfmpegDecoderModule, "FFMPEG Decoder", 0)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_FfmpegDecoderModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return FfmpegDecoderModule_Create(object);
}
