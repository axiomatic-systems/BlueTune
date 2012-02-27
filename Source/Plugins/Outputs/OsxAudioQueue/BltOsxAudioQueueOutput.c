/*****************************************************************
|
|   OSX Audio Queue Output Module
|
|   (c) 2002-2010 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include <AvailabilityMacros.h>
#if (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5) || (__IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_2_0)
#include <mach/mach_time.h>
#include <AudioToolbox/AudioQueue.h>
#include <AudioToolbox/AudioFormat.h>
#endif
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltOsxAudioQueueOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"
#include "BltVolumeControl.h"
#include "BltCommonMediaTypes.h"

#if (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5) || (__IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_2_0)

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.osx.audio-queue")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT             8
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFERS_TOTAL_DURATION   1000 /* milliseconds */
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_PACKET_DESCRIPTION_COUNT 512
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_DEFAULT_BUFFER_SIZE      32768
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_MAX_WAIT                 3 /* seconds */
#define BLT_OSX_AUDIO_QUEUE_UNDERFLOW_THRESHOLD             11025 /* number of samples */

#define BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_MAIN 0x66
#define BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_LC   0x67
#define BLT_AAC_OBJECT_TYPE_ID_MPEG4_AUDIO    0x40

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_MediaType               base;
    AudioStreamBasicDescription asbd;
    unsigned int                magic_cookie_size;
    unsigned char               magic_cookie[1];
    // followed by zero or more magic_cookie bytes
} AsbdMediaType;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
    
    /* members */
    BLT_MediaTypeId asbd_media_type_id;
    BLT_MediaTypeId mp4_es_media_type_id;
} OsxAudioQueueOutputModule;

typedef struct {
    AudioQueueBufferRef data;
    BLT_UInt64          timestamp;
    BLT_UInt64          duration;
} OsxAudioQueueBuffer;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_VolumeControl);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    pthread_mutex_t              lock;
    AudioQueueRef                audio_queue;
    BLT_Boolean                  audio_queue_started;
    BLT_Boolean                  audio_queue_paused;
    pthread_cond_t               audio_queue_stopped_cond;
    BLT_Boolean                  waiting_for_stop;
    AudioStreamBasicDescription  audio_format;
    Float32                      volume;
    OsxAudioQueueBuffer          buffers[BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT];
    BLT_Ordinal                  buffer_index;
    pthread_cond_t               buffer_released_cond;
    AudioStreamPacketDescription packet_descriptions[BLT_OSX_AUDIO_QUEUE_OUTPUT_PACKET_DESCRIPTION_COUNT];
    BLT_Ordinal                  packet_count;
    BLT_Cardinal                 packet_count_max;
    struct {
        BLT_UInt64 packet_time;
        BLT_UInt64 max_time;
        UInt64     host_time;
    }                            timestamp_snapshot;
    struct {
        BLT_PcmMediaType      pcm;
        AsbdMediaType         asbd;
        BLT_Mp4AudioMediaType mp4;
    }                            expected_media_types;
} OsxAudioQueueOutput;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_VolumeControl)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_PacketConsumer)

BLT_METHOD OsxAudioQueueOutput_Resume(BLT_MediaNode* self);
BLT_METHOD OsxAudioQueueOutput_Stop(BLT_MediaNode* self);
BLT_METHOD OsxAudioQueueOutput_Drain(BLT_OutputNode* self);

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
const unsigned int MP4_AAC_MAX_SAMPLING_FREQUENCY_INDEX = 12;
static const unsigned int MP4_AacSamplingFreqTable[13] =
{
	96000, 88200, 64000, 48000, 
    44100, 32000, 24000, 22050, 
    16000, 12000, 11025, 8000, 
    7350
};

#define MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR             5  /**< Spectral Band Replication                    */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_PS              29 /**< Parametric Stereo                            */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_MAIN        1  /**< AAC Main Profile                             */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_LC          2  /**< AAC Low Complexity                           */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_SSR         3  /**< AAC Scalable Sample Rate                     */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_LTP         4  /**< AAC Long Term Predictor                      */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR             5  /**< Spectral Band Replication                    */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_SCALABLE    6  /**< AAC Scalable                                 */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_TWINVQ          7  /**< Twin VQ                                      */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LC       17 /**< Error Resilient AAC Low Complexity           */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LTP      19 /**< Error Resilient AAC Long Term Prediction     */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_SCALABLE 20 /**< Error Resilient AAC Scalable                 */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_TWINVQ       21 /**< Error Resilient Twin VQ                      */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_BSAC         22 /**< Error Resilient Bit Sliced Arithmetic Coding */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LD       23 /**< Error Resilient AAC Low Delay                */
#define MP4_MPEG4_AUDIO_OBJECT_TYPE_PS              29 /**< Parametric Stereo                            */


/*----------------------------------------------------------------------
|   Mp4AudioDsiParser
+---------------------------------------------------------------------*/
typedef struct {
    const unsigned char* data;
    unsigned int         data_size;
    unsigned int         position;
} Mp4AudioDsiParser;

/*----------------------------------------------------------------------
|   Mp4AudioDsiParser_BitsLeft
+---------------------------------------------------------------------*/
#define Mp4AudioDsiParser_BitsLeft(_bits) (8*(_bits)->data_size-(_bits)->position) 

/*----------------------------------------------------------------------
|   Mp4AudioDsiParser_ReadBits
+---------------------------------------------------------------------*/
static ATX_UInt32 
Mp4AudioDsiParser_ReadBits(Mp4AudioDsiParser* self, unsigned int n) 
{
    ATX_UInt32 result = 0;
    const unsigned char* data = self->data;
    while (n) {
        unsigned int bits_avail = 8-(self->position%8);
        unsigned int chunk_size = bits_avail >= n ? n : bits_avail;
        unsigned int chunk_bits = (((unsigned int)(data[self->position/8]))>>(bits_avail-chunk_size))&((1<<chunk_size)-1);
        result = (result << chunk_size) | chunk_bits;
        n -= chunk_size;
        self->position += chunk_size;
    }

    return result;
}

