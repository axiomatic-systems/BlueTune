/*****************************************************************
|
|   BlueTune - FFMPEG Wrapper Decoder Module
|
|   (c) 2008-2016 Gilles Boccon-Gibod
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
#include "BltOutputNode.h"

#include "avcodec.h"

//#define BLT_CONFIG_ENABLE_FFMPEG_HWACCEL

#if defined(BLT_CONFIG_ENABLE_FFMPEG_HWACCEL)
#include "libavcodec/videotoolbox.h"
#include "libavutil/pixdesc.h"
#endif

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
    BLT_UInt32 h264_es_type_id; /* h.264 annex B */
    BLT_UInt32 mp4_video_es_type_id;
    BLT_UInt32 iso_base_video_es_type_id;
} FfmpegDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_MediaType* media_type;
} FfmpegDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_RawVideoMediaType media_type;
    ATX_List*             pictures;
} FfmpegDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    FfmpegDecoderModule*  module;
    FfmpegDecoderInput    input;
    FfmpegDecoderOutput   output;
    AVCodecContext*       codec_context;
    AVCodecParserContext* parser_context;
    AVFrame*              frame;
    ATX_Boolean           enable_hwaccel;
} FfmpegDecoder;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_FFMPEG_FORMAT_TAG_AVC1 0x61766331    /* 'avc1' */
#define BLT_FFMPEG_FORMAT_TAG_AVC2 0x61766332    /* 'avc2' */
#define BLT_FFMPEG_FORMAT_TAG_AVC3 0x61766333    /* 'avc3' */
#define BLT_FFMPEG_FORMAT_TAG_AVC4 0x61766334    /* 'avc4' */
#define BLT_FFMPEG_FORMAT_TAG_HVC1 0x68766331    /* 'hvc1' */
#define BLT_FFMPEG_FORMAT_TAG_HEV1 0x68657631    /* 'hev1' */

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(FfmpegDecoderModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(FfmpegDecoder, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(FfmpegDecoder, ATX_Referenceable)

#if defined(BLT_CONFIG_ENABLE_FFMPEG_HWACCEL)
/*----------------------------------------------------------------------
|   FfmpegDecoder_GetFormatCallback
+---------------------------------------------------------------------*/
static enum AVPixelFormat
FfmpegDecoder_GetFormatCallback(struct AVCodecContext* context, const enum AVPixelFormat* formats)
{
    BLT_COMPILER_UNUSED(context);
    
    while (*formats != AV_PIX_FMT_NONE) {
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(*formats);

        if (*formats == AV_PIX_FMT_VIDEOTOOLBOX) {
            /* prefer hardware-accelerated formats if available */
            ATX_LOG_FINE("selecting hardware-accelerated codec mode");
            int result = av_videotoolbox_default_init(context);
            if (result < 0) {
                ATX_LOG_FINE_1("av_videotoolbox_default_init failed (%d)", result);
                continue;
            }
            
            return *formats;
        } else if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
            return *formats;
        }
        ++formats;
    }
    return *formats;
}
#endif

/*----------------------------------------------------------------------
|   FfmpegDecoder_ListItem_Destroy
+---------------------------------------------------------------------*/
static void
FfmpegDecoder_ListItemDestruct(ATX_ListDataDestructor* self, ATX_Any data, ATX_UInt32 type)
{
    ATX_COMPILER_UNUSED(self);
    ATX_COMPILER_UNUSED(type);
    BLT_MediaPacket_Release((BLT_MediaPacket*)data);
}

/*----------------------------------------------------------------------
|   FfmpegDecoder_ConvertFrame
+---------------------------------------------------------------------*/
static BLT_MediaPacket*
FfmpegDecoder_ConvertFrame(FfmpegDecoder* self)
{
    BLT_MediaPacket* picture = NULL;
    BLT_Result       result;
    unsigned int     picture_size = 0;
    unsigned char*   picture_buffer;
    unsigned int     i;

    self->output.media_type.width  = self->codec_context->width;
    self->output.media_type.height = self->codec_context->height;
    self->output.media_type.format = BLT_PIXEL_FORMAT_YV12;
    self->output.media_type.flags  = 0;
    
#if defined(BLT_CONFIG_ENABLE_FFMPEG_HWACCEL)
    if (self->frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        CVPixelBufferRef pixel_buffer = (CVPixelBufferRef)self->frame->data[3];
        OSType           pixel_format = CVPixelBufferGetPixelFormatType(pixel_buffer);
        unsigned int     line;
        const uint8_t*   cr_cb_in;
        uint8_t*         cb_out;
        uint8_t*         cr_out;
        CVReturn         err;

        /* kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange = '420v', Bi-Planar Component Y'CbCr 8-bit 4:2:0, video-range (luma=[16,235] chroma=[16,240]).  baseAddr points to a big-endian CVPlanarPixelBufferInfo_YCbCrBiPlanar struct */
        if (pixel_format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
            ATX_LOG_WARNING_1("unexpected pixel format %x", pixel_format);
            return NULL;
        }

        /* lock the buffer */
        err = CVPixelBufferLockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
        if (err != kCVReturnSuccess) {
            ATX_LOG_WARNING_1("CVPixelBufferLockBaseAddress failed (%d)", err);
            return NULL;
        }

        /* init */
        for (i=0; i<4; i++) {
            self->output.media_type.planes[i].offset         = 0;
            self->output.media_type.planes[i].bytes_per_line = 0;
        }
        self->output.media_type.planes[0].offset         = 0;
        self->output.media_type.planes[0].bytes_per_line = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
        self->output.media_type.planes[1].offset         = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0)*self->codec_context->height;
        self->output.media_type.planes[1].bytes_per_line = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1)/2;
        self->output.media_type.planes[2].offset         = self->output.media_type.planes[1].offset+self->output.media_type.planes[1].bytes_per_line*self->codec_context->height/2;
        self->output.media_type.planes[2].bytes_per_line = self->output.media_type.planes[1].bytes_per_line;
        picture_size = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0)*self->codec_context->height+
                       CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1)*self->codec_context->height/2;
        
        /* allocate a packet */
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                            picture_size, 
                                            &self->output.media_type.base, 
                                            &picture);
        if (BLT_FAILED(result)) {
            ATX_LOG_WARNING_1("BLT_Core_CreateMediaPacket returned %d", result);
            return NULL;
        }

        /* retrieve the timestamp from the frame */
        {
            BLT_TimeStamp pts = BLT_TimeStamp_FromNanos(av_frame_get_best_effort_timestamp(self->frame));
            BLT_MediaPacket_SetTimeStamp(picture, pts);
        }

        /* copy the pixels */
        BLT_MediaPacket_SetPayloadSize(picture, picture_size);
        picture_buffer = BLT_MediaPacket_GetPayloadBuffer(picture);
        ATX_CopyMemory(picture_buffer+self->output.media_type.planes[0].offset,
                       CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0),
                       CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0)*self->codec_context->height);
        cr_cb_in = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1);
        cr_out   = (uint8_t*)(picture_buffer+self->output.media_type.planes[1].offset);
        cb_out   = (uint8_t*)(picture_buffer+self->output.media_type.planes[2].offset);
        for (line=0; line<self->output.media_type.planes[1].bytes_per_line*self->codec_context->height/2; line++) {
            *cr_out++ = *cr_cb_in++;
            *cb_out++ = *cr_cb_in++;
        }

        /* unlock the buffer */
        CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
    } else
