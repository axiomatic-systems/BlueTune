/*****************************************************************
|
|   BlueTune - Apple AudioConverter Decoder Module
|
|   (c) 2008-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltOsxAudioConverterDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltMediaPacket.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"
#include "BltPixels.h"

#if !defined(__COREAUDIO_USE_FLAT_INCLUDES__)
    #include <AudioToolbox/AudioToolbox.h>
    #include <CoreFoundation/CoreFoundation.h>
#else
    #include "AudioToolbox.h"
    #include "CoreFoundation.h"
#endif

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.osx.audio-converter")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
const unsigned int BLT_OSX_AUDIO_CONVERTER_DECODER_PACKETS_PER_CONVERSION = 1024;
const unsigned int BLT_OSX_AUDIO_CONVERTER_DECODER_MAX_OUTPUT_BUFFER_SIZE = 1024*2*8; /* max 8 channels @ 16 bits */
const int          BLT_OSX_AUDIO_CONVERTER_DATA_UNDERFLOW_ERROR = 1234; /* arbitrary positive value */

#define BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_LC 0x67
#define BLT_AAC_OBJECT_TYPE_ID_MPEG4_AUDIO  0x40

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 mpeg_audio_type_id;
    BLT_UInt32 mp4es_type_id;
    BLT_UInt32 asbd_media_type_id;
} OsxAudioConverterDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_MediaPacket*             pending_packet;
    BLT_MediaPacket*             current_packet;
    AudioStreamPacketDescription current_packet_desc;
    ATX_UInt64                   packets_since_seek;
} OsxAudioConverterDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_PcmMediaType media_type;
    unsigned char    buffer[BLT_OSX_AUDIO_CONVERTER_DECODER_MAX_OUTPUT_BUFFER_SIZE];
    bool             started;
    BLT_TimeStamp    timestamp_base;
    ATX_UInt64       samples_since_seek;
} OsxAudioConverterDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    OsxAudioConverterDecoderModule* module;
    OsxAudioConverterDecoderInput   input;
    OsxAudioConverterDecoderOutput  output;
    AudioConverterRef               converter;
} OsxAudioConverterDecoder;

typedef struct {
    BLT_MediaType               base;
    AudioStreamBasicDescription asbd;
    unsigned int                magic_cookie_size;
    unsigned char               magic_cookie[1];
    // followed by zero or more magic_cookie bytes
} AsbdMediaType;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
    
/*----------------------------------------------------------------------
|   OsxAudioConverterDecoder_DataProc
+---------------------------------------------------------------------*/
static OSStatus
OsxAudioConverterDecoder_DataProc(AudioConverterRef             inAudioConverter,
                                  UInt32                        *ioNumberDataPackets,
                                  AudioBufferList               *ioData,
                                  AudioStreamPacketDescription  **outDataPacketDescription,
                                  void                          *inUserData)
{
    OsxAudioConverterDecoder* self = (OsxAudioConverterDecoder*)inUserData;
 
    ATX_COMPILER_UNUSED(inAudioConverter);

    /* switch the current packet */
    if (self->input.current_packet) {
        BLT_MediaPacket_Release(self->input.current_packet);
        self->input.current_packet = NULL;
    }
    self->input.current_packet = self->input.pending_packet;
    self->input.pending_packet = NULL;
    
    /* if we don't have data available, return nothing */
    if (self->input.current_packet == NULL) {
        *ioNumberDataPackets = 0;
        return BLT_OSX_AUDIO_CONVERTER_DATA_UNDERFLOW_ERROR;
    }
    
    /* return one packet's worth of data */
    ioData->mNumberBuffers              = 1;
    ioData->mBuffers[0].mNumberChannels = 0;
    ioData->mBuffers[0].mDataByteSize   = BLT_MediaPacket_GetPayloadSize(self->input.current_packet);
    ioData->mBuffers[0].mData = BLT_MediaPacket_GetPayloadBuffer(self->input.current_packet);
    *ioNumberDataPackets = 1;
    if (outDataPacketDescription) {
        self->input.current_packet_desc.mStartOffset            = 0;
        self->input.current_packet_desc.mDataByteSize           = ioData->mBuffers[0].mDataByteSize;
        self->input.current_packet_desc.mVariableFramesInPacket = 0;
        *outDataPacketDescription = &self->input.current_packet_desc;
    }
    return noErr;
}

