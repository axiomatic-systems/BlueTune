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

const float BLT_ANDROID_OUTPUT_VOLUME_RANGE = 5000.0;

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
    SLVolumeItf                   sl_output_mix_volume;
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
|    AndroidOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
AndroidOutput_Drain(AndroidOutput* self)
{
    ATX_LOG_FINER("draining output");

    return BLT_SUCCESS;
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

    /* set the player's state to stopped */
    result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_STOPPED);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
    }

    /* clear the queue */
    result = (*self->sl_player_buffer_queue)->Clear(self->sl_player_buffer_queue);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_FINE_1("Clear() failed (%d)", result);
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
    unsigned int            queue_index = self->packet_index%BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE;
    BLT_Result              result;
    
    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)(const void*)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* wait for some space in the packet queue */
    
    /* enqueue the payload */
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
            return ATX_ERROR_TIMEOUT;
        }
    }
    ATX_LOG_FINER_2("enqueueing packet %d, size=%d", queue_index, BLT_MediaPacket_GetPayloadSize(packet));
    self->packet_queue[queue_index] = packet;
    BLT_MediaPacket_AddReference(packet);
    result = (*self->sl_player_buffer_queue)->Enqueue(self->sl_player_buffer_queue,
                                                      BLT_MediaPacket_GetPayloadBuffer(packet),
                                                      BLT_MediaPacket_GetPayloadSize(packet));
    if (result != SL_RESULT_SUCCESS) {
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
    
    /* get the output mix volume interface */
    result = (*self->sl_output_mix_object)->GetInterface(self->sl_output_mix_object, 
                                                         SL_IID_VOLUME, 
                                                         &self->sl_output_mix_volume);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_FINE_1("GetInterface (sl_output_mix_object, SL_IID_VOLUME) failed (%d)", result);
        self->sl_output_mix_volume = NULL;
    }
    
    /* create audio player */
    {
        /* audio source configuration */
        SLDataLocator_AndroidSimpleBufferQueue bq_locator = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 
            BLT_ANDROID_OUTPUT_PACKET_QUEUE_SIZE /* number of buffers */
        };
        SLDataFormat_PCM format_pcm = {
            SL_DATAFORMAT_PCM, 
            2, 
            SL_SAMPLINGRATE_44_1,
            16, 
            16,
            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, 
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
    result = AndroidOutput_SetupOpenSL(output);
    if (BLT_FAILED(result)) {
        AndroidOutput_Destroy(output);
        return result;
    }
    
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
    
    /* set the player's state to playing */
    result = (*self->sl_player_play)->SetPlayState(self->sl_player_play, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) {
        ATX_LOG_WARNING_1("SetPlayState failed (%d)", result);
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

    ATX_LOG_FINER("stopping output");

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
    AndroidOutput*       self = ATX_SELF(AndroidOutput, BLT_OutputNode);

    /* default values */
    status->media_time.seconds = 0;
    status->media_time.nanoseconds = 0;
    status->flags = 0;
    status->media_time = BLT_TimeStamp_FromNanos(self->media_time);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AndroidOutput_SetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_SetVolume(BLT_VolumeControl* _self, float volume)
{
    AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_VolumeControl);
    float millibels = self->sl_player_max_volume-((1.0-volume)*BLT_ANDROID_OUTPUT_VOLUME_RANGE);
    ATX_LOG_FINE_1("setting volume to %d millibels", (int)millibels);
    if (self->sl_player_volume) {
        (*self->sl_player_volume)->SetVolumeLevel(self->sl_player_volume, (SLmillibel)millibels);
    }
}

/*----------------------------------------------------------------------
|    AndroidOutput_GetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
AndroidOutput_GetVolume(BLT_VolumeControl* _self, float* volume)
{
    AndroidOutput* self = ATX_SELF(AndroidOutput, BLT_VolumeControl);
    SLmillibel millibels = 0;
    if (self->sl_player_volume) {
        if ((*self->sl_player_volume)->GetVolumeLevel(self->sl_player_volume, &millibels) == SL_RESULT_SUCCESS) {
            *volume = 1.0-((self->sl_player_max_volume-millibels)/BLT_ANDROID_OUTPUT_VOLUME_RANGE);
            if (*volume > 1.0) *volume = 1.0;
            if (*volume < 0.0) *volume = 0.0;
            return BLT_SUCCESS;
        }
    }
    *volume = 0.0;
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
    NULL
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