#endif
    if (self->frame->format == AV_PIX_FMT_YUV420P) {
        unsigned int     plane_size[3]   = {0,0,0};
        void*            plane_data[3]   = {0,0,0};
        unsigned int     padding_size[3] = {0,0,0};
        for (i=0; i<3; i++) {
            plane_data[i] = self->frame->data[i];
            
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

        /* allocate a packet */
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                            picture_size, 
                                            &self->output.media_type.base, 
                                            &picture);
        if (BLT_FAILED(result)) {
            ATX_LOG_WARNING_1("BLT_Core_CreateMediaPacket returned %d", result);
            return NULL;
        }

        /* retrieve the timestamp from the frame */
        {
            BLT_TimeStamp pts = BLT_TimeStamp_FromNanos(av_frame_get_best_effort_timestamp(self->frame));
            BLT_MediaPacket_SetTimeStamp(picture, pts);
        }

        /* copy the pixels */
        BLT_MediaPacket_SetPayloadSize(picture, picture_size);
        picture_buffer = BLT_MediaPacket_GetPayloadBuffer(picture);
        for (i=0; i<3; i++) {
            if (plane_size[i] && plane_data[i]) {
                ATX_CopyMemory(picture_buffer+self->output.media_type.planes[i].offset,
                               plane_data[i],
                               plane_size[i]);
            }
        }
    } else {
        ATX_LOG_WARNING_1("unsupported pixel format %d, skipping", self->frame->format);
        return NULL;
    }
    
    return picture;
}

