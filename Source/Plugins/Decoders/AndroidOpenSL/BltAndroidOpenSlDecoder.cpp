/*****************************************************************
|
|   BlueTune - Android OpenSL ES Decoder Module
|
|   (c) 2002-2014 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <pthread.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltAndroidOpenSlDecoder.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.android.opensl")

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
const unsigned int BLT_AAC_DECODER_OBJECT_TYPE_MPEG2_AAC_LC = 0x67;
const unsigned int BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO  = 0x40;
const unsigned int BLT_AAC_DECODER_FRAME_SIZE               = 1024;

const unsigned int BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT         = 2;
const unsigned int BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_COUNT          = 2;
const unsigned int BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE           = 2*sizeof(short)*BLT_AAC_DECODER_FRAME_SIZE;
const unsigned int BLT_ANDROID_OPENSL_DECODER_BUFFER_QUEUE_WAIT_TIME    = 20000000; /* 20 ms */
const unsigned int BLT_ANDROID_OPENSL_DECODER_BUFFER_QUEUE_WATCHDOG_MAX = 100;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 mp4es_type_id;
    BLT_UInt32 probe_score;
} AndroidOpenSlDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    NPT_DataBuffer*        buffers[BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT];
    pthread_mutex_t        buffers_lock;
    pthread_cond_t         buffers_available_condition;
    BLT_Mp4AudioMediaType* media_type;
} AndroidOpenSlDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_PcmMediaType media_type;
    int8_t           pcm_buffer[BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE*BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_COUNT];
    unsigned int     pcm_buffer_index;
    ATX_List*        packets;
    pthread_mutex_t  packets_lock;
} AndroidOpenSlDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    AndroidOpenSlDecoderInput  input;
    AndroidOpenSlDecoderOutput output;
    volatile ATX_Boolean       eos;
    volatile ATX_Boolean       flushing;
    BLT_UInt32                 mp4es_type_id;

    SLObjectItf                   sl_engine_object;
    SLEngineItf                   sl_engine;
    SLObjectItf                   sl_player;
    SLPlayItf                     sl_play_interface;
    SLMetadataExtractionItf       sl_metadata_interface;
    SLAndroidSimpleBufferQueueItf sl_output_buffer_queue_interface;
    SLAndroidBufferQueueItf       sl_input_buffer_queue_interface;    
    bool                          sl_player_started;

    struct {
        unsigned int channel_count_key_index;
        unsigned int sample_rate_key_index;
        unsigned int bits_per_sample_key_index;
        unsigned int container_size_key_index;
        unsigned int channel_mask_key_index;
        unsigned int endianness_key_index;
    } sl_metadata_info;

    struct {
        unsigned int leading_frames;
        unsigned int trailing_frames;
        ATX_UInt64   valid_frames;
    } gapless_info;
} AndroidOpenSlDecoder;

/*----------------------------------------------------------------------
 |   GetSamplingFrequencyIndex
 +---------------------------------------------------------------------*/
static unsigned int
GetSamplingFrequencyIndex(unsigned int sampling_frequency)
{
    switch (sampling_frequency) {
        case 96000: return 0;
        case 88200: return 1;
        case 64000: return 2;
        case 48000: return 3;
        case 44100: return 4;
        case 32000: return 5;
        case 24000: return 6;
        case 22050: return 7;
        case 16000: return 8;
        case 12000: return 9;
        case 11025: return 10;
        case 8000:  return 11;
        case 7350:  return 12;
        default:    return 0;
    }
}

