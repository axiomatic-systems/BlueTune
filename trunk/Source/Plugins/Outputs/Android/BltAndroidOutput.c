/*****************************************************************
|
|      Android Output Module
|
|      (c) 2002-2012 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltAndroidOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"
#include "BltVolumeControl.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.android")

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(AndroidOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(AndroidOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(AndroidOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(AndroidOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(AndroidOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(AndroidOutput, BLT_PacketConsumer)
ATX_DECLARE_INTERFACE_MAP(AndroidOutput, BLT_VolumeControl)

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE        8
#define BLT_ANDROID_OUTPUT_PACKET_QUEUE_MAX_WAIT    50
#define BLT_ANDROID_OUTPUT_PACKET_QUEUE_WAIT_TIME   100000000 /* 0.1 seconds */

const float BLT_ANDROID_OUTPUT_VOLUME_SCALE = 50.0;

// 20*log10(vol) for volume between 0.0 and 1.0
const float BLT_ANDROID_OUTPUT_VOLUME_CURVE[101] = {
    -92.1034037198,
    -78.2404601086,
    -70.1311579464,
    -64.3775164974,
    -59.9146454711,
    -56.2682143352,
    -53.1852007387,
    -50.5145728862,
    -48.158912173,
    -46.0517018599,
    -44.1454982638,
    -42.405270724,
    -40.8044165705,
    -39.3222571275,
    -37.9423996977,
    -36.651629275,
    -35.4391368386,
    -34.2959685618,
    -33.2146241364,
    -32.1887582487,
    -31.2129549653,
    -30.2825546526,
    -29.3935194012,
    -28.5423271128,
    -27.7258872224,
    -26.9414729593,
    -26.1866663997,
    -25.4593135163,
    -24.75748712,
    -24.0794560865,
    -23.4236596301,
    -22.7886856638,
    -22.1732524904,
    -21.5761932274,
    -20.99644249,
    -20.4330249506,
    -19.8850454669,
    -19.3516805252,
    -18.8321707972,
    -18.3258146375,
    -17.8319623857,
    -17.3500113541,
    -16.8794014059,
    -16.4196110414,
    -15.9701539244,
    -15.53057579,
    -15.1004516856,
    -14.6793835016,
    -14.2669977575,
    -13.8629436112,
    -13.4668910653,
    -13.0785293481,
    -12.6975654487,
    -12.3237227885,
    -11.9567400151,
    -11.5963699051,
    -11.2423783631,
    -10.8945435088,
    -10.5526548416,
    -10.2165124753,
    -9.8859264363,
    -9.56071601886,
    -9.24070919193,
    -8.92574205257,
    -8.61565832185,
    -8.31030887923,
    -8.00955133194,
    -7.71324961624,
    -7.42127362782,
    -7.13349887877,
    -6.84980617894,
    -6.57008133944,
    -6.29421489679,
    -6.02210185568,
    -5.75364144904,
    -5.48873691404,
    -5.22729528269,
    -4.96922718597,
    -4.71444667042,
    -4.46287102628,
    -4.21442062631,
    -3.96901877448,
    -3.72659156383,
    -3.4870677429,
    -3.25037858996,
    -3.01645779469,
    -2.78524134667,
    -2.5566674302,
    -2.33067632512,
    -2.10721031316,
    -1.88621358942,
    -1.66763217878,
    -1.4514138567,
    -1.23750807436,
    -1.02586588775,
    -0.816439890405,
    -0.609184149694,
    -0.40405414635,
    -0.20100671707,
    0.0
};

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} AndroidOutputModule;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_VolumeControl);

    /* members */
    SLObjectItf                   sl_engine_object;
    SLEngineItf                   sl_engine;
    SLObjectItf                   sl_output_mix_object;
    SLObjectItf                   sl_player_object;
    SLPlayItf                     sl_player_play;
    SLAndroidSimpleBufferQueueItf sl_player_buffer_queue;
    SLVolumeItf                   sl_player_volume;
    SLmillibel                    sl_player_max_volume;
    
    BLT_MediaPacket* volatile     packet_queue[BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE];
    ATX_Ordinal                   packet_index;
    
    BLT_PcmMediaType expected_media_type;
    BLT_PcmMediaType media_type;
    ATX_UInt64       media_time;      /* media time of the last received packet */
} AndroidOutput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
static BLT_Result AndroidOutput_SetupOutput(AndroidOutput* self, const BLT_PcmMediaType* format);
static BLT_Result AndroidOutput_Drain(BLT_OutputNode* self);