/*----------------------------------------------------------------------
|   FfmpegDecoder_DecodePicture
+---------------------------------------------------------------------*/
static BLT_Result
FfmpegDecoder_DecodePicture(FfmpegDecoder* self, BLT_MediaPacket* packet)
{
    int            got_picture   = 0;
    int            av_result     = 0;
    unsigned int   packet_size   = 0;
    unsigned char* packet_buffer = NULL;
    int64_t        packet_ts     = 0;
    
    /* libavcodec wants the input buffers to be padded */
    if (packet) {
        BLT_Size buffer_size_needed;
        packet_size   = BLT_MediaPacket_GetPayloadSize(packet);
        packet_buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
        packet_ts     = BLT_TimeStamp_ToNanos(BLT_MediaPacket_GetTimeStamp(packet));
        buffer_size_needed  = BLT_MediaPacket_GetPayloadOffset(packet)+packet_size+BLT_FFMPEG_INPUT_PADDING_SIZE;
        if (buffer_size_needed > BLT_MediaPacket_GetAllocatedSize(packet)) {
            BLT_MediaPacket_SetAllocatedSize(packet, buffer_size_needed);
            packet_buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
        }
        ATX_SetMemory(packet_buffer+packet_size, 0, BLT_FFMPEG_INPUT_PADDING_SIZE);

        ATX_LOG_FINEST_2("decoding frame size=%d, pts=%f", 
                         packet_size, 
                         (float)BLT_MediaPacket_GetTimeStamp(packet).seconds +
                         (float)BLT_MediaPacket_GetTimeStamp(packet).nanoseconds/1000000000.0f);
    } else {
        ATX_LOG_FINEST("flushing delayed frames");
        return BLT_SUCCESS;
    }
    
    while (packet_size) {
        uint8_t* frame_buffer      = NULL;
        int      frame_buffer_size = 0;
        int64_t  frame_ts          = 0;
        AVPacket av_packet;
        
        /* parse the data if needed */
        if (self->parser_context) {
            int consumed = av_parser_parse2(self->parser_context,
                                            self->codec_context,
                                            &frame_buffer,
                                            &frame_buffer_size,
                                            packet_buffer,
                                            packet_size,
                                            packet_ts,
                                            packet_ts,
                                            0);
            if (consumed > 0) {
                packet_buffer += consumed;
                packet_size   -= consumed;
            }
            if (frame_buffer != NULL && frame_buffer_size != 0) {
                /* the parser produced a frame */
                frame_ts = self->parser_context->pts;
            } else {
                continue;
            }
        } else {
            frame_buffer      = packet_buffer;
            frame_buffer_size = packet_size;
            frame_ts          = packet_ts;
            packet_size       = 0; /* we're consuming the entire packet */
        }
        
        /* feed the codec */
        ATX_LOG_FINE_1("decoding video frame (%d bytes)", frame_buffer_size);
        self->codec_context->opaque = &frame_ts;
        av_init_packet(&av_packet);
        av_packet_from_data(&av_packet, frame_buffer, frame_buffer_size);
        av_packet.pts = packet_ts;
        av_result = avcodec_decode_video2(self->codec_context,
                                          self->frame,
                                          &got_picture,
                                          &av_packet);
        if (av_result < 0) {
            ATX_LOG_FINE_1("avcodec_decode_video returned %d", av_result);
            continue;
        }
        if (got_picture) {
            ATX_LOG_FINEST_2("decoded frame width=%d, height=%d",
                              self->codec_context->width, 
                              self->codec_context->height);

            BLT_MediaPacket* picture = FfmpegDecoder_ConvertFrame(self);
            if (picture == NULL) {
                ATX_LOG_WARNING("unable to convert frame, skipping picture");
                continue;
            }
            
#if defined(BLT_CONFIG_BTPLAYX_ENABLE_FFMPEG_DECODER)
            /* FIXME: test */
            if (self->enable_hwaccel) {
                BLT_MediaPacket_SetTimeStamp(picture, BLT_MediaPacket_GetTimeStamp(packet));
            }
#endif

            ATX_List_AddData(self->output.pictures, picture);
            
            av_frame_unref(self->frame);
        }
    } 
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FfmpegDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FfmpegDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                             BLT_MediaPacket*    packet)
{
    FfmpegDecoder*               self   = ATX_SELF_M(input, FfmpegDecoder, BLT_PacketConsumer);
    const BLT_Mp4VideoMediaType* mp4_media_type = NULL;
    const BLT_MediaType*         media_type;
    enum AVCodecID               codec_id = AV_CODEC_ID_NONE;
    
    /* check the media type */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (media_type->id == self->module->h264_es_type_id) {
        ATX_LOG_FINER("annex-b format");
        codec_id = AV_CODEC_ID_H264;
    } else if (media_type->id == self->module->iso_base_video_es_type_id ||
               media_type->id == self->module->mp4_video_es_type_id) {
        mp4_media_type = (const BLT_Mp4VideoMediaType*)media_type;
        if (mp4_media_type->base.stream_type != BLT_MP4_STREAM_TYPE_VIDEO) {
            ATX_LOG_FINE("invalid media type (not video)");
            return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
        if (mp4_media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_AVC1 ||
            mp4_media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_AVC2 ||
            mp4_media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_AVC3 ||
            mp4_media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_AVC4) {
            codec_id = AV_CODEC_ID_H264;
        } else if (mp4_media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_HVC1 ||
                   mp4_media_type->base.format_or_object_type_id == BLT_FFMPEG_FORMAT_TAG_HEV1) {
            codec_id = AV_CODEC_ID_HEVC;
        } else {
            ATX_LOG_FINE_1("unsupported codec (%x)", mp4_media_type->base.format_or_object_type_id);
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
    } else {
        ATX_LOG_FINE("invalid media type");
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    /* allocate a codec if we don't already have one */
    if (self->input.media_type == NULL ||
        !BLT_MediaType_Equals((const BLT_MediaType*)self->input.media_type, 
                              (const BLT_MediaType*)media_type)) {
        AVCodec* codec = NULL;
        int      av_result;
            
        /* release any previous delayed pictures, codec and frame */
        if (self->codec_context) {
            /*BLT_Result result;
            //do {
            //    result = FfmpegDecoder_DecodePicture(self, NULL);
            //} while (BLT_SUCCEEDED(result)); */
            avcodec_close(self->codec_context);
            av_free(self->codec_context);
            self->codec_context = NULL;
        }
        if (self->frame) {
            av_free(self->frame);
            self->frame = NULL;
        }
        
        /* find the codec handle */
        codec = avcodec_find_decoder(codec_id);
        if (codec == NULL) {
            ATX_LOG_WARNING("FfmpegDecoderInput::PutPacket - avcodec_find_decoder failed");
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
        
        /* allocate the codec context */
        self->codec_context = avcodec_alloc_context3(codec);
        if (self->codec_context == NULL) {
            ATX_LOG_WARNING("FfmpegDecoderInput::PutPacket - avcodec_alloc_context returned NULL");
            return BLT_ERROR_OUT_OF_MEMORY;
        }
                    
        /* setup the codec options */
        avcodec_get_context_defaults3(self->codec_context, codec);
        self->codec_context->debug_mv          = 0;
        self->codec_context->debug             = 0;
        self->codec_context->workaround_bugs   = 1;
        self->codec_context->lowres            = 0;
        self->codec_context->idct_algo         = FF_IDCT_AUTO;
        self->codec_context->skip_frame        = AVDISCARD_DEFAULT;
        self->codec_context->skip_idct         = AVDISCARD_DEFAULT;
        self->codec_context->skip_loop_filter  = AVDISCARD_DEFAULT;
        self->codec_context->error_concealment = 3;
        self->codec_context->thread_count      = 1;
        
        /* setup the callbacks */
#if defined(BLT_CONFIG_ENABLE_FFMPEG_HWACCEL)
        ATX_LOG_INFO_1("FFMPEG hwaccel enabled = %s", self->enable_hwaccel?"yes":"no");
        if (self->enable_hwaccel) {
            self->codec_context->get_format  = FfmpegDecoder_GetFormatCallback;
        }
#endif
        
        if (mp4_media_type) {
            /* set the generic video config */
            self->codec_context->width           = mp4_media_type->width;
            self->codec_context->height          = mp4_media_type->height;
            /*self->codec_context->bits_per_sample = mp4_media_type->depth;*/
            
            /* set the H.264 decoder config */
            self->codec_context->extradata_size = mp4_media_type->decoder_info_length;
            if (mp4_media_type->decoder_info_length) {
                self->codec_context->extradata = av_malloc(mp4_media_type->decoder_info_length);
                ATX_CopyMemory(self->codec_context->extradata, mp4_media_type->decoder_info, mp4_media_type->decoder_info_length);        
            } else {
                self->codec_context->extradata = NULL;
            }
        }
        
        /* open the codec */
        av_result = avcodec_open2(self->codec_context, codec, NULL);
        if (av_result < 0) {
            ATX_LOG_WARNING_1("FfmpegDecoderInput::PutPacket - avcodec_open returned %d", av_result);
            return BLT_ERROR_INTERNAL;
        }
        
        /* allocate a frame */
        self->frame = av_frame_alloc();
        
        /* open a parser if necessary */
        if (mp4_media_type == NULL) {
            self->parser_context = av_parser_init(codec_id);
            if (self->parser_context == NULL) {
                ATX_LOG_WARNING("no parser for the selected codec");
            } 
        }
        
        /* remember the media type */
        if (self->input.media_type) {
            BLT_MediaType_Free((BLT_MediaType*)self->input.media_type);
        }
        BLT_MediaType_Clone(media_type, &self->input.media_type);                
    }
    
    /* frame skipping logic */
    if (ATX_BASE(self, BLT_BaseMediaNode).context) {
        BLT_StreamStatus status;
        BLT_Stream_GetStatus(ATX_BASE(self, BLT_BaseMediaNode).context, &status);
        if (status.output_status.flags & BLT_OUTPUT_NODE_STATUS_UNDERFLOW) {
            if (self->codec_context->skip_frame != AVDISCARD_NONREF) {
                ATX_LOG_FINE("output underflow, entering skip-non-ref decoding mode");
                self->codec_context->skip_frame = AVDISCARD_NONREF;
            }
        } else {
            if (self->codec_context->skip_frame != AVDISCARD_DEFAULT) {
                ATX_LOG_FINE("returning to normal decoding mode (no-skip)");
                self->codec_context->skip_frame = AVDISCARD_DEFAULT;
            }
        }
    }
    
    /* decode a picture */
    FfmpegDecoder_DecodePicture(self, packet);
                     
    return BLT_SUCCESS;
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
    
    /* check if the queue is empty */
    ATX_ListItem* head = ATX_List_GetFirstItem(self->output.pictures);
    if (head == NULL) return BLT_ERROR_PORT_HAS_NO_DATA;

    /* return the next queued picture */
    *packet = ATX_ListItem_GetData(head);
    BLT_MediaPacket_AddReference(*packet);
    ATX_List_RemoveItem(self->output.pictures, head);
    
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
    /* setup the output port */
    BLT_RawVideoMediaType_Init(&self->output.media_type);
    self->output.media_type.format = BLT_PIXEL_FORMAT_YV12;
    
    /* setup the list of decoded pictures */
    {
        ATX_ListDataDestructor destructor = { NULL, FfmpegDecoder_ListItemDestruct };
        ATX_List_CreateEx(&destructor, &self->output.pictures);
    }
    
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

    /* configure options */
    {
        ATX_Properties* properties = NULL;
        if (BLT_SUCCEEDED(BLT_Core_GetProperties(core, &properties))) {
            ATX_PropertyValue property;
            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(properties, "com.axiosys.decoders.ffmpeg.hwaccel", &property))) {
                if (property.type == ATX_PROPERTY_VALUE_TYPE_BOOLEAN) {
                    self->enable_hwaccel = property.data.boolean;
                }
            }
        }
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

    /* release input resources */
    if (self->input.media_type) {
        BLT_MediaType_Free((BLT_MediaType*)self->input.media_type);
    }
    
    /* release avcodec resources */
    if (self->codec_context) {
        avcodec_close(self->codec_context);
        av_free(self->codec_context);
    }
    if (self->parser_context) {
        av_parser_close(self->parser_context);
    }
    if (self->frame) av_free(self->frame);
    
    /* free buffered pictures */
    ATX_List_Destroy(self->output.pictures);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));
    
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

    /* flush anything that may be pending */
    if (self->codec_context) {
        avcodec_flush_buffers(self->codec_context);
        self->codec_context->skip_frame = AVDISCARD_DEFAULT;        
    }
    ATX_List_Clear(self->output.pictures);
    
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

    /* register the type id for video/H264 */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "video/H264",
        &self->h264_es_type_id);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1("video/H264 type = %d)", self->h264_es_type_id);

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
    
    /* initialize libavcodec */
    /*avcodec_init();*/
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
            if (constructor->spec.input.media_type->id != self->h264_es_type_id &&
                constructor->spec.input.media_type->id != self->mp4_video_es_type_id &&
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
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(FfmpegDecoderModule,
                                         "FFMPEG Decoder",
                                         "com.axiosys.decoders.ffmpeg",
                                         "1.2.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