/*----------------------------------------------------------------------
|   OsxAudioConverterDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioConverterDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                                        BLT_MediaPacket*    packet)
{
    OsxAudioConverterDecoder* self = ATX_SELF_M(input, OsxAudioConverterDecoder, BLT_PacketConsumer);
    
    if (self->input.pending_packet) {
        BLT_MediaPacket_Release(self->input.pending_packet);
    }
    self->input.pending_packet = packet;
    BLT_MediaPacket_AddReference(packet);
    
    if (self->input.packets_since_seek++ == 0) {
        self->output.timestamp_base = BLT_MediaPacket_GetTimeStamp(packet);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioConverterDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(OsxAudioConverterDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(OsxAudioConverterDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxAudioConverterDecoderInput, BLT_PacketConsumer)
    OsxAudioConverterDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(OsxAudioConverterDecoderInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(OsxAudioConverterDecoderInput, BLT_MediaPort)
    OsxAudioConverterDecoderInput_GetName,
    OsxAudioConverterDecoderInput_GetProtocol,
    OsxAudioConverterDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   OsxAudioConverterDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioConverterDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                                         BLT_MediaPacket**   packet)
{
    OsxAudioConverterDecoder* self = ATX_SELF_M(output, OsxAudioConverterDecoder, BLT_PacketProducer);
    OSStatus                  status;
    
    // default return
    *packet = NULL;
        
    if (self->converter == NULL) {
        /* setup the source format */
        AudioStreamBasicDescription source_format;
        NPT_SetMemory(&source_format, 0, sizeof(source_format));

        /* setup the defaults of the source format */
        source_format.mSampleRate       = 44100.0;
        source_format.mFormatFlags      = 0;
        source_format.mBytesPerPacket   = 0;
        source_format.mFramesPerPacket  = 0;
        source_format.mBytesPerFrame    = 0;
        source_format.mChannelsPerFrame = 2;
        source_format.mBitsPerChannel   = 0;

        /* be more specific about the format based on the codec we're using */
        const BLT_MediaType* input_type;
        BLT_MediaPacket_GetMediaType(self->input.pending_packet, &input_type);
        if (input_type->id == self->module->asbd_media_type_id) {
            const AsbdMediaType* asbd_type = (const AsbdMediaType*)input_type;
            source_format = asbd_type->asbd;
        } else if (input_type->id == self->module->mpeg_audio_type_id) {
            source_format.mFormatID = kAudioFormatMPEGLayer3;
        } else if (input_type->id == self->module->mp4es_type_id) {
            const BLT_Mp4AudioMediaType* mp4_type = (const BLT_Mp4AudioMediaType*)input_type;
            if (mp4_type->base.format_or_object_type_id == BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_LC ||
                mp4_type->base.format_or_object_type_id == BLT_AAC_OBJECT_TYPE_ID_MPEG4_AUDIO) {
                if (mp4_type->decoder_info_length > 2) {
                    // if the decoder info is more than 2 bytes, assume this is He-AAC
                    source_format.mFormatID = kAudioFormatMPEG4AAC_HE_V2;
                    source_format.mSampleRate = mp4_type->sample_rate;
                    if (source_format.mSampleRate < 32000.0) {
                        source_format.mSampleRate *= 2;
                    }
                } else {
                    source_format.mFormatID = kAudioFormatMPEG4AAC;
                }
            } else {
                return BLT_ERROR_UNSUPPORTED_CODEC;
            }
        } else {
            return BLT_ERROR_UNSUPPORTED_CODEC;
        }
        
        /* setup the dest format */
        AudioStreamBasicDescription dest_format;
        NPT_SetMemory(&dest_format, 0, sizeof(dest_format));
        dest_format.mFormatID         = kAudioFormatLinearPCM;
        dest_format.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        dest_format.mSampleRate       = 0;
        dest_format.mBytesPerPacket   = 0;
        dest_format.mFramesPerPacket  = 1;
        dest_format.mBytesPerFrame    = 0;
        dest_format.mChannelsPerFrame = 2;
        dest_format.mBitsPerChannel   = 16;
                   
        status = AudioConverterNew(&source_format,
                                   &dest_format,
                                   &self->converter);
        if (status != noErr) {
            ATX_LOG_FINER_1("AudioConvertNew failed (%d)", status);
            return BLT_ERROR_UNSUPPORTED_FORMAT;
        }
        
        /* notify of the format name */
        if (ATX_BASE(self, BLT_BaseMediaNode).context) {
            BLT_StreamInfo info;
            switch (source_format.mFormatID) {
                case kAudioFormatMPEG4AAC:       info.data_type = "MPEG-4 AAC";      break;
                case kAudioFormatMPEG4AAC_HE:    info.data_type = "MPEG-4 He-AAC";   break;
                case kAudioFormatMPEG4AAC_HE_V2: info.data_type = "MPEG-4 He-AACv2"; break;
                case kAudioFormatMPEGLayer3:     info.data_type = "MP3";             break;
                default:                         info.data_type = NULL;
            }
            if (info.data_type) {
                info.mask = BLT_STREAM_INFO_MASK_DATA_TYPE;
                BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &info);
            }
        }
        
        /* setup codec-specific parameters */
        if (input_type->id == self->module->mp4es_type_id) {
            const BLT_Mp4AudioMediaType* mp4_type = (const BLT_Mp4AudioMediaType*)input_type;

            /* cheat a bit: pretent that MPEG2_AAC_LC is actually MPEG4_AUDIO */
            unsigned int oti = mp4_type->base.format_or_object_type_id;
            if (oti == BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_LC) {
                oti = BLT_AAC_OBJECT_TYPE_ID_MPEG4_AUDIO;
            }
            
            if (oti == BLT_AAC_OBJECT_TYPE_ID_MPEG4_AUDIO) {
                unsigned int magic_cookie_size = mp4_type->decoder_info_length+25;
                unsigned char* magic_cookie = new unsigned char[magic_cookie_size];
        
                /* construct the content of the magic cookie (the 'ES Descriptor') */
                magic_cookie[ 0] = 0x03;                 /* ES_Descriptor tag */
                magic_cookie[ 1] = magic_cookie_size-2;  /* ES_Descriptor payload size */
                magic_cookie[ 2] = 0;                    /* ES ID */      
                magic_cookie[ 3] = 0;                    /* ES ID */
                magic_cookie[ 4] = 0;                    /* flags */
                magic_cookie[ 5] = 0x04;                 /* DecoderConfig tag */
                magic_cookie[ 6] = magic_cookie_size-10; /* DecoderConfig payload size */
                magic_cookie[ 7] = oti;                  /* object type */
                magic_cookie[ 8] = 0x05<<2 | 1;          /* stream type | reserved */
                magic_cookie[ 9] = 0;                    /* buffer size */
                magic_cookie[10] = 0x18;                 /* buffer size */
                magic_cookie[11] = 0;                    /* buffer size */
                magic_cookie[12] = 0;                    /* max bitrate */
                magic_cookie[13] = 0x08;                 /* max bitrate */
                magic_cookie[14] = 0;                    /* max bitrate */
                magic_cookie[15] = 0;                    /* max bitrate */
                magic_cookie[16] = 0;                    /* avg bitrate */
                magic_cookie[17] = 0x04;                 /* avg bitrate */
                magic_cookie[18] = 0;                    /* avg bitrate */
                magic_cookie[19] = 0;                    /* avg bitrate */
                magic_cookie[20] = 0x05;                 /* DecoderSpecificInfo tag */
                magic_cookie[21] = mp4_type->decoder_info_length; /* DecoderSpecificInfo payload size */
                if (mp4_type->decoder_info_length) {
                    ATX_CopyMemory(&magic_cookie[22], mp4_type->decoder_info, mp4_type->decoder_info_length);
                }
                magic_cookie[22+mp4_type->decoder_info_length  ] = 0x06; /* SLConfigDescriptor tag    */
                magic_cookie[22+mp4_type->decoder_info_length+1] = 0x01; /* SLConfigDescriptor length */
                magic_cookie[22+mp4_type->decoder_info_length+2] = 0x02; /* fixed                     */
        
                status = AudioConverterSetProperty(self->converter,
                                                   kAudioConverterDecompressionMagicCookie,
                                                   magic_cookie_size,
                                                   magic_cookie);
                delete[] magic_cookie;
                if (status != noErr) {
                    ATX_LOG_WARNING_1("failed to set codec magic cookie (%d)", status);
                    return BLT_ERROR_UNSUPPORTED_FORMAT;
                }
            }
        }
    }
    
    // decode some of the input data
    AudioBufferList output_buffers;
    UInt32          output_packet_count = BLT_OSX_AUDIO_CONVERTER_DECODER_PACKETS_PER_CONVERSION;
    output_buffers.mNumberBuffers              = 1;
    output_buffers.mBuffers[0].mNumberChannels = 0;
    output_buffers.mBuffers[0].mDataByteSize   = BLT_OSX_AUDIO_CONVERTER_DECODER_MAX_OUTPUT_BUFFER_SIZE;
    output_buffers.mBuffers[0].mData           = self->output.buffer;
    status = AudioConverterFillComplexBuffer(self->converter,
                                             OsxAudioConverterDecoder_DataProc,
                                             (void*)self,
                                             &output_packet_count,
                                             &output_buffers,
                                             NULL);

    if (status != noErr && status != BLT_OSX_AUDIO_CONVERTER_DATA_UNDERFLOW_ERROR) {
        ATX_LOG_WARNING_1("AudioConverterFillComplexBuffer() failed (%d)", status);
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    if (output_packet_count == 0) {
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
    
    // look at the input format
    AudioStreamBasicDescription input_format;
    UInt32 input_format_size = sizeof(input_format);
    status = AudioConverterGetProperty(self->converter,
                                       kAudioConverterCurrentInputStreamDescription,
                                       &input_format_size,
                                       &input_format);
    
    // look at the format
    AudioStreamBasicDescription output_format;
    UInt32 output_format_size = sizeof(output_format);
    status = AudioConverterGetProperty(self->converter,
                                       kAudioConverterCurrentOutputStreamDescription,
                                       &output_format_size,
                                       &output_format);
                                        
    // check if the format has changed and notify if it has
    if (self->output.media_type.sample_rate != output_format.mSampleRate) {
        if (ATX_BASE(self, BLT_BaseMediaNode).context) {
            BLT_StreamInfo info;
            info.sample_rate   = output_format.mSampleRate;
            info.channel_count = output_format.mChannelsPerFrame;
            info.mask = 
                BLT_STREAM_INFO_MASK_SAMPLE_RATE  |
                BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
            BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &info);
        }
    }
    
    // update the sample counters
    if (self->output.media_type.sample_rate && self->output.media_type.sample_rate != output_format.mSampleRate) {
        double ratio = (double)output_format.mSampleRate/(double)self->output.media_type.sample_rate;
        self->output.samples_since_seek = (ATX_UInt64)((double)self->output.samples_since_seek*ratio);
    }
    if (output_format.mChannelsPerFrame) {
        unsigned int sample_count = output_buffers.mBuffers[0].mDataByteSize/(2*output_format.mChannelsPerFrame);
        self->output.samples_since_seek += sample_count;
    }
    
    // return a media packet
    self->output.media_type.sample_rate     = output_format.mSampleRate;
    self->output.media_type.channel_count   = output_format.mChannelsPerFrame;
    self->output.media_type.bits_per_sample = 16;
    self->output.media_type.channel_mask    = 0;
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                               output_buffers.mBuffers[0].mDataByteSize,
                               &self->output.media_type.base,
                               packet);
    NPT_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(*packet),
                   output_buffers.mBuffers[0].mData,
                   output_buffers.mBuffers[0].mDataByteSize);
    BLT_MediaPacket_SetPayloadSize(*packet, output_buffers.mBuffers[0].mDataByteSize);
    if (!self->output.started) {
        BLT_MediaPacket_SetFlags(*packet, BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
        self->output.started = true;
    }
    
    // compute the timestamp
    if (self->output.media_type.sample_rate) {
        BLT_TimeStamp elapsed = BLT_TimeStamp_FromSamples(self->output.samples_since_seek,
                                                     self->output.media_type.sample_rate);
        BLT_MediaPacket_SetTimeStamp(*packet, BLT_TimeStamp_Add(elapsed, self->output.timestamp_base));
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioConverterDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(OsxAudioConverterDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(OsxAudioConverterDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(OsxAudioConverterDecoderOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(OsxAudioConverterDecoderOutput, BLT_MediaPort)
    OsxAudioConverterDecoderOutput_GetName,
    OsxAudioConverterDecoderOutput_GetProtocol,
    OsxAudioConverterDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxAudioConverterDecoderOutput, BLT_PacketProducer)
    OsxAudioConverterDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    OsxAudioConverterDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioConverterDecoder_Destroy(OsxAudioConverterDecoder* self)
{ 

    ATX_LOG_FINE("enter");

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));
    
    /* free resources */
    if (self->input.pending_packet) {
        BLT_MediaPacket_Release(self->input.pending_packet);
    }
    if (self->input.current_packet) {
        BLT_MediaPacket_Release(self->input.current_packet);
    }
    if (self->converter) {
        AudioConverterDispose(self->converter);
    }
    
    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   OsxAudioConverterDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioConverterDecoder_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    OsxAudioConverterDecoder* self = ATX_SELF_EX(OsxAudioConverterDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    OsxAudioConverterDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioConverterDecoder_Seek(BLT_MediaNode* _self,
                              BLT_SeekMode*  mode,
                              BLT_SeekPoint* point)
{
    OsxAudioConverterDecoder* self = ATX_SELF_EX(OsxAudioConverterDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    if (self->input.pending_packet) {
        BLT_MediaPacket_Release(self->input.pending_packet);
        self->input.pending_packet = NULL;
    }
    if (self->input.current_packet) {
        BLT_MediaPacket_Release(self->input.current_packet);
        self->input.current_packet = NULL;
    }
    self->input.packets_since_seek = 0;
    
    if (self->converter) {
        AudioConverterReset(self->converter);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioConverterDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioConverterDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioConverterDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(OsxAudioConverterDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    OsxAudioConverterDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    OsxAudioConverterDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxAudioConverterDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|    OsxAudioConverterDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioConverterDecoder_Create(BLT_Module*              module,
                                BLT_Core*                core,
                                BLT_ModuleParametersType parameters_type,
                                const void*              parameters,
                                BLT_MediaNode**          object)
{
    OsxAudioConverterDecoder* self;
    
    ATX_LOG_FINE("enter");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }
    
    /* allocate memory for the object */
    self = (OsxAudioConverterDecoder*)ATX_AllocateZeroMemory(sizeof(OsxAudioConverterDecoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }
    
    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);
    
    /* construct the object */
    self->module = (OsxAudioConverterDecoderModule*)module;
    
    /* setup the input and output ports */
    BLT_PcmMediaType_Init(&self->output.media_type);
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, OsxAudioConverterDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, OsxAudioConverterDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  OsxAudioConverterDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  OsxAudioConverterDecoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, OsxAudioConverterDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, OsxAudioConverterDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);
    
    return BLT_SUCCESS;
}    
    
/*----------------------------------------------------------------------
|   OsxAudioConverterDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioConverterDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    OsxAudioConverterDecoderModule* self = ATX_SELF_EX(OsxAudioConverterDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*                   registry;
    BLT_Result                      result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the .mp2, .mp1, .mp3 .mpa and .mpg file extensions */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mp3",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mp2",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mp1",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mpa",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mpg",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/mpeg" type */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mpeg",
        &self->mpeg_audio_type_id);
    if (BLT_FAILED(result)) return result;
    
    /* register the MP4 elementary stream type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_MP4_AUDIO_ES_MIME_TYPE,
        &self->mp4es_type_id);
    if (BLT_FAILED(result)) return result;

    /* register the audio/x-apple-asbd type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-apple-asbd",
        &self->asbd_media_type_id);
    if (BLT_FAILED(result)) return result;

    /* register mime type aliases */
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/mp3", self->mpeg_audio_type_id);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mp3", self->mpeg_audio_type_id);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/mpg", self->mpeg_audio_type_id);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mpg", self->mpeg_audio_type_id);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mpeg", self->mpeg_audio_type_id);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/mpeg3", self->mpeg_audio_type_id);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mpeg3", self->mpeg_audio_type_id);

    ATX_LOG_FINE_1("(audio/mpeg type = %d)", self->mpeg_audio_type_id);
    ATX_LOG_FINE_1("(" BLT_MP4_AUDIO_ES_MIME_TYPE " = %d)", self->mp4es_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   OsxAudioConverterDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioConverterDecoderModule_Probe(BLT_Module*              _self, 
                                     BLT_Core*                core,
                                     BLT_ModuleParametersType parameters_type,
                                     BLT_AnyConst             parameters,
                                     BLT_Cardinal*            match)
{
    OsxAudioConverterDecoderModule* self = ATX_SELF_EX(OsxAudioConverterDecoderModule, BLT_BaseModule, BLT_Module);
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

            /* the input type should be one of the supported types */
            if (constructor->spec.input.media_type->id != self->mpeg_audio_type_id &&
                constructor->spec.input.media_type->id != self->mp4es_type_id      &&
                constructor->spec.input.media_type->id != self->asbd_media_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/pcm */
            if (constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_AUDIO_PCM &&
                constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "com.bluetune.decoders.osx.audio-converter")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 5;
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioConverterDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioConverterDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioConverterDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(OsxAudioConverterDecoderModule, OsxAudioConverterDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(OsxAudioConverterDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    OsxAudioConverterDecoderModule_Attach,
    OsxAudioConverterDecoderModule_CreateInstance,
    OsxAudioConverterDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define OsxAudioConverterDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxAudioConverterDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   node constructor
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(OsxAudioConverterDecoderModule, "Apple AudioConverter Decoder", 0)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_OsxAudioConverterDecoderModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return OsxAudioConverterDecoderModule_Create(object);
}