/*----------------------------------------------------------------------
|   MakeAdtsHeader
+---------------------------------------------------------------------*/
static void
MakeAdtsHeader(unsigned char* header, 
               unsigned int   frame_size,
               unsigned int   sampling_frequency_index,
               unsigned int   channel_configuration)
{
    header[0] = 0xFF;
    header[1] = 0xF1; // 0xF9 (MPEG2)
    header[2] = 0x40 | (sampling_frequency_index << 2) | (channel_configuration >> 2);
    header[3] = ((channel_configuration&0x3)<<6) | ((frame_size+7) >> 11);
    header[4] = ((frame_size+7) >> 3)&0xFF;
    header[5] = (((frame_size+7) << 5)&0xFF) | 0x1F;
    header[6] = 0xFC;

    /*
        0:  syncword 12 always: '111111111111' 
        12: ID 1 0: MPEG-4, 1: MPEG-2 
        13: layer 2 always: '00' 
        15: protection_absent 1  
        16: profile 2  
        18: sampling_frequency_index 4  
        22: private_bit 1  
        23: channel_configuration 3  
        26: original/copy 1  
        27: home 1  
        28: emphasis 2 only if ID == 0 

        ADTS Variable header: these can change from frame to frame 
        28: copyright_identification_bit 1  
        29: copyright_identification_start 1  
        30: aac_frame_length 13 length of the frame including header (in bytes) 
        43: adts_buffer_fullness 11 0x7FF indicates VBR 
        54: no_raw_data_blocks_in_frame 2  
        ADTS Error check 
        crc_check 16 only if protection_absent == 0 
   */
}

/*----------------------------------------------------------------------
|   AndroidOpenSlDecoder_Flush
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOpenSlDecoder_Flush(AndroidOpenSlDecoder* self)
{
    pthread_mutex_lock(&self->input.buffers_lock);
    self->flushing = ATX_TRUE;
    pthread_cond_signal(&self->input.buffers_available_condition);
    pthread_mutex_unlock(&self->input.buffers_lock);

    self->sl_player_started = false;
    (*self->sl_play_interface)->SetPlayState(self->sl_play_interface, SL_PLAYSTATE_STOPPED);
    (*self->sl_input_buffer_queue_interface)->Clear(self->sl_input_buffer_queue_interface);
    (*self->sl_output_buffer_queue_interface)->Clear(self->sl_output_buffer_queue_interface);

    for (unsigned int i=0; i<BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT; i++) {
        delete self->input.buffers[i];
        self->input.buffers[i] = NULL;
    }

    pthread_mutex_lock(&self->output.packets_lock);
    ATX_ListItem* item;
    while ((item = ATX_List_GetFirstItem(self->output.packets))) {
        BLT_MediaPacket* packet = (BLT_MediaPacket*)ATX_ListItem_GetData(item);
        if (packet) BLT_MediaPacket_Release(packet);
        ATX_List_RemoveItem(self->output.packets, item);
    }
    pthread_mutex_unlock(&self->output.packets_lock);

    self->flushing = ATX_FALSE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AndroidOpenSlDecoderInput_GetFreeBufferIndex
+---------------------------------------------------------------------*/
static int
AndroidOpenSlDecoderInput_GetFreeBufferIndex(AndroidOpenSlDecoderInput& input)
{
    int result = -1;

    pthread_mutex_lock(&input.buffers_lock);

    for (unsigned int i=0; i<BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT; i++) {
        if (input.buffers[i] == NULL) {
            result = (int)i;
            break;
        }
    }

    pthread_mutex_unlock(&input.buffers_lock);

    return result;
}