/*----------------------------------------------------------------------
|    AndroidOutput_Callback
+---------------------------------------------------------------------*/
static void 
AndroidOutput_Callback(SLAndroidSimpleBufferQueueItf queue, void* context)
{
    AndroidOutput*                  self = (AndroidOutput*)context;
    unsigned int                    queue_index;
    SLAndroidSimpleBufferQueueState state;
    SLresult                        result;
    
    if (queue == NULL || context == NULL) return;

    result = (*queue)->GetState(queue, &state);
    queue_index = (state.index-1)%BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE;
    ATX_LOG_FINER_3("callback for %d, count=%d, index=%d", queue_index, state.count, state.index);
    if (self->packet_queue[queue_index] == NULL) {
        ATX_LOG_WARNING("queue entry null, not expected");
        return;
    }
    BLT_MediaPacket_Release(self->packet_queue[queue_index]);
    self->packet_queue[queue_index] = NULL;
}

/*----------------------------------------------------------------------
|    AndroidOutput_Reset
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_Reset(AndroidOutput* self)
{
    SLresult result;
    unsigned int i;
    
    ATX_LOG_FINER("resetting output");

    /* clear the queue */
    if (self->sl_player_buffer_queue) {
        result = (*self->sl_player_buffer_queue)->Clear(self->sl_player_buffer_queue);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_FINE_1("Clear() failed (%d)", result);
        }
    }
    
    /* reset the queue state */
    for (i=0; i<BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE; i++) {
        if (self->packet_queue[i]) {
            BLT_MediaPacket_Release(self->packet_queue[i]);
            self->packet_queue[i] = NULL;
        }
    }
    self->packet_index = 0;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_PutPacket(BLT_PacketConsumer* _self,
                        BLT_MediaPacket*    packet)
{
    AndroidOutput*          self = ATX_SELF(AndroidOutput, BLT_PacketConsumer);
    const BLT_PcmMediaType* media_type;
    unsigned int            watchdog = BLT_ANDROID_OUTPUT_PACKET_QUEUE_MAX_WAIT;
    unsigned int            queue_index;
    BLT_Result              result;
    
    ATX_LOG_FINEST("put packet");
    
    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)(const void*)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* exit early if the packet is empty */
    if (BLT_MediaPacket_GetPayloadSize(packet) == 0) return BLT_SUCCESS;
    
    /* check if the media type has changed */
    if (media_type->sample_rate     != self->media_type.sample_rate     ||
        media_type->channel_count   != self->media_type.channel_count   ||
        media_type->bits_per_sample != self->media_type.bits_per_sample) {
        ATX_LOG_FINE("PCM format changed, configuring output");
        
        if (self->media_type.sample_rate) {
            AndroidOutput_Drain(&ATX_BASE(self, BLT_OutputNode));
            AndroidOutput_Reset(self);
        }
        result = AndroidOutput_SetupOutput(self, media_type);

        self->media_type = *media_type;
        if (BLT_FAILED(result)) {
            ATX_LOG_WARNING_1("AndroidOutput_SetupOutput failed (%d)", result);
            return result;
        }
        if (self->sl_player_play) {
            result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_PLAYING);
            if (result != SL_RESULT_SUCCESS) {
                ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
            }
        } else {
            ATX_LOG_WARNING("sl_player_play is NULL...");
            return BLT_ERROR_INVALID_STATE;
        }
    }
    
    /* wait for some space in the packet queue */
    queue_index = self->packet_index%BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE;
    for (;;) {
        if (self->packet_queue[queue_index] == NULL) {
            break;
        } else {
            ATX_LOG_FINEST("waiting for queue entry");
            ATX_TimeInterval wait;
            wait.seconds     = 0;
            wait.nanoseconds = BLT_ANDROID_OUTPUT_PACKET_QUEUE_WAIT_TIME;
            ATX_System_Sleep(&wait);
        }
        if (watchdog-- == 0) {
            ATX_LOG_WARNING("the watchdog bit us!");
            AndroidOutput_Reset(self);
            return ATX_ERROR_TIMEOUT;
        }
    }
    
    /* enqueue the payload */
    ATX_LOG_FINER_2("enqueueing packet %d, size=%d", queue_index, BLT_MediaPacket_GetPayloadSize(packet));
    self->packet_queue[queue_index] = packet;
    BLT_MediaPacket_AddReference(packet);
    result = (*self->sl_player_buffer_queue)->Enqueue(self->sl_player_buffer_queue,
                                                      BLT_MediaPacket_GetPayloadBuffer(packet),
                                                      BLT_MediaPacket_GetPayloadSize(packet));
    if (result != SL_RESULT_SUCCESS) {
        self->packet_queue[queue_index] = NULL;
        BLT_MediaPacket_Release(packet);
        ATX_LOG_WARNING_1("Enqueue failed (%d)", result);
        return BLT_FAILURE;
    }
    ++self->packet_index;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_QueryMediaType(BLT_MediaPort*        _self,
                             BLT_Ordinal           index,
                             const BLT_MediaType** media_type)
{
    AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    AndroidOutput_SetupOpenSL
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_SetupOpenSL(AndroidOutput* self)
{
    SLresult result;

    /* create the OpenSL engine */
    result = slCreateEngine(&self->sl_engine_object, 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("slCreateEngine failed (%d)", result);
        return BLT_FAILURE;
    }
    
    /* realize the engine */
    result = (*self->sl_engine_object)->Realize(self->sl_engine_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("Realize failed (%d)", result);
        return BLT_FAILURE;
    }
    
    /* get the engine interface, which is needed in order to create other objects */
    result = (*self->sl_engine_object)->GetInterface(self->sl_engine_object, 
                                                     SL_IID_ENGINE, 
                                                     &self->sl_engine);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface (SL_IID_ENGINE) failed (%d)", result);
        return BLT_FAILURE;
    }
    
    /* create output mix */
    {
        const SLInterfaceID ids[1] = { SL_IID_VOLUME };
        const SLboolean req[1]     = { SL_BOOLEAN_FALSE };
        result = (*self->sl_engine)->CreateOutputMix(self->sl_engine, 
                                                     &self->sl_output_mix_object, 
                                                     1, ids, req);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("CreateOutputMix failed (%d)", result);
            return BLT_FAILURE;
        }
    }
    
    /* realize the output mix */
    result = (*self->sl_output_mix_object)->Realize(self->sl_output_mix_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("Realize failed (%d)", result);
        return BLT_FAILURE;
    }
                
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_GetChannelMask
+---------------------------------------------------------------------*/
static SLuint32
AndroidOutput_GetDefaultChannelMask(BLT_UInt32 channel_count)
{
    switch (channel_count) {
        case 1: return SL_SPEAKER_FRONT_CENTER;
        case 2: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        case 3: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_FRONT_RIGHT;
        case 4: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT  | SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT;
        case 5: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_FRONT_RIGHT  | SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT;
        case 6: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_FRONT_RIGHT  | SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT  | SL_SPEAKER_LOW_FREQUENCY;
        case 7: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_FRONT_RIGHT  | SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_CENTER | SL_SPEAKER_BACK_RIGHT   | SL_SPEAKER_LOW_FREQUENCY;
        case 8: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_FRONT_RIGHT  | SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT  | SL_SPEAKER_SIDE_LEFT    | SL_SPEAKER_SIDE_RIGHT    | SL_SPEAKER_LOW_FREQUENCY;
        default: return 0;
    }
}