/*----------------------------------------------------------------------
|   Mp4AudioDecoderConfig
+---------------------------------------------------------------------*/
typedef struct {
    // members
    ATX_UInt8            object_type;              /**< Type identifier for the audio data */
    unsigned int         sampling_frequency_index; /**< Index of the sampling frequency in the sampling frequency table */
    unsigned int         sampling_frequency;       /**< Sampling frequency */
    unsigned int         channel_count;            /**< Number of audio channels */
    /** Extension details */
    struct {
        ATX_Boolean  sbr_present;              /**< SBR is present        */
        ATX_Boolean  ps_present;               /**< PS is present         */
        ATX_UInt8    object_type;              /**< Extension object type */
        unsigned int sampling_frequency_index; /**< Sampling frequency index of the extension */
        unsigned int sampling_frequency;       /**< Sampling frequency of the extension */
    } extension;
} Mp4AudioDecoderConfig;

/*----------------------------------------------------------------------
|   Mp4AudioDecoderConfig_ParseAudioObjectType
+---------------------------------------------------------------------*/
static BLT_Result
Mp4AudioDecoderConfig_ParseAudioObjectType(Mp4AudioDecoderConfig* self,
                                           Mp4AudioDsiParser*     parser, 
                                           ATX_UInt8*             object_type)
{
    BLT_COMPILER_UNUSED(self);
    if (Mp4AudioDsiParser_BitsLeft(parser) < 5) return BLT_ERROR_INVALID_MEDIA_FORMAT;
    *object_type = (ATX_UInt8)Mp4AudioDsiParser_ReadBits(parser, 5);
	if (*object_type == 31) {
        if (Mp4AudioDsiParser_BitsLeft(parser) < 6) return BLT_ERROR_INVALID_MEDIA_FORMAT;
		*object_type = (ATX_UInt8)(32 + Mp4AudioDsiParser_ReadBits(parser, 6));
	}
	return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   Mp4AudioDecoderConfig_ParseGASpecificInfo
+---------------------------------------------------------------------*/
static BLT_Result
Mp4AudioDecoderConfig_ParseGASpecificInfo(Mp4AudioDecoderConfig* self,
                                          Mp4AudioDsiParser*     parser)
{
    ATX_Boolean frame_length_flag = ATX_FALSE;
    ATX_Boolean depends_on_core_coder = ATX_FALSE;
    unsigned int core_coder_delay = 0;
    unsigned int extension_flag = 0;
    if (Mp4AudioDsiParser_BitsLeft(parser) < 2) return BLT_ERROR_INVALID_MEDIA_FORMAT;
	frame_length_flag = (Mp4AudioDsiParser_ReadBits(parser, 1) == 1)?ATX_TRUE:ATX_FALSE;
	depends_on_core_coder = (Mp4AudioDsiParser_ReadBits(parser, 1) == 1)?ATX_TRUE:ATX_FALSE;
	if (depends_on_core_coder) {		
        if (Mp4AudioDsiParser_BitsLeft(parser) < 14) return BLT_ERROR_INVALID_MEDIA_FORMAT;
		core_coder_delay = Mp4AudioDsiParser_ReadBits(parser, 14);
    } 
    if (Mp4AudioDsiParser_BitsLeft(parser) < 1) return BLT_ERROR_INVALID_MEDIA_FORMAT;
	extension_flag = Mp4AudioDsiParser_ReadBits(parser, 1);
    if (self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_SCALABLE ||
        self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_SCALABLE) {
        if (Mp4AudioDsiParser_BitsLeft(parser) < 3) return BLT_ERROR_INVALID_MEDIA_FORMAT;
        Mp4AudioDsiParser_ReadBits(parser, 3);
    }
    if (extension_flag) {
        if (self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_BSAC) {
            if (Mp4AudioDsiParser_BitsLeft(parser) < 16) return BLT_ERROR_INVALID_MEDIA_FORMAT;
            Mp4AudioDsiParser_ReadBits(parser, 16); /* numOfSubFrame (5); layer_length (11) */
        }
        if (self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LC       ||
            self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_SCALABLE ||
            self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LD) {
            if (Mp4AudioDsiParser_BitsLeft(parser) < 3) return BLT_ERROR_INVALID_MEDIA_FORMAT;
            Mp4AudioDsiParser_ReadBits(parser, 3); /* aacSectionDataResilienceFlag (1)     */
                                                   /* aacScalefactorDataResilienceFlag (1) */
                                                   /* aacSpectralDataResilienceFlag (1)    */
        }
        if (Mp4AudioDsiParser_BitsLeft(parser) < 1) return BLT_ERROR_INVALID_MEDIA_FORMAT;
        Mp4AudioDsiParser_ReadBits(parser, 1);
    }
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   Mp4AudioDecoderConfig_ParseSamplingFrequency
+---------------------------------------------------------------------*/
static BLT_Result
Mp4AudioDecoderConfig_ParseSamplingFrequency(Mp4AudioDecoderConfig* self,
                                             Mp4AudioDsiParser*     parser, 
                                             unsigned int*          sampling_frequency_index,
                                             unsigned int*          sampling_frequency)
{
    BLT_COMPILER_UNUSED(self);

    if (Mp4AudioDsiParser_BitsLeft(parser) < 4) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    *sampling_frequency_index = Mp4AudioDsiParser_ReadBits(parser, 4);
    if (*sampling_frequency_index == 0xF) {
        if (Mp4AudioDsiParser_BitsLeft(parser) < 24) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        *sampling_frequency = Mp4AudioDsiParser_ReadBits(parser, 24);
    } else if (*sampling_frequency_index <= MP4_AAC_MAX_SAMPLING_FREQUENCY_INDEX) {
        *sampling_frequency = MP4_AacSamplingFreqTable[*sampling_frequency_index];
    } else {
        *sampling_frequency = 0;
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_Mp4AudioDecoderConfig::ParseExtension
+---------------------------------------------------------------------*/
static BLT_Result
Mp4AudioDecoderConfig_ParseExtension(Mp4AudioDecoderConfig* self,
                                     Mp4AudioDsiParser*     parser)
{
    unsigned int sync_extension_type;
    if (Mp4AudioDsiParser_BitsLeft(parser) < 16) return BLT_ERROR_INVALID_MEDIA_FORMAT;
    sync_extension_type = Mp4AudioDsiParser_ReadBits(parser, 11);
    if (sync_extension_type == 0x2b7) {
        BLT_Result result = Mp4AudioDecoderConfig_ParseAudioObjectType(self, 
                                                                       parser, 
                                                                       &self->extension.object_type);
        if (BLT_FAILED(result)) return result;
        if (self->extension.object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR) {
            self->extension.sbr_present = (Mp4AudioDsiParser_ReadBits(parser, 1) == 1);
            if (self->extension.sbr_present) {
                result = Mp4AudioDecoderConfig_ParseSamplingFrequency(self, 
                                                                      parser, 
                                                                      &self->extension.sampling_frequency_index,
                                                                      &self->extension.sampling_frequency);
                if (BLT_FAILED(result)) return result;
                if (Mp4AudioDsiParser_BitsLeft(parser) >= 12) {
                    sync_extension_type = Mp4AudioDsiParser_ReadBits(parser, 11);
                    if (sync_extension_type == 0x548) {
                        self->extension.ps_present = (Mp4AudioDsiParser_ReadBits(parser, 1) == 1);
                    }
                }
            }
        } else if (self->extension.object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_BSAC) {
            self->extension.sbr_present = (Mp4AudioDsiParser_ReadBits(parser, 1) == 1);
            if (self->extension.sbr_present) {
                result = Mp4AudioDecoderConfig_ParseSamplingFrequency(self, 
                                                                      parser, 
                                                                      &self->extension.sampling_frequency_index,
                                                                      &self->extension.sampling_frequency);
                if (BLT_FAILED(result)) return result;
            } 
            Mp4AudioDsiParser_ReadBits(parser, 4); /* extensionChannelConfiguration */
        }
    }
	return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_Mp4AudioDecoderConfig_Parse
+---------------------------------------------------------------------*/
static BLT_Result
Mp4AudioDecoderConfig_Parse(Mp4AudioDecoderConfig* self,
                            const unsigned char*   data, 
                            unsigned int           data_size)
{
    BLT_Result        result;
    Mp4AudioDsiParser bits = {data, data_size, 0};

    // default config
    ATX_SetMemory(self, 0, sizeof(*self));
    
    // parse the audio object type
	result = Mp4AudioDecoderConfig_ParseAudioObjectType(self, &bits, &self->object_type);
    if (BLT_FAILED(result)) return result;

    // parse the sampling frequency
    result = Mp4AudioDecoderConfig_ParseSamplingFrequency(self, &bits, 
                                                          &self->sampling_frequency_index, 
                                                          &self->sampling_frequency);
    if (BLT_FAILED(result)) return result;

    if (Mp4AudioDsiParser_BitsLeft(&bits) < 4) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
	self->channel_count = Mp4AudioDsiParser_ReadBits(&bits, 4);
    
	if (self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR ||
        self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_PS) {
		self->extension.object_type = MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR;
		self->extension.sbr_present = ATX_TRUE;
        self->extension.ps_present  = (self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_PS)?ATX_TRUE:ATX_FALSE;
        result = Mp4AudioDecoderConfig_ParseSamplingFrequency(self, &bits, 
                                                              &self->extension.sampling_frequency_index, 
                                                              &self->extension.sampling_frequency);
        if (BLT_FAILED(result)) return result;
		result = Mp4AudioDecoderConfig_ParseAudioObjectType(self, &bits, &self->object_type);
        if (BLT_FAILED(result)) return result;
        if (self->object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_BSAC) {
            if (Mp4AudioDsiParser_BitsLeft(&bits) < 4) return BLT_ERROR_INVALID_MEDIA_FORMAT;
            Mp4AudioDsiParser_ReadBits(&bits, 4); /* extensionChannelConfiguration (4) */
        }
	} else {
        self->extension.object_type              = 0;
        self->extension.sampling_frequency       = 0;
        self->extension.sampling_frequency_index = 0;
        self->extension.sbr_present              = false;
        self->extension.ps_present               = false;
    }
    
	switch (self->object_type) {
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_MAIN:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_LC:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_SSR:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_LTP:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_SCALABLE:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_TWINVQ:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LC:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LTP:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_SCALABLE:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_AAC_LD:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_TWINVQ:
        case MP4_MPEG4_AUDIO_OBJECT_TYPE_ER_BSAC:
            result = Mp4AudioDecoderConfig_ParseGASpecificInfo(self, &bits);
            if (result == BLT_SUCCESS) {
                if (self->extension.object_type !=  MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR &&
                    Mp4AudioDsiParser_BitsLeft(&bits) >= 16) {
                    result = Mp4AudioDecoderConfig_ParseExtension(self, &bits);
                }
            }
            if (result != BLT_SUCCESS) return result;
            break;

        default:
            return BLT_ERROR_NOT_SUPPORTED;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_BufferCallback
+---------------------------------------------------------------------*/
static void
OsxAudioQueueOutput_BufferCallback(void*               _self, 
                                   AudioQueueRef       queue, 
                                   AudioQueueBufferRef buffer)
{
    OsxAudioQueueOutput* self = (OsxAudioQueueOutput*)_self;
    BLT_COMPILER_UNUSED(queue);
    
    /* mark the buffer as free */
    pthread_mutex_lock(&self->lock);
    buffer->mUserData = NULL;
    buffer->mAudioDataByteSize = 0;
    pthread_cond_signal(&self->buffer_released_cond);
    pthread_mutex_unlock(&self->lock);
    
    ATX_LOG_FINER_1("callback for buffer %p", buffer);
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_PropertyCallback
+---------------------------------------------------------------------*/
static void 
OsxAudioQueueOutput_PropertyCallback(void*                _self, 
                                     AudioQueueRef        queue, 
                                     AudioQueuePropertyID property_id)
{
    OsxAudioQueueOutput* self = (OsxAudioQueueOutput*)_self;
    UInt32               is_running = false;
    UInt32               property_size = sizeof(UInt32);
    OSStatus             status;
    
    BLT_COMPILER_UNUSED(property_id);
    
    status = AudioQueueGetProperty(queue, kAudioQueueProperty_IsRunning, &is_running, &property_size);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueGetProperty failed (%x)", status);
        return;
    }
    ATX_LOG_FINE_1("is_running property = %d", is_running);
    
    if (!is_running) {
        pthread_mutex_lock(&self->lock);
        if (self->waiting_for_stop) {
            self->waiting_for_stop = BLT_FALSE;
            pthread_cond_signal(&self->audio_queue_stopped_cond);
        }
        pthread_mutex_unlock(&self->lock);
    }
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_ConvertFormat
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_ConvertFormat(OsxAudioQueueOutput*         self,
                                  const BLT_MediaType*         media_type, 
                                  AudioStreamBasicDescription* audio_format)
{
    if (media_type->id == self->expected_media_types.pcm.base.id) {
        const BLT_PcmMediaType* pcm_type = (const BLT_PcmMediaType*) media_type;
        audio_format->mFormatID          = kAudioFormatLinearPCM;
        audio_format->mFormatFlags       = kAudioFormatFlagIsPacked;
        audio_format->mFramesPerPacket   = 1;
        audio_format->mSampleRate        = pcm_type->sample_rate;
        audio_format->mChannelsPerFrame  = pcm_type->channel_count;
        audio_format->mBitsPerChannel    = pcm_type->bits_per_sample;
        audio_format->mBytesPerFrame     = (audio_format->mBitsPerChannel * audio_format->mChannelsPerFrame) / 8;
        audio_format->mBytesPerPacket    = audio_format->mBytesPerFrame * audio_format->mFramesPerPacket;
        audio_format->mReserved          = 0;
        
        /* select the sample format */
        switch (pcm_type->sample_format) {
            case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE:
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
                break;
            
            case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE:
                break;
                
            case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE:
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
                break;
                
            case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE:
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
                break;
                
            case BLT_PCM_SAMPLE_FORMAT_FLOAT_BE:
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsFloat;
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
                break;
                
            case BLT_PCM_SAMPLE_FORMAT_FLOAT_LE:
                audio_format->mFormatFlags |= kLinearPCMFormatFlagIsFloat;
                break;
                
            default:
                return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
    } else if (media_type->id == self->expected_media_types.asbd.base.id) {
        const AsbdMediaType* asbd_type = (const AsbdMediaType*)media_type;
        *audio_format = asbd_type->asbd;
    } else if (media_type->id == self->expected_media_types.mp4.base.base.id) {
        const BLT_Mp4AudioMediaType* mp4_type = (const BLT_Mp4AudioMediaType*)media_type;
        ATX_SetMemory(audio_format, 0, sizeof(*audio_format));
        
        /* check that we support this audio codec */
        if (mp4_type->base.stream_type != BLT_MP4_STREAM_TYPE_AUDIO) {
            return BLT_ERROR_INVALID_MEDIA_TYPE;
        }

        /* setup defaults */
        audio_format->mSampleRate       = mp4_type->sample_rate;
        audio_format->mChannelsPerFrame = mp4_type->channel_count;
        audio_format->mFramesPerPacket  = 1024;
        audio_format->mFormatID         = kAudioFormatMPEG4AAC;

        switch (mp4_type->base.format_or_object_type_id) {
            case BLT_AAC_OBJECT_TYPE_ID_MPEG4_AUDIO: {
            case BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_MAIN:
            case BLT_AAC_OBJECT_TYPE_ID_MPEG2_AAC_LC:
                if (mp4_type->decoder_info_length >= 2) {
                    BLT_Result result;
                    Mp4AudioDecoderConfig decoder_config;
                    result = Mp4AudioDecoderConfig_Parse(&decoder_config, 
                                                         mp4_type->decoder_info, 
                                                         mp4_type->decoder_info_length);
                    if (BLT_SUCCEEDED(result)) {
                        ATX_Boolean sbr = ATX_FALSE;
                        ATX_Boolean ps  = ATX_FALSE;

                        audio_format->mSampleRate       = decoder_config.sampling_frequency;
                        audio_format->mChannelsPerFrame = decoder_config.channel_count;

                        if (decoder_config.extension.object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_SBR ||
                            decoder_config.extension.object_type == MP4_MPEG4_AUDIO_OBJECT_TYPE_PS) {
                            sbr = decoder_config.extension.sbr_present;
                            ps  = decoder_config.extension.ps_present;
                            audio_format->mSampleRate = decoder_config.extension.sampling_frequency;
                            if (ps && decoder_config.channel_count == 1) {
                                audio_format->mChannelsPerFrame = 2;
                            }
                            if (sbr) {
                                audio_format->mFramesPerPacket *= 2;
                                if (ps) {
                                    audio_format->mFormatID = kAudioFormatMPEG4AAC_HE_V2;
                                } else {
                                    audio_format->mFormatID = kAudioFormatMPEG4AAC_HE;
                                }
                            }
                        }
                    }
                }
                break;
            }
                
            default:
                return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
    } else {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_UpdateStreamFormat
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_UpdateStreamFormat(OsxAudioQueueOutput* self, 
                                       const BLT_MediaType* media_type)
{
    AudioStreamBasicDescription audio_format;
    BLT_Result                  result;
    OSStatus                    status;
    
    /* convert the media type into an audio format */
    result = OsxAudioQueueOutput_ConvertFormat(self, media_type, &audio_format);
    if (BLT_FAILED(result)) return result;
    
    /* do nothing if the format has not changed */
    if (self->audio_format.mFormatID         == audio_format.mFormatID         &&
        self->audio_format.mFormatFlags      == audio_format.mFormatFlags      &&
        self->audio_format.mFramesPerPacket  == audio_format.mFramesPerPacket  && 
        self->audio_format.mSampleRate       == audio_format.mSampleRate       && 
        self->audio_format.mChannelsPerFrame == audio_format.mChannelsPerFrame && 
        self->audio_format.mBitsPerChannel   == audio_format.mBitsPerChannel   && 
        self->audio_format.mBytesPerFrame    == audio_format.mBytesPerFrame    && 
        self->audio_format.mBytesPerPacket   == audio_format.mBytesPerPacket) {
        return BLT_SUCCESS;
    }
        
    /* reset any existing queue before we create a new one */
    if (self->audio_queue) {
        /* drain any pending packets before we switch */
        OsxAudioQueueOutput_Drain(&ATX_BASE(self, BLT_OutputNode));
        
        /* destroy the queue (this will also free the buffers) */
        AudioQueueDispose(self->audio_queue, true);
        self->audio_queue = NULL;
    }

    /* create an audio queue */
    status = AudioQueueNewOutput(&audio_format, 
                                 OsxAudioQueueOutput_BufferCallback,
                                 self,
                                 NULL,
                                 kCFRunLoopCommonModes,
                                 0,
                                 &self->audio_queue);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueNewOutput returned %x", status);
        self->audio_queue = NULL;
        return BLT_ERROR_UNSUPPORTED_FORMAT;
    }
    
    /* listen for property changes */
    status = AudioQueueAddPropertyListener(self->audio_queue, 
                                           kAudioQueueProperty_IsRunning, 
                                           OsxAudioQueueOutput_PropertyCallback, 
                                           self);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueAddPropertyListener returned %x", status);
        AudioQueueDispose(self->audio_queue, true);
        return BLT_FAILURE;
    }
    
    /* if this is an ASBD type, set the magic cookie and look at the buffer sizes */
    if (media_type->id == self->expected_media_types.asbd.base.id) {
        AsbdMediaType* asbd_media_type = (AsbdMediaType*)(media_type);
        if (asbd_media_type->magic_cookie_size) {
            status = AudioQueueSetProperty(self->audio_queue, 
                                           kAudioQueueProperty_MagicCookie, 
                                           asbd_media_type->magic_cookie, 
                                           asbd_media_type->magic_cookie_size);
            if (status != noErr) return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
    } else if (media_type->id == self->expected_media_types.mp4.base.base.id) {
        const BLT_Mp4AudioMediaType* mp4_type = (const BLT_Mp4AudioMediaType*)media_type;
        unsigned int magic_cookie_size = mp4_type->decoder_info_length+25;
        unsigned char* magic_cookie = ATX_AllocateZeroMemory(magic_cookie_size);
        
        /* construct the content of the magic cookie (the 'ES Descriptor') */
        magic_cookie[ 0] = 0x03;                 /* ES_Descriptor tag */
        magic_cookie[ 1] = magic_cookie_size-2;  /* ES_Descriptor payload size */
        magic_cookie[ 2] = 0;                    /* ES ID */      
        magic_cookie[ 3] = 0;                    /* ES ID */
        magic_cookie[ 4] = 0;                    /* flags */
        magic_cookie[ 5] = 0x04;                 /* DecoderConfig tag */
        magic_cookie[ 6] = magic_cookie_size-10; /* DecoderConfig payload size */
        magic_cookie[ 7] = mp4_type->base.format_or_object_type_id; /* object type */
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

        status = AudioQueueSetProperty(self->audio_queue, 
                                       kAudioQueueProperty_MagicCookie, 
                                       magic_cookie, 
                                       magic_cookie_size);
        if (status != noErr) return BLT_ERROR_INVALID_MEDIA_TYPE;
    } else {
        self->packet_count_max = 0;
    }
    if (audio_format.mFramesPerPacket) {
        self->packet_count_max = 
            (((((UInt64)BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFERS_TOTAL_DURATION * 
                (UInt64)audio_format.mSampleRate)/audio_format.mFramesPerPacket)/1000) +
                BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT/2)/
                BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT;
    } else {
        self->packet_count_max = 0;
    }
    self->packet_count = 0;
    ATX_LOG_FINE_1("max packets per buffer = %d", self->packet_count_max);
    
    /* create the buffers */
    {
        unsigned int i;
        unsigned int buffer_size;
        if (audio_format.mBytesPerFrame && ((UInt32)audio_format.mSampleRate != 0)) {
            buffer_size = (((UInt64)BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFERS_TOTAL_DURATION * 
                            (UInt64)audio_format.mBytesPerFrame*(UInt64)audio_format.mSampleRate)/1000)/
                            BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT;
        } else {
            buffer_size = BLT_OSX_AUDIO_QUEUE_OUTPUT_DEFAULT_BUFFER_SIZE;
        }
        ATX_LOG_FINE_1("buffer size = %d", buffer_size);
        for (i=0; i<BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT; i++) {
            AudioQueueAllocateBuffer(self->audio_queue, buffer_size, &self->buffers[i].data);                
            self->buffers[i].data->mUserData = NULL;
            self->buffers[i].data->mAudioDataByteSize = 0;
            self->buffers[i].timestamp = 0;
            self->buffers[i].duration = 0;
        }
    }
            
    /* copy the format */
    self->audio_format = audio_format;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_WaitForCondition
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_WaitForCondition(OsxAudioQueueOutput* self, pthread_cond_t* condition)
{
    struct timespec timeout;
    struct timeval  now;
    gettimeofday(&now, NULL);
    timeout.tv_sec  = now.tv_sec+BLT_OSX_AUDIO_QUEUE_OUTPUT_MAX_WAIT;
    timeout.tv_nsec = now.tv_usec*1000;
    ATX_LOG_FINE("waiting for buffer...");
    int result = pthread_cond_timedwait(condition, &self->lock, &timeout);
    return result==0?BLT_SUCCESS:ATX_ERROR_TIMEOUT;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_WaitForBuffer
+---------------------------------------------------------------------*/
static OsxAudioQueueBuffer*
OsxAudioQueueOutput_WaitForBuffer(OsxAudioQueueOutput* self)
{
    OsxAudioQueueBuffer* buffer = &self->buffers[self->buffer_index];
    
    /* check that we have a queue and buffers */
    if (self->audio_queue == NULL) return NULL;
    
    /* wait for the next buffer to be released */
    pthread_mutex_lock(&self->lock);
    while (buffer->data->mUserData) {
        /* the buffer is locked, wait for it to be released */
        BLT_Result result = OsxAudioQueueOutput_WaitForCondition(self, &self->buffer_released_cond);
        if (BLT_FAILED(result)) {
            ATX_LOG_WARNING("timeout while waiting for buffer");
            buffer = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&self->lock);
    
    return buffer;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Flush
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_Flush(OsxAudioQueueOutput* self)
{
    unsigned int i;
    
    /* check that we have a queue and buffers */
    if (self->audio_queue == NULL) return BLT_SUCCESS;

    /* reset the buffers */
    pthread_mutex_lock(&self->lock);
    for (i=0; i<BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT; i++) {
        self->buffers[i].data->mAudioDataByteSize = 0;
        self->buffers[i].data->mUserData = NULL;
    }
    pthread_mutex_unlock(&self->lock);
    self->packet_count = 0;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_EnqueueBuffer
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_EnqueueBuffer(OsxAudioQueueOutput* self)
{
    OSStatus             status;
    unsigned int         buffer_index = self->buffer_index;
    OsxAudioQueueBuffer* buffer = &self->buffers[self->buffer_index];
    AudioTimeStamp       start_time;
    AudioTimeStamp       current_time;
    unsigned int         packet_count = self->packet_count;
     
    /* reset the packet count */
    self->packet_count = 0;

    /* check that the buffer has data */
    if (buffer->data->mAudioDataByteSize == 0) return BLT_SUCCESS;
    
    /* mark this buffer as 'in queue' and move on to the next buffer */
    buffer->data->mUserData = self;
    self->buffer_index++;
    if (self->buffer_index == BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT) {
        self->buffer_index = 0;
    }
    
    /* queue the buffer */
    if (packet_count) {
        ATX_LOG_FINE_2("enqueuing buffer %d, %d packets", buffer_index, packet_count);
    } else {
        ATX_LOG_FINE_2("enqueuing buffer %d, %d bytes", buffer_index, buffer->data?buffer->data->mAudioDataByteSize:0);
    }
    status = AudioQueueEnqueueBufferWithParameters(self->audio_queue, 
                                                   buffer->data, 
                                                   packet_count, 
                                                   packet_count?self->packet_descriptions:NULL, 
                                                   0, 
                                                   0, 
                                                   0, 
                                                   NULL, 
                                                   NULL, 
                                                   &start_time);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueEnqueueBuffer returned %x", status);
        buffer->data->mUserData = NULL;
        buffer->data->mAudioDataByteSize = 0;
        return BLT_FAILURE;
    }
    if (start_time.mFlags & kAudioTimeStampSampleTimeValid) {
        ATX_LOG_FINER_1("start_time = %lf", start_time.mSampleTime);
    } else {
        ATX_LOG_FINER("start_time not available");
        start_time.mSampleTime = 0;
    }
    if (start_time.mFlags & kAudioTimeStampHostTimeValid) {
        self->timestamp_snapshot.host_time = start_time.mHostTime;
    } else {
        self->timestamp_snapshot.host_time = 0;
    }

    /* automatically start the queue if it is not already running */
    if (!self->audio_queue_started) {
        ATX_LOG_FINE("auto-starting the queue");
        OSStatus status = AudioQueueStart(self->audio_queue, NULL);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueueStart failed (%x)", status);
            return BLT_ERROR_INTERNAL;
        } 
        self->audio_queue_started = BLT_TRUE;
    }
    
    /* check the current queue time to detect underflows */
    status = AudioQueueGetCurrentTime(self->audio_queue, NULL, &current_time, NULL);
    if (status == noErr) {
        ATX_LOG_FINER_1("current time = %lf", current_time.mSampleTime);
        if ((current_time.mFlags & kAudioTimeStampSampleTimeValid) && 
            (start_time.mFlags & kAudioTimeStampSampleTimeValid)   &&
            (start_time.mSampleTime != 0)) {
            if (current_time.mSampleTime > start_time.mSampleTime) {
                ATX_LOG_FINE_1("current time is past queued sample time (%f)", (float)(current_time.mSampleTime - start_time.mSampleTime));
            }
            if (current_time.mSampleTime > start_time.mSampleTime+BLT_OSX_AUDIO_QUEUE_UNDERFLOW_THRESHOLD) {
                /* stop the queue (it will restart automatically on the next call) */
                ATX_LOG_FINE("audio queue underflow");
                AudioQueueStop(self->audio_queue, TRUE);
                self->audio_queue_started = BLT_FALSE;
            }
        }
    } else {
        ATX_LOG_FINER_1("no timestamp available (%d)", status);
    }
    
    /* remember the timestamps */
    self->timestamp_snapshot.packet_time = buffer->timestamp;
    self->timestamp_snapshot.max_time    = buffer->timestamp + buffer->duration;
        
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_PutPacket(BLT_PacketConsumer* _self,
                              BLT_MediaPacket*    packet)
{
    OsxAudioQueueOutput* self = ATX_SELF(OsxAudioQueueOutput, BLT_PacketConsumer);
    const BLT_MediaType* media_type;
    BLT_Result           result;

    /* check parameters */
    if (packet == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (BLT_FAILED(result)) return result;

    /* update the media type */
    result = OsxAudioQueueOutput_UpdateStreamFormat(self, media_type);
    if (BLT_FAILED(result)) return result;

    /* exit early if the packet is empty */
    if (BLT_MediaPacket_GetPayloadSize(packet) == 0) return BLT_SUCCESS;
        
    /* ensure we're not paused */
    OsxAudioQueueOutput_Resume(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));

    /* queue the packet */
    {
        const unsigned char* payload = (const unsigned char*)BLT_MediaPacket_GetPayloadBuffer(packet);
        BLT_Size             payload_size = BLT_MediaPacket_GetPayloadSize(packet);
        unsigned int         buffer_fullness = 0;
        
        /* wait for a buffer */
        OsxAudioQueueBuffer* buffer = OsxAudioQueueOutput_WaitForBuffer(self);
        if (buffer == NULL) return BLT_ERROR_INTERNAL;
            
        /* check if there is enough space in the buffer */
        if (buffer->data->mAudioDataBytesCapacity-buffer->data->mAudioDataByteSize < payload_size) {
            /* not enough space, enqueue this buffer and wait for the next one */
            ATX_LOG_FINER("buffer full");
            OsxAudioQueueOutput_EnqueueBuffer(self);
            buffer = OsxAudioQueueOutput_WaitForBuffer(self);
            if (buffer == NULL) return BLT_ERROR_INTERNAL;
        }
        
        /* we should always have enough space at this point (unless the buffers are too small) */
        if (buffer->data->mAudioDataBytesCapacity-buffer->data->mAudioDataByteSize < payload_size) {
            ATX_LOG_WARNING_1("buffer too small! (%d needed)", payload_size);
            return BLT_ERROR_INTERNAL;
        }
        
        /* copy the data into the buffer */
        buffer_fullness = buffer->data->mAudioDataByteSize;
        ATX_CopyMemory((unsigned char*)buffer->data->mAudioData+buffer->data->mAudioDataByteSize,
                       payload,
                       payload_size);
                       
        /* set the buffer timestamp if needed */
        if (buffer_fullness == 0) {
            buffer->timestamp = BLT_TimeStamp_ToNanos(BLT_MediaPacket_GetTimeStamp(packet));
            buffer->duration  = 0;
        }

        /* adjust the buffer fullness */
        buffer->data->mAudioDataByteSize += payload_size;
        
        /* for compressed types, update the packet descriptions */
        if (media_type->id == self->expected_media_types.asbd.base.id ||
            media_type->id == self->expected_media_types.mp4.base.base.id) {
            AudioStreamPacketDescription* packet_description = &self->packet_descriptions[self->packet_count++];
            packet_description->mDataByteSize           = payload_size;
            packet_description->mStartOffset            = buffer_fullness;
            packet_description->mVariableFramesInPacket = self->audio_format.mFramesPerPacket;
            
            /* enqueue now if we've used all the packet descriptions or there's enough in the buffer */
            if (self->packet_count == BLT_OSX_AUDIO_QUEUE_OUTPUT_PACKET_DESCRIPTION_COUNT ||
                (self->packet_count_max && self->packet_count >= self->packet_count_max)) {
                ATX_LOG_FINE_1("reached max packets in buffer (%d), enqueuing", self->packet_count);
                OsxAudioQueueOutput_EnqueueBuffer(self);
            }
        }        
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_QueryMediaType(BLT_MediaPort*        _self,
                                   BLT_Ordinal           index,
                                   const BLT_MediaType** media_type)
{
    OsxAudioQueueOutput* self = ATX_SELF(OsxAudioQueueOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_types.pcm;
        return BLT_SUCCESS;
    } else if (index == 1) {
        *media_type = (const BLT_MediaType*)&self->expected_media_types.asbd;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_Create(BLT_Module*              _module,
                           BLT_Core*                core, 
                           BLT_ModuleParametersType parameters_type,
                           BLT_CString              parameters, 
                           BLT_MediaNode**          object)
{
    OsxAudioQueueOutput*       self;
    OsxAudioQueueOutputModule* module = (OsxAudioQueueOutputModule*)_module;
    
    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(OsxAudioQueueOutput));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), _module, core);

    /* create a lock and conditions */
    pthread_mutex_init(&self->lock, NULL);
    pthread_cond_init(&self->buffer_released_cond, NULL);
    pthread_cond_init(&self->audio_queue_stopped_cond, NULL);
    
    /* setup the expected media types */
    BLT_PcmMediaType_Init(&self->expected_media_types.pcm);
    self->expected_media_types.pcm.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    BLT_MediaType_Init(&self->expected_media_types.asbd.base, module->asbd_media_type_id);
    BLT_MediaType_Init(&self->expected_media_types.mp4.base.base, module->mp4_es_media_type_id);
    self->expected_media_types.mp4.base.stream_type = BLT_MP4_STREAM_TYPE_AUDIO;
    
    /* initialize fields */
    self->volume = 1.0;
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, OsxAudioQueueOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (self, OsxAudioQueueOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE   (self, OsxAudioQueueOutput, BLT_OutputNode);
    ATX_SET_INTERFACE   (self, OsxAudioQueueOutput, BLT_VolumeControl);
    ATX_SET_INTERFACE   (self, OsxAudioQueueOutput, BLT_MediaPort);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_Destroy(OsxAudioQueueOutput* self)
{
    /* drain the queue */
    OsxAudioQueueOutput_Drain(&ATX_BASE(self, BLT_OutputNode));

    /* stop the audio pump */
    OsxAudioQueueOutput_Stop(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));
    
    /* close the audio queue */
    if (self->audio_queue) {
        AudioQueueDispose(self->audio_queue, true);
    }
        
    /* destroy the lock and conditions */
    pthread_cond_destroy(&self->buffer_released_cond);
    pthread_cond_destroy(&self->audio_queue_stopped_cond);
    pthread_mutex_destroy(&self->lock);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|   OsxAudioQueueOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_GetPortByName(BLT_MediaNode*  _self,
                                  BLT_CString     name,
                                  BLT_MediaPort** port)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Seek(BLT_MediaNode* _self,
                         BLT_SeekMode*  mode,
                         BLT_SeekPoint* point)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);
    
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* return now if no queue has been created */
    if (self->audio_queue == NULL) return BLT_SUCCESS;
    
    /* reset the queue */
    AudioQueueReset(self->audio_queue);

    /* flush any pending buffer */
    OsxAudioQueueOutput_Flush(self);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_GetStatus(BLT_OutputNode*       _self,
                              BLT_OutputNodeStatus* status)
{
    OsxAudioQueueOutput* self = ATX_SELF(OsxAudioQueueOutput, BLT_OutputNode);

    /* default value */
    status->media_time.seconds     = 0;
    status->media_time.nanoseconds = 0;
    status->flags = 0;
    
    /* compute the media time */
    if (self->timestamp_snapshot.host_time) {
        /* Get the mach timebase info */
        mach_timebase_info_data_t timebase;

        /* convert to nanoseconds */
        int64_t now  = (int64_t)mach_absolute_time();
        int64_t delta = (int64_t)self->timestamp_snapshot.host_time-now;
        mach_timebase_info(&timebase);
        delta *= timebase.numer;
        delta /= timebase.denom;
        
        if (delta < 0) {
            delta = 0;
        }
        if (self->timestamp_snapshot.packet_time > delta) {
            SInt64 ts = (SInt64)self->timestamp_snapshot.packet_time-delta;
            //if (ts > self->timestamp_snapshot.max_time) {
            //    ATX_LOG_FINER("clamping timestamp");
            //    ts = self->timestamp_snapshot.max_time;
            //}
            status->media_time = BLT_TimeStamp_FromNanos(ts);
            ATX_LOG_FINER_2("delta = %d, ts = %lld", (int)delta, ts);
        } else {
            ATX_LOG_FINER_1("delta too large (%d)", (int)delta);
        }
    }
    
    /* check if we're full */
    if (self->audio_queue) {
        pthread_mutex_lock(&self->lock);
        if (self->buffers[self->buffer_index].data->mUserData) {
            /* buffer is busy */
            status->flags |= BLT_OUTPUT_NODE_STATUS_QUEUE_FULL;
        }
        pthread_mutex_unlock(&self->lock);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Drain
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Drain(BLT_OutputNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF(OsxAudioQueueOutput, BLT_OutputNode);
    OSStatus             status;
    
    /* if we're paused, there's nothing to drain */
    if (self->audio_queue_paused) return BLT_SUCCESS;
    
    /* in case we have a pending buffer, enqueue it now */
    OsxAudioQueueBuffer* buffer = OsxAudioQueueOutput_WaitForBuffer(self);
    if (buffer && buffer->data->mAudioDataByteSize) OsxAudioQueueOutput_EnqueueBuffer(self);
    
    /* flush anything that may be in the queue */
    ATX_LOG_FINE("flusing queued buffers");
    status = AudioQueueFlush(self->audio_queue);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueFlush failed (%x)", status);
    }
    
    /* if the queue is not started, we're done */
    if (!self->audio_queue_started) return BLT_SUCCESS;
    
    /* wait for the queue to be stopped */
    pthread_mutex_lock(&self->lock);
    ATX_LOG_FINE("stopping audio queue (async)");
    status = AudioQueueStop(self->audio_queue, false);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueStop failed (%x)", status);
    }
    self->waiting_for_stop = BLT_TRUE;
    OsxAudioQueueOutput_WaitForCondition(self, &self->audio_queue_stopped_cond);
    pthread_mutex_unlock(&self->lock);

    /* we're really stopped now */
    self->audio_queue_started = BLT_FALSE;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Start(BLT_MediaNode* _self)
{
    /* do nothing here, because the queue is auto-started when packets arrive */
    BLT_COMPILER_UNUSED(_self);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Stop(BLT_MediaNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (self->audio_queue == NULL) return BLT_SUCCESS;

    /* stop the audio queue */
    ATX_LOG_FINE("stopping audio queue (sync)");
    OSStatus status = AudioQueueStop(self->audio_queue, true);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueStop failed (%x)", status);
    }
    self->audio_queue_started = BLT_FALSE;

    /* flush any pending buffer */
    OsxAudioQueueOutput_Flush(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Pause(BLT_MediaNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);
    
    if (self->audio_queue && !self->audio_queue_paused) {
        OSStatus status = AudioQueuePause(self->audio_queue);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueuePause failed (%x)", status);
        }
        self->audio_queue_paused = BLT_TRUE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Resume(BLT_MediaNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (self->audio_queue && self->audio_queue_paused) {
        ATX_LOG_FINE("resuming from pause: starting audio queue");
        OSStatus status = AudioQueueStart(self->audio_queue, NULL);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueueStart failed (%x)", status);
        }
        self->audio_queue_paused = BLT_FALSE;
        self->audio_queue_started = BLT_TRUE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_SetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_SetVolume(BLT_VolumeControl* _self, float volume)
{
    OsxAudioQueueOutput* self = ATX_SELF(OsxAudioQueueOutput, BLT_VolumeControl);
    
    self->volume = volume;
    if (self->audio_queue) {
        OSStatus status = AudioQueueSetParameter(self->audio_queue, kAudioQueueParam_Volume, volume);
        if (status != noErr) return BLT_FAILURE;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_GetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_GetVolume(BLT_VolumeControl* _self, float* volume)
{
    OsxAudioQueueOutput* self = ATX_SELF(OsxAudioQueueOutput, BLT_VolumeControl);

    if (self->audio_queue) {
        OSStatus status = AudioQueueGetParameter(self->audio_queue, kAudioQueueParam_Volume, &self->volume);
        if (status == noErr) {
            *volume = self->volume;
            return BLT_SUCCESS;
        } else {
            *volume = 1.0f;
            return BLT_FAILURE;
        }
    } else {
        *volume = self->volume;
        return BLT_SUCCESS;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioQueueOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (OsxAudioQueueOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT   (OsxAudioQueueOutput, BLT_VolumeControl)
    ATX_GET_INTERFACE_ACCEPT   (OsxAudioQueueOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT   (OsxAudioQueueOutput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(OsxAudioQueueOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(OsxAudioQueueOutput, BLT_MediaPort)
    OsxAudioQueueOutput_GetName,
    OsxAudioQueueOutput_GetProtocol,
    OsxAudioQueueOutput_GetDirection,
    OsxAudioQueueOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxAudioQueueOutput, BLT_PacketConsumer)
    OsxAudioQueueOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    OsxAudioQueueOutput_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    OsxAudioQueueOutput_Start,
    OsxAudioQueueOutput_Stop,
    OsxAudioQueueOutput_Pause,
    OsxAudioQueueOutput_Resume,
    OsxAudioQueueOutput_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxAudioQueueOutput, BLT_OutputNode)
    OsxAudioQueueOutput_GetStatus,
    OsxAudioQueueOutput_Drain
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_VolumeControl interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxAudioQueueOutput, BLT_VolumeControl)
    OsxAudioQueueOutput_GetVolume,
    OsxAudioQueueOutput_SetVolume
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxAudioQueueOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   OsxAudioQueueOutputModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutputModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    OsxAudioQueueOutputModule* self = ATX_SELF_EX(OsxAudioQueueOutputModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*              registry;
    BLT_Result                 result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the audio/x-apple-asbd type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-apple-asbd",
        &self->asbd_media_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("audio/x-apple-asbd type = %d", self->asbd_media_type_id);

    /* register the AAC elementary stream type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_MP4_AUDIO_ES_MIME_TYPE,
        &self->mp4_es_media_type_id);
    if (BLT_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   OsxAudioQueueOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutputModule_Probe(BLT_Module*              _self, 
                                BLT_Core*                core,
                                BLT_ModuleParametersType parameters_type,
                                BLT_AnyConst             parameters,
                                BLT_Cardinal*            match)
{
    OsxAudioQueueOutputModule* self = (OsxAudioQueueOutputModule*)_self;
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be PACKET and the */
            /* output protocol should be NONE              */
            if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* the input type should be unknown, or audio/pcm */
            if (!(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.input.media_type->id == self->asbd_media_type_id) &&
                !(constructor->spec.input.media_type->id == self->mp4_es_media_type_id) &&
                !(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* the name should be 'osxaq:<n>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "osxaq:", 6)) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioQueueOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioQueueOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioQueueOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(OsxAudioQueueOutputModule, OsxAudioQueueOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(OsxAudioQueueOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    OsxAudioQueueOutputModule_Attach,
    OsxAudioQueueOutputModule_CreateInstance,
    OsxAudioQueueOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define OsxAudioQueueOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxAudioQueueOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(OsxAudioQueueOutputModule,
                                         "OSX Audio Queue Output",
                                         "com.axiosys.output.osx-audio-queue",
                                         "1.2.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
#else 
/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_OsxAudioQueueOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;
    *object = NULL;
    return BLT_ERROR_NOT_SUPPORTED;
}
#endif