/*----------------------------------------------------------------------
|   AndroidOpenSlDecoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoderInput_PutPacket(BLT_PacketConsumer* _self,
                                    BLT_MediaPacket*    packet)
{
    AndroidOpenSlDecoder* self = ATX_SELF_M(input, AndroidOpenSlDecoder, BLT_PacketConsumer);

    // check the packet type
    const BLT_MediaType* media_type = NULL;
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (media_type->id != self->mp4es_type_id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    const BLT_Mp4AudioMediaType* mp4_media_type = (BLT_Mp4AudioMediaType*)media_type;
    if (mp4_media_type->base.stream_type != BLT_MP4_STREAM_TYPE_AUDIO) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    if (mp4_media_type->base.format_or_object_type_id != BLT_AAC_DECODER_OBJECT_TYPE_MPEG2_AAC_LC &&
        mp4_media_type->base.format_or_object_type_id != BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }    

    // wait for some buffer space to be available
    int buffer_index = -1;
    for (unsigned int watchdog=0; watchdog<BLT_ANDROID_OPENSL_DECODER_BUFFER_QUEUE_WATCHDOG_MAX; watchdog++) {
        buffer_index = AndroidOpenSlDecoderInput_GetFreeBufferIndex(self->input);
        if (buffer_index < 0) {
            ATX_LOG_FINEST_1("no ADTS buffer available, waiting for one to free up (%u)", watchdog);

            ATX_TimeInterval wait;
            wait.seconds     = 0;
            wait.nanoseconds = BLT_ANDROID_OPENSL_DECODER_BUFFER_QUEUE_WAIT_TIME;
            ATX_System_Sleep(&wait);            
        }
    }
    if (buffer_index < 0) {
        ATX_LOG_WARNING("the buffer wait watchdog bit us");
        SLuint32 play_state = 0;
        (*self->sl_play_interface)->GetPlayState(self->sl_play_interface, &play_state);
        ATX_LOG_WARNING_1("play state = %d", play_state);
        AndroidOpenSlDecoder_Flush(self);
        buffer_index = AndroidOpenSlDecoderInput_GetFreeBufferIndex(self->input);
        if (buffer_index < 0) {
            ATX_LOG_WARNING("no ADTS buffer free after flushing?");
            return BLT_ERROR_INTERNAL;
        }
    }

    pthread_mutex_lock(&self->input.buffers_lock);

    // queue a new packet with an ADTS header
    unsigned int packet_size = BLT_MediaPacket_GetPayloadSize(packet);
    self->input.buffers[buffer_index] = new NPT_DataBuffer(7+packet_size);
    self->input.buffers[buffer_index]->SetDataSize(7+packet_size);
    NPT_CopyMemory(self->input.buffers[buffer_index]->UseData()+7, BLT_MediaPacket_GetPayloadBuffer(packet), packet_size);
    MakeAdtsHeader(self->input.buffers[buffer_index]->UseData(), 
                   packet_size, 
                   GetSamplingFrequencyIndex(mp4_media_type->sample_rate),
                   mp4_media_type->channel_count);
    pthread_cond_signal(&self->input.buffers_available_condition);

    ATX_LOG_FINE_2("enqueing ADTS buffer %d, size=%u", buffer_index, self->input.buffers[buffer_index]->GetDataSize());
    SLresult result = (*self->sl_input_buffer_queue_interface)->Enqueue(
        self->sl_input_buffer_queue_interface, 
        NULL,  /* pBufferContext */
        self->input.buffers[buffer_index]->UseData(), 
        self->input.buffers[buffer_index]->GetDataSize(), 
        NULL, 
        0);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("Enqueue failed (%d)", result);
        pthread_mutex_unlock(&self->input.buffers_lock);
        return BLT_FAILURE;
    }

    pthread_mutex_unlock(&self->input.buffers_lock);

    /* start the player if needed */
    if (!self->sl_player_started) {
        ATX_LOG_FINE("starting the player");
        SLresult result = (*self->sl_play_interface)->SetPlayState(self->sl_play_interface, SL_PLAYSTATE_PLAYING);
        if (result == SL_RESULT_SUCCESS) {
            self->sl_player_started = true;
        } else {
            ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
            return BLT_FAILURE;
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AndroidOpenSlDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(AndroidOpenSlDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AndroidOpenSlDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AndroidOpenSlDecoderInput, BLT_PacketConsumer)
    AndroidOpenSlDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AndroidOpenSlDecoderInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(AndroidOpenSlDecoderInput, BLT_MediaPort)
    AndroidOpenSlDecoderInput_GetName,
    AndroidOpenSlDecoderInput_GetProtocol,
    AndroidOpenSlDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   AndroidOpenSlDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                                     BLT_MediaPacket**   packet)
{
    AndroidOpenSlDecoder* self = ATX_SELF_M(output, AndroidOpenSlDecoder, BLT_PacketProducer);

    // default return
    *packet = NULL;
    
    /* check if we have a packet available */
    pthread_mutex_lock(&self->output.packets_lock);
    ATX_ListItem* packet_item = ATX_List_GetFirstItem(self->output.packets);
    if (packet_item) {
        *packet = (BLT_MediaPacket*)ATX_ListItem_GetData(packet_item);
        ATX_List_RemoveItem(self->output.packets, packet_item);
    }
    pthread_mutex_unlock(&self->output.packets_lock);

    if (*packet) {
        ATX_LOG_FINER_1("returning PCM packet, size=%u", BLT_MediaPacket_GetPayloadSize(*packet));
        return BLT_SUCCESS;
    } else {
        ATX_LOG_FINER("no packet available");
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AndroidOpenSlDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(AndroidOpenSlDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AndroidOpenSlDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AndroidOpenSlDecoderOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(AndroidOpenSlDecoderOutput, BLT_MediaPort)
    AndroidOpenSlDecoderOutput_GetName,
    AndroidOpenSlDecoderOutput_GetProtocol,
    AndroidOpenSlDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AndroidOpenSlDecoderOutput, BLT_PacketProducer)
    AndroidOpenSlDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    AndroidOpenSlDecoder_InputCallback
+---------------------------------------------------------------------*/
static SLresult 
AndroidOpenSlDecoder_InputCallback(
        SLAndroidBufferQueueItf    queue,
        void*                      callback_context,
        void*                      buffer_context,
        void*                      buffer_data,
        SLuint32                   data_size,
        SLuint32                   data_used,
        const SLAndroidBufferItem* items,
        SLuint32                   items_length)
{
    AndroidOpenSlDecoder* self = (AndroidOpenSlDecoder*)callback_context;

    ATX_LOG_FINER_2("input callback, data_size=%u, data_used=%u", data_size, data_used);

    pthread_mutex_lock(&self->input.buffers_lock);
    for (unsigned int i=0; i<BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT; i++) {
        if (self->input.buffers[i]) {
            if (self->input.buffers[i]->GetData() == buffer_data) {
                /* found a match for the buffer that's being released */
                ATX_LOG_FINEST_1("releasing buffer %d", i);
                delete self->input.buffers[i];
                self->input.buffers[i] = NULL;
                break;
            }
        }
    }
    for (;;) {
        unsigned int filled_buffer_count = 0;
        for (unsigned int i=0; i<BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT; i++) {
            if (self->input.buffers[i]) {
                ++filled_buffer_count;
            }
        }
        if (filled_buffer_count || self->flushing || self->eos) {
            break;
        }
        ATX_LOG_FINEST("waiting for more buffers to be queued");
        pthread_cond_wait(&self->input.buffers_available_condition, &self->input.buffers_lock);
    }
    pthread_mutex_unlock(&self->input.buffers_lock);

    return SL_RESULT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOpenSlDecoder_OutputCallback
+---------------------------------------------------------------------*/
static void 
AndroidOpenSlDecoder_OutputCallback(SLAndroidSimpleBufferQueueItf queue,
                                    void*                         callback_context)
{
    AndroidOpenSlDecoder* self = (AndroidOpenSlDecoder*)callback_context;
    SLresult              result;

    ATX_LOG_FINER("output callback");

    /* update the media type if needed */
    if (self->output.media_type.sample_rate == 0) {
        union {
            SLMetadataInfo value;
            int8_t         raw_data[sizeof(SLMetadataInfo)+4];
        } metadata;

        result = (*self->sl_metadata_interface)->GetValue(self->sl_metadata_interface, 
                                                          self->sl_metadata_info.sample_rate_key_index,
                                                          sizeof(metadata), &metadata.value);
        if (result == SL_RESULT_SUCCESS) {
            self->output.media_type.sample_rate = (BLT_UInt32)*((SLuint32*)metadata.value.data);
            ATX_LOG_FINER_1("sample rate = %d", self->output.media_type.sample_rate);
        }

        result = (*self->sl_metadata_interface)->GetValue(self->sl_metadata_interface, 
                                                          self->sl_metadata_info.channel_count_key_index,
                                                          sizeof(metadata), &metadata.value);
        if (result == SL_RESULT_SUCCESS) {
            self->output.media_type.channel_count = (BLT_UInt16)*((SLuint32*)metadata.value.data);
            ATX_LOG_FINER_1("channel count = %d", self->output.media_type.channel_count);
        }

        result = (*self->sl_metadata_interface)->GetValue(self->sl_metadata_interface, 
                                                          self->sl_metadata_info.bits_per_sample_key_index,
                                                          sizeof(metadata), &metadata.value);
        if (result == SL_RESULT_SUCCESS) {
            self->output.media_type.bits_per_sample = (BLT_UInt8)*((SLuint32*)metadata.value.data);
            ATX_LOG_FINER_1("bits per sample = %d", self->output.media_type.bits_per_sample);
        }
    }

    /* create a media packet to hold the samples */
    BLT_MediaPacket* packet = NULL;
    BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                               BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE,
                               &self->output.media_type.base,
                               &packet);
    ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(packet),
                   &self->output.pcm_buffer[self->output.pcm_buffer_index*BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE],
                   BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE);
    BLT_MediaPacket_SetPayloadSize(packet, BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE);
    pthread_mutex_lock(&self->output.packets_lock);
    ATX_List_AddData(self->output.packets, packet);
    pthread_mutex_unlock(&self->output.packets_lock);

    /* re-enqueue the buffer */
    ATX_LOG_FINER_1("re-enqueueing PCM buffer %u", self->output.pcm_buffer_index);
    result = (*self->sl_output_buffer_queue_interface)->Enqueue(
        self->sl_output_buffer_queue_interface, 
        &self->output.pcm_buffer[self->output.pcm_buffer_index*BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE], 
        BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE);

    if (++self->output.pcm_buffer_index >= BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_COUNT) {
        self->output.pcm_buffer_index = 0;
    }
}

/*----------------------------------------------------------------------
|    AndroidOpenSlDecoder_SetupOpenSL
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOpenSlDecoder_SetupOpenSL(AndroidOpenSlDecoder* self)
{
    SLresult result;

    /* create the OpenSL engine */
    SLEngineOption engine_options[] = {
        {(SLuint32) SL_ENGINEOPTION_THREADSAFE, (SLuint32) SL_BOOLEAN_TRUE}
    };    
    result = slCreateEngine(&self->sl_engine_object, 1, engine_options, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("slCreateEngine failed (%d)", (int)result);
        return BLT_FAILURE;
    }
    
    /* realize the engine */
    result = (*self->sl_engine_object)->Realize(self->sl_engine_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("Realize failed (%d)", (int)result);
        return BLT_FAILURE;
    }
    
    /* get the engine interface, which is needed in order to create other objects */
    result = (*self->sl_engine_object)->GetInterface(self->sl_engine_object, 
                                                     SL_IID_ENGINE, 
                                                     &self->sl_engine);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface (SL_IID_ENGINE) failed (%d)", (int)result);
        return BLT_FAILURE;
    }
                    
    /* initialize interface requirements (the SL_IID_PLAY interface is implicit) */
    SLboolean     if_required[3];
    SLInterfaceID if_iids[3];
    if_required[0] = SL_BOOLEAN_TRUE;
    if_iids[0]     = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
    if_required[1] = SL_BOOLEAN_TRUE;
    if_iids[1]     = SL_IID_ANDROIDBUFFERQUEUESOURCE;
    if_required[2] = SL_BOOLEAN_TRUE;
    if_iids[2]     = SL_IID_METADATAEXTRACTION;

    /* setup the data source for queueing AAC/ADTS buffers */
    SLDataLocator_AndroidBufferQueue source_locator = {
            SL_DATALOCATOR_ANDROIDBUFFERQUEUE            /* locatorType */,
            BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT /* numBuffers  */
    };
    SLDataFormat_MIME source_format = {
            SL_DATAFORMAT_MIME,      /* formatType    */
            SL_ANDROID_MIME_AACADTS, /* mimeType      */
            SL_CONTAINERTYPE_RAW,    /* containerType */
    };
    SLDataSource data_source = {
        &source_locator, /* pLocator */
        &source_format   /* pFormat  */
    };

    /* setup the data sink, a buffer queue for buffers of PCM data */
    SLDataLocator_AndroidSimpleBufferQueue sink_locator = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,    /* locatorType */
            BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_COUNT /* numBuffers  */ 
    };

    /* destination format (parameters after formatType are ignored, they will be set */
    /* after decoding has started                                                    */
    SLDataFormat_PCM sink_format = { 
        SL_DATAFORMAT_PCM,                              /* formatType        */
        2,                                              /* numChannels       */ 
        SL_SAMPLINGRATE_44_1,                           /* samplesPerSec     */ 
        SL_PCMSAMPLEFORMAT_FIXED_16,                    /* pcm.bitsPerSample */ 
        16,                                             /* containerSize     */ 
        SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, /* channelMask       */ 
        SL_BYTEORDER_LITTLEENDIAN                       /* endianness        */ 
    };
    SLDataSink data_sink = {
        &sink_locator, /* pLocator */
        &sink_format   /* pFormat  */
    };

    /* create an audio player */
    result = (*self->sl_engine)->CreateAudioPlayer(
        self->sl_engine, 
        &self->sl_player, 
        &data_source, 
        &data_sink,
        sizeof(if_iids)/sizeof(if_iids[0]),
        if_iids, 
        if_required);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("CreateAudioPlayer failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* realize the player in synchronous mode. */
    result = (*self->sl_player)->Realize(self->sl_player, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("Realize failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* get the play interface */
    result = (*self->sl_player)->GetInterface(self->sl_player, SL_IID_PLAY, (void*)&self->sl_play_interface);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface(SL_IID_PLAY) failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* get the input buffer queue interface */
    result = (*self->sl_player)->GetInterface(self->sl_player, SL_IID_ANDROIDBUFFERQUEUESOURCE, (void*)&self->sl_input_buffer_queue_interface);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface(SL_IID_ANDROIDBUFFERQUEUESOURCE) failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* get the output simple buffer queue interface */
    result = (*self->sl_player)->GetInterface(self->sl_player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, (void*)&self->sl_output_buffer_queue_interface);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface(SL_IID_ANDROIDSIMPLEBUFFERQUEUE) failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* get the metadata extraction interface */
    result = (*self->sl_player)->GetInterface(self->sl_player, SL_IID_METADATAEXTRACTION, (void*)&self->sl_metadata_interface);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface(SL_IID_METADATAEXTRACTION) failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* register the input callback */
    result = (*self->sl_input_buffer_queue_interface)->RegisterCallback(
        self->sl_input_buffer_queue_interface, 
        AndroidOpenSlDecoder_InputCallback, 
        self);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("RegisterCallback() failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* register the output callback */
    result = (*self->sl_output_buffer_queue_interface)->RegisterCallback(
        self->sl_output_buffer_queue_interface, 
        AndroidOpenSlDecoder_OutputCallback, 
        self);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("RegisterCallback() failed (%d)", (int)result);
        return BLT_FAILURE;
    }

    /* enqueue empty PCM buffers */
    for(unsigned int i = 0; i < BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_COUNT; i++) {
        result = (*self->sl_output_buffer_queue_interface)->Enqueue(
            self->sl_output_buffer_queue_interface, 
            &self->output.pcm_buffer[i*BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE], 
            BLT_ANDROID_OPENSL_DECODER_PCM_BUFFER_SIZE);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("Enqueue failed (%d)", result);
            return BLT_FAILURE;
        }
    }

    /* obtain metadata key indices */
    SLuint32 item_count;
    result = (*self->sl_metadata_interface)->GetItemCount(self->sl_metadata_interface, &item_count);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetItemCount (%d)", result);
        return BLT_FAILURE;
    }
    ATX_LOG_FINER_1("found %u metadata items", item_count);
    for(unsigned int i=0 ; i<item_count ; i++) {
        SLuint32 key_size = 0;
        result = (*self->sl_metadata_interface)->GetKeySize(self->sl_metadata_interface, i, &key_size);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("GetKeySize (%d)", result);
            return BLT_FAILURE;
        }
        SLMetadataInfo* key_info = (SLMetadataInfo*)malloc(key_size);
        if (key_info) {
            result = (*self->sl_metadata_interface)->GetKey(self->sl_metadata_interface, i, key_size, key_info);
            if (result != SL_RESULT_SUCCESS) {
                ATX_LOG_WARNING_1("GetKey (%d)", result);
                return BLT_FAILURE;
            }
            ATX_LOG_FINER_3("key %d: size=%d, name=%s", i, key_info->size, key_info->data);
            if (!strcmp((char*)key_info->data, ANDROID_KEY_PCMFORMAT_NUMCHANNELS)) {
                self->sl_metadata_info.channel_count_key_index = i;
            } else if (!strcmp((char*)key_info->data, ANDROID_KEY_PCMFORMAT_SAMPLERATE)) {
                self->sl_metadata_info.sample_rate_key_index = i;
            } else if (!strcmp((char*)key_info->data, ANDROID_KEY_PCMFORMAT_BITSPERSAMPLE)) {
                self->sl_metadata_info.bits_per_sample_key_index = i;
            } else if (!strcmp((char*)key_info->data, ANDROID_KEY_PCMFORMAT_CONTAINERSIZE)) {
                self->sl_metadata_info.container_size_key_index = i;
            } else if (!strcmp((char*)key_info->data, ANDROID_KEY_PCMFORMAT_CHANNELMASK)) {
                self->sl_metadata_info.channel_mask_key_index = i;
            } else if (!strcmp((char*)key_info->data, ANDROID_KEY_PCMFORMAT_ENDIANNESS)) {
                self->sl_metadata_info.endianness_key_index = i;
            }
            free(key_info);
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOpenSlDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOpenSlDecoder_Destroy(AndroidOpenSlDecoder* self)
{ 

    /* stop the player */
    ATX_LOG_FINE("stopping player");
    if (self->sl_play_interface) {
        (*self->sl_play_interface)->SetPlayState(self->sl_play_interface, SL_PLAYSTATE_STOPPED);
        self->sl_player_started = false;
    }

    /* release input resources */
    if (self->input.media_type) {
        BLT_MediaType_Free((BLT_MediaType*)self->input.media_type);
    }
    for (unsigned int i=0; i<BLT_ANDROID_OPENSL_DECODER_ADTS_BUFFER_COUNT; i++) {
        delete self->input.buffers[i];
        self->input.buffers[i] = NULL;
    }   
    pthread_cond_destroy(&self->input.buffers_available_condition);
    pthread_mutex_destroy(&self->input.buffers_lock);

    /* release output resources */
    AndroidOpenSlDecoder_Flush(self);
    if (self->output.packets) {
        ATX_List_Destroy(self->output.packets);
    }
    pthread_mutex_destroy(&self->output.packets_lock);

    /* destroy the player */
    if (self->sl_player) {
        (*self->sl_player)->Destroy(self->sl_player);
    }

    /* destroy the OpenSL engine */
    if (self->sl_engine_object) {
        (*self->sl_engine_object)->Destroy(self->sl_engine_object);
    }
        /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   AndroidOpenSlDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoder_GetPortByName(BLT_MediaNode*  _self,
                            BLT_CString     name,
                            BLT_MediaPort** port)
{
    AndroidOpenSlDecoder* self = ATX_SELF_EX(AndroidOpenSlDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    AndroidOpenSlDecoder_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoder_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    AndroidOpenSlDecoder* self = ATX_SELF_EX(AndroidOpenSlDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_BASE(self, BLT_BaseMediaNode).context = stream;

    /* setup gapless info */
    self->gapless_info.leading_frames  = 0;
    self->gapless_info.trailing_frames = 0;
    self->gapless_info.valid_frames    = 0;
    ATX_Properties* properties = NULL;
    if (BLT_SUCCEEDED(BLT_Stream_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).context, &properties))) {
        ATX_PropertyValue property;
        if (ATX_SUCCEEDED(ATX_Properties_GetProperty(properties, BLT_STREAM_GAPLESS_LEADING_FRAMES_PROPERTY, &property)) &&
            property.type == ATX_PROPERTY_VALUE_TYPE_INTEGER) {
            self->gapless_info.leading_frames = (unsigned int)property.data.integer;
        }
        if (ATX_SUCCEEDED(ATX_Properties_GetProperty(properties, BLT_STREAM_GAPLESS_TRAILING_FRAMES_PROPERTY, &property)) &&
            property.type == ATX_PROPERTY_VALUE_TYPE_INTEGER) {
            self->gapless_info.trailing_frames = (unsigned int)property.data.integer;
        }
        if (ATX_SUCCEEDED(ATX_Properties_GetProperty(properties, BLT_STREAM_GAPLESS_VALID_FRAMES_PROPERTY, &property)) &&
            property.type == ATX_PROPERTY_VALUE_TYPE_LARGE_INTEGER) {
            self->gapless_info.valid_frames = (ATX_UInt64)property.data.large_integer;
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOpenSlDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoder_Seek(BLT_MediaNode* _self,
                          BLT_SeekMode*  mode,
                          BLT_SeekPoint* point)
{
    AndroidOpenSlDecoder* self = ATX_SELF_EX(AndroidOpenSlDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);
        
    AndroidOpenSlDecoder_Flush(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AndroidOpenSlDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOpenSlDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOpenSlDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AndroidOpenSlDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    AndroidOpenSlDecoder_GetPortByName,
    AndroidOpenSlDecoder_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    AndroidOpenSlDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AndroidOpenSlDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|    AndroidOpenSlDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOpenSlDecoder_Create(BLT_Module*              module,
                            BLT_Core*                core,
                            BLT_ModuleParametersType parameters_type,
                            ATX_AnyConst             parameters,
                            BLT_MediaNode**          object)
{
    AndroidOpenSlDecoder*       self;
    AndroidOpenSlDecoderModule* aac_decoder_module = (AndroidOpenSlDecoderModule*)module;

    ATX_LOG_FINE("AndroidOpenSlDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = (AndroidOpenSlDecoder*)ATX_AllocateZeroMemory(sizeof(AndroidOpenSlDecoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup the input and output ports */
    self->mp4es_type_id = aac_decoder_module->mp4es_type_id;
    BLT_PcmMediaType_Init(&self->output.media_type);
    pthread_mutex_init(&self->input.buffers_lock, NULL);
    pthread_cond_init(&self->input.buffers_available_condition, NULL);
    self->output.media_type.bits_per_sample = 16;
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    ATX_List_Create(&self->output.packets);
    pthread_mutex_init(&self->output.packets_lock, NULL);

   /* setup OpenSL */
    ATX_LOG_FINE("setting up OpenSL...");
    BLT_Result result = AndroidOpenSlDecoder_SetupOpenSL(self);
    if (BLT_FAILED(result)) {
        AndroidOpenSlDecoder_Destroy(self);
        return result;
    }
    ATX_LOG_FINE("OpenSL setup");

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, AndroidOpenSlDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, AndroidOpenSlDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  AndroidOpenSlDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  AndroidOpenSlDecoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, AndroidOpenSlDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, AndroidOpenSlDecoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AndroidOpenSlDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    AndroidOpenSlDecoderModule* self = ATX_SELF_EX(AndroidOpenSlDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*               registry;
    BLT_Result                  result;

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
    
    ATX_LOG_FINE_1("AndroidOpenSlDecoderModule::Attach (" BLT_MP4_AUDIO_ES_MIME_TYPE " = %d)", self->mp4es_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AndroidOpenSlDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOpenSlDecoderModule_Probe(BLT_Module*              _self, 
                                 BLT_Core*                core,
                                 BLT_ModuleParametersType parameters_type,
                                 BLT_AnyConst             parameters,
                                 BLT_Cardinal*            match)
{
    AndroidOpenSlDecoderModule* self = ATX_SELF_EX(AndroidOpenSlDecoderModule, BLT_BaseModule, BLT_Module);

    ATX_Properties* core_properties = NULL;
    if (self->probe_score == 0) {
        if (BLT_SUCCEEDED(BLT_Core_GetProperties(core, &core_properties))) {
            ATX_PropertyValue property;
            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(core_properties, "com.axiosys.decoders.android.opensl.probe_score", &property)) &&
                property.type == ATX_PROPERTY_VALUE_TYPE_INTEGER) {
                self->probe_score = (BLT_UInt32)property.data.integer;
                ATX_LOG_FINER_1("module default probe score = %u", self->probe_score);
            }
        }
    }

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
                if (ATX_StringsEqual(constructor->name, "com.axiosys.decoders.android.opensl")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = self->probe_score;
            }

            ATX_LOG_FINE_1("AndroidOpenSlDecoderModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AndroidOpenSlDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOpenSlDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOpenSlDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AndroidOpenSlDecoderModule, AndroidOpenSlDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AndroidOpenSlDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    AndroidOpenSlDecoderModule_Attach,
    AndroidOpenSlDecoderModule_CreateInstance,
    AndroidOpenSlDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AndroidOpenSlDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AndroidOpenSlDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(AndroidOpenSlDecoderModule,
                                         "Android OpenSL Audio Decoder",
                                         "com.axiosys.decoder.android.opensl",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