/*----------------------------------------------------------------------
|    AndroidOutput_SetupOutput
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_SetupOutput(AndroidOutput* self, const BLT_PcmMediaType* format)
{
    SLresult result;
    
    /* release the audio player if one exists */
    if (self->sl_player_object) {
        (*self->sl_player_object)->Destroy(self->sl_player_object);
        self->sl_player_object = NULL;
        self->sl_player_play = NULL;
        self->sl_player_buffer_queue = NULL;
        self->sl_player_volume = NULL;
    }

    /* create audio player */
    {
        /* audio source configuration */
        SLDataLocator_AndroidSimpleBufferQueue bq_locator = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 
            BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE /* number of buffers */
        };
        SLuint32 channel_mask = format->channel_mask?format->channel_mask:AndroidOutput_GetDefaultChannelMask(format->channel_count);
        SLDataFormat_PCM format_pcm = {
            SL_DATAFORMAT_PCM, 
            format->channel_count,
            1000*format->sample_rate,
            format->bits_per_sample,
            format->bits_per_sample,
            channel_mask,
            SL_BYTEORDER_LITTLEENDIAN};
        SLDataSource audio_source = {
            &bq_locator, 
            &format_pcm
        };

        /* audio sink configuration */
        SLDataLocator_OutputMix out_locator = {
            SL_DATALOCATOR_OUTPUTMIX, 
            self->sl_output_mix_object
        };
        SLDataSink audio_sink = {
            &out_locator, 
            NULL
        };

        ATX_LOG_FINE_3("creating SL Audio Player, sr=%d, ch=%d, chmsk=%x",
                       format->sample_rate,
                       format->channel_count,
                       channel_mask);
        const SLInterfaceID ids[2] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME };
        const SLboolean req[2]     = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
        result = (*self->sl_engine)->CreateAudioPlayer(self->sl_engine,
                                                       &self->sl_player_object, 
                                                       &audio_source, 
                                                       &audio_sink,
                                                       2, ids, req);
    }
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("CreateAudioPlayer failed (%d)", result);
        return BLT_FAILURE;
    }

    /* realize the player */
    result = (*self->sl_player_object)->Realize(self->sl_player_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("Realize failed (%d)", result);
        return BLT_FAILURE;
    }

    /* get the play interface */
    result = (*self->sl_player_object)->GetInterface(self->sl_player_object, 
                                                     SL_IID_PLAY, 
                                                     &self->sl_player_play);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface (SL_IID_PLAY) failed (%d)", result);
        return BLT_FAILURE;
    }

    /* get the buffer queue interface */
    result = (*self->sl_player_object)->GetInterface(self->sl_player_object, 
                                                     SL_IID_ANDROIDSIMPLEBUFFERQUEUE /*SL_IID_BUFFERQUEUE*/,
                                                     &self->sl_player_buffer_queue);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface (SL_IID_ANDROIDSIMPLEBUFFERQUEUE) failed (%d)", result);
        return BLT_FAILURE;
    }

    /* register a callback with the buffer queue */
    result = (*self->sl_player_buffer_queue)->RegisterCallback(self->sl_player_buffer_queue, 
                                                               AndroidOutput_Callback, 
                                                               self);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("RegisterCallback failed (%d)", result);
        return BLT_FAILURE;
    }

    /* get the volume interface */
    result = (*self->sl_player_object)->GetInterface(self->sl_player_object, 
                                                     SL_IID_VOLUME, 
                                                     &self->sl_player_volume);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("GetInterface (sl_player_object, SL_IID_VOLUME) failed (%d)", result);
        return BLT_FAILURE;
    }

    /* query the current volume info */
    {
        SLmillibel volume = 0;
        result = (*self->sl_player_volume)->GetMaxVolumeLevel(self->sl_player_volume, &self->sl_player_max_volume);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("GetMaxVolumeLevel failed (%d)", result);
        }
        result = (*self->sl_player_volume)->GetVolumeLevel(self->sl_player_volume, &volume);        
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("GetVolumeLevel failed (%d)", result);
        }
        ATX_LOG_FINE_2("volume: current=%d, max=%d", (int)volume, (int)self->sl_player_max_volume);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_Destroy(AndroidOutput* self)
{
    ATX_LOG_FINE("destroying output");

    /* release the OpenSL resources */
    if (self->sl_player_object) {
        (*self->sl_player_object)->Destroy(self->sl_player_object);
    }
    /* destroy the output mix */
    if (self->sl_output_mix_object) {
        (*self->sl_output_mix_object)->Destroy(self->sl_output_mix_object);
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
|    AndroidOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_Create(BLT_Module*              module,
                     BLT_Core*                core, 
                     BLT_ModuleParametersType parameters_type,
                     BLT_AnyConst             parameters, 
                     BLT_MediaNode**          object)
{
    AndroidOutput* output;
    BLT_Result     result;
    /*BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;*/

    ATX_LOG_FINE("creating output");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(AndroidOutput));
    if (output == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(output, BLT_BaseMediaNode), module, core);

    /* construct the object */
    output->media_type.sample_rate     = 0;
    output->media_type.channel_count   = 0;
    output->media_type.bits_per_sample = 0;
    
    /* setup the expected media type */
    BLT_PcmMediaType_Init(&output->expected_media_type);
    
    /* setup OpenSL */
    ATX_LOG_FINE("setting up OpenSL...");
    result = AndroidOutput_SetupOpenSL(output);
    if (BLT_FAILED(result)) {
        AndroidOutput_Destroy(output);
        return result;
    }
    ATX_LOG_FINE("OpenSL setup");
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(output, AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(output, AndroidOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(output, AndroidOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE(output, AndroidOutput, BLT_OutputNode);
    ATX_SET_INTERFACE(output, AndroidOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(output, AndroidOutput, BLT_VolumeControl);
    *object = &ATX_BASE_EX(output, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AndroidOutput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(stream);
        
    ATX_LOG_FINER("activating output");

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AndroidOutput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Deactivate(BLT_MediaNode* _self)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(self);

    ATX_LOG_FINER("deactivating output");

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AndroidOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Start(BLT_MediaNode* _self)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    SLresult result;
    
    ATX_LOG_FINER("starting output");

    /* set the player's state to playing */
    if (self->sl_player_play) {
        result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_PLAYING);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
        }
    }
    
    return BLT_SUCCESS;
}    

/*----------------------------------------------------------------------
|       AndroidOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Stop(BLT_MediaNode* _self)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    SLresult result;

    ATX_LOG_FINER("stopping output");

    /* set the player's state to stopped */
    if (self->sl_player_play) {
        result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_STOPPED);
        if (result != SL_RESULT_SUCCESS) {
            ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
        }
    }
    
    /* reset the device */
    AndroidOutput_Reset(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AndroidOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Pause(BLT_MediaNode* _self)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    SLresult result;
        
    ATX_LOG_FINER("pausing output");
    
    /* set the player's state to playing */
    result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_PAUSED);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AndroidOutput_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Resume(BLT_MediaNode* _self)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    SLresult result;
        
    ATX_LOG_FINER("resuming output");

    /* set the player's state to playing */
    result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AndroidOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_GetPortByName(BLT_MediaNode*  _self,
                            BLT_CString     name,
                            BLT_MediaPort** port)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    AndroidOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_Seek(BLT_MediaNode* _self,
                   BLT_SeekMode*  mode,
                   BLT_SeekPoint* point)
{
    AndroidOutput* self = ATX_SELF_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    ATX_LOG_FINER("seeking");

    /* reset the device */
    AndroidOutput_Reset(self);

    /* update the media time */
    if (point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) {
        self->media_time = BLT_TimeStamp_ToNanos(point->time_stamp);
    } else {
        self->media_time = 0;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_GetStatus(BLT_OutputNode*       _self,
                        BLT_OutputNodeStatus* status)
{
    AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_OutputNode);

    /* default values */
    status->media_time.seconds = 0;
    status->media_time.nanoseconds = 0;
    status->flags = 0;
    status->media_time = BLT_TimeStamp_FromNanos(self->media_time);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_Drain(BLT_OutputNode* _self)
{
    /*AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_OutputNode);*/
    BLT_COMPILER_UNUSED(_self);

    ATX_LOG_FINER("draining output");
    
    /* FIXME: not implemented yet */
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_SetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_SetVolume(BLT_VolumeControl* _self, float volume)
{
    AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_VolumeControl);
    float scaled;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    scaled = BLT_ANDROID_OUTPUT_VOLUME_CURVE[(int)(volume*100.0f)];
    float millibels = BLT_ANDROID_OUTPUT_VOLUME_SCALE*(self->sl_player_max_volume+scaled);
    ATX_LOG_FINE_1("setting volume to %d millibels", (int)millibels);
    if (self->sl_player_volume) {
        (*self->sl_player_volume)->SetVolumeLevel(self->sl_player_volume, (SLmillibel)millibels);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_GetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_GetVolume(BLT_VolumeControl* _self, float* volume)
{
    AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_VolumeControl);
    SLmillibel millibels = 0;
    *volume = 0.0f;
    if (self->sl_player_volume) {
        if ((*self->sl_player_volume)->GetVolumeLevel(self->sl_player_volume, &millibels) == SL_RESULT_SUCCESS) {
            int i = 0;
            for (i=0; i<=100; i++) {
                float scaled = BLT_ANDROID_OUTPUT_VOLUME_CURVE[i];
                float tmillibels = BLT_ANDROID_OUTPUT_VOLUME_SCALE*(self->sl_player_max_volume+scaled);
                if (millibels >= tmillibels) {
                    *volume = (float)i/100.0f;
                }
            }
            ATX_LOG_FINE_1("volume is %f", *volume);
            return BLT_SUCCESS;
        }
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AndroidOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(AndroidOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT(AndroidOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AndroidOutput, BLT_PacketConsumer)
    ATX_GET_INTERFACE_ACCEPT(AndroidOutput, BLT_VolumeControl)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AndroidOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(AndroidOutput, BLT_MediaPort)
    AndroidOutput_GetName,
    AndroidOutput_GetProtocol,
    AndroidOutput_GetDirection,
    AndroidOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AndroidOutput, BLT_PacketConsumer)
    AndroidOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AndroidOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    AndroidOutput_GetPortByName,
    AndroidOutput_Activate,
    AndroidOutput_Deactivate,
    AndroidOutput_Start,
    AndroidOutput_Stop,
    AndroidOutput_Pause,
    AndroidOutput_Resume,
    AndroidOutput_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AndroidOutput, BLT_OutputNode)
    AndroidOutput_GetStatus,
    AndroidOutput_Drain
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_VolumeControl interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AndroidOutput, BLT_VolumeControl)
    AndroidOutput_GetVolume,
    AndroidOutput_SetVolume
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AndroidOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|       AndroidOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutputModule_Probe(BLT_Module*              self, 
                          BLT_Core*                core,
                          BLT_ModuleParametersType parameters_type,
                          BLT_AnyConst             parameters,
                          BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be PACKET and the */
            /* output protocol should be NONE              */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* the input type should be unknown, or audio/pcm */
            if (!(constructor->spec.input.media_type->id == 
                  BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.input.media_type->id == 
                  BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* the name should be 'Android:<name>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "android:", 4)) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            ATX_LOG_FINE_1("probe ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AndroidOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(AndroidOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AndroidOutputModule, AndroidOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AndroidOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    AndroidOutputModule_CreateInstance,
    AndroidOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AndroidOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AndroidOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_AndroidOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Android Output", NULL, 0, 
                                 &AndroidOutputModule_BLT_ModuleInterface,
                                 &AndroidOutputModule_ATX_ReferenceableInterface,
                                 object);
}
