/*****************************************************************
|
|   OSX Audio Queue Output Module
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include <AvailabilityMacros.h>
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
#include <AudioToolbox/AudioQueue.h>
#endif
#include <pthread.h>
#include <unistd.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltOsxAudioQueueOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.osx.audio-queue")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT         4
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_SLEEP_INTERVAL       100000 /* us -> 0.1 secs */
#define BLT_OSX_AUDIO_QUEUE_OUTPUT_MAX_QUEUE_WAIT_COUNT (5000000/BLT_OSX_AUDIO_QUEUE_OUTPUT_SLEEP_INTERVAL) /* 5 secs */

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} OsxAudioQueueOutputModule;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    pthread_mutex_t     lock;
    AudioQueueRef       audio_queue;
    BLT_Boolean         audio_queue_started;
    AudioQueueBufferRef buffers[BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT];
    BLT_Ordinal         buffer_index;
    BLT_PcmMediaType    expected_media_type;
    BLT_PcmMediaType    media_type;
    BLT_Boolean         paused;
} OsxAudioQueueOutput;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(OsxAudioQueueOutput, BLT_PacketConsumer)

BLT_METHOD OsxAudioQueueOutput_Resume(BLT_MediaNode* self);
BLT_METHOD OsxAudioQueueOutput_Stop(BLT_MediaNode* self);

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_Drain(OsxAudioQueueOutput* self)
{
    BLT_COMPILER_UNUSED(self);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Callback
+---------------------------------------------------------------------*/
static void
OsxAudioQueueOutput_Callback(void*               _self, 
                             AudioQueueRef       queue, 
                             AudioQueueBufferRef buffer)
{
    OsxAudioQueueOutput* self = (OsxAudioQueueOutput*)_self;
    
    /* mark the buffer as free */
    pthread_mutex_lock(&self->lock);
    buffer->mUserData = NULL;
    buffer->mAudioDataByteSize = 0;
    pthread_mutex_unlock(&self->lock);
    
    ATX_LOG_FINER_1("callback for buffer %x", (int)buffer);
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_SetStreamFormat
+---------------------------------------------------------------------*/
static BLT_Result
OsxAudioQueueOutput_SetStreamFormat(OsxAudioQueueOutput*    self, 
                                    const BLT_PcmMediaType* media_type)
{
    BLT_Result                  result;
    OSStatus                    status;
    AudioStreamBasicDescription audio_desc;
    
    /* setup the audio description */
    audio_desc.mFormatID         = kAudioFormatLinearPCM;
    audio_desc.mFormatFlags      = kAudioFormatFlagIsPacked;
    audio_desc.mFramesPerPacket  = 1;
    audio_desc.mSampleRate       = media_type->sample_rate;
    audio_desc.mChannelsPerFrame = media_type->channel_count;
    audio_desc.mBitsPerChannel   = media_type->bits_per_sample;
    audio_desc.mBytesPerFrame    = (audio_desc.mBitsPerChannel * audio_desc.mChannelsPerFrame) / 8;
    audio_desc.mBytesPerPacket   = audio_desc.mBytesPerFrame * audio_desc.mFramesPerPacket;
    audio_desc.mReserved         = 0;
    
    /* select the sample format */
    switch (media_type->sample_format) {
        case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE:
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
            break;
        
        case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE:
            break;
            
        case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE:
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
            break;
            
        case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE:
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
            break;
            
        case BLT_PCM_SAMPLE_FORMAT_FLOAT_BE:
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsFloat;
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
            break;
            
        case BLT_PCM_SAMPLE_FORMAT_FLOAT_LE:
            audio_desc.mFormatFlags |= kLinearPCMFormatFlagIsFloat;
            break;
            
        default:
            return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* reset any existing queue before we create a new one */
    if (self->audio_queue) {
        /* drain any pending packets before we switch */
        result = OsxAudioQueueOutput_Drain(self);
        if (BLT_FAILED(result)) return result;
        
        /* stop the queue */
        AudioQueueStop(self->audio_queue, true);
        self->audio_queue_started = BLT_FALSE;
        
        /* destroy the queue (this will also free the buffers) */
        AudioQueueDispose(self->audio_queue, true);
        self->audio_queue = NULL;
    }

    /* create an audio queue */
    status = AudioQueueNewOutput(&audio_desc, OsxAudioQueueOutput_Callback,
                                 self,
                                 NULL,
                                 kCFRunLoopCommonModes,
                                 0,
                                 &self->audio_queue);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioQueueNewOutput returned %d", status);
        return BLT_FAILURE;
    }
    
    /* create the buffers */
    {
        unsigned int i;
        unsigned int buffer_size = audio_desc.mBytesPerFrame*audio_desc.mSampleRate/BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT;
        /*buffer_size = 44100*2;*/
        for (i=0; i<BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT; i++) {
            AudioQueueAllocateBuffer(self->audio_queue, buffer_size, &self->buffers[i]);
            self->buffers[i]->mUserData = NULL;
            self->buffers[i]->mAudioDataByteSize = 0;
        }
    }
    
    /* copy the format */
    self->media_type = *media_type;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_WaitForBuffer
+---------------------------------------------------------------------*/
static AudioQueueBufferRef
OsxAudioQueueOutput_WaitForBuffer(OsxAudioQueueOutput* self)
{
    unsigned int watchdog = BLT_OSX_AUDIO_QUEUE_OUTPUT_MAX_QUEUE_WAIT_COUNT;
    
    while (watchdog--) {
        AudioQueueBufferRef buffer = self->buffers[self->buffer_index];
        BLT_Boolean         available = BLT_FALSE;
        
        pthread_mutex_lock(&self->lock);
        if (buffer->mUserData == NULL &&
            buffer->mAudioDataByteSize != buffer->mAudioDataBytesCapacity) {
            available = BLT_TRUE;
        }
        pthread_mutex_unlock(&self->lock);
        
        if (available) return buffer;
        
        /* wait a bit before retrying */
        usleep(BLT_OSX_AUDIO_QUEUE_OUTPUT_SLEEP_INTERVAL);
    }
    
    ATX_LOG_WARNING("the watchdog bit us");
    return NULL;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_PutPacket(BLT_PacketConsumer* _self,
                              BLT_MediaPacket*    packet)
{
    OsxAudioQueueOutput*    self = ATX_SELF(OsxAudioQueueOutput, BLT_PacketConsumer);
    const BLT_PcmMediaType* media_type;
    BLT_Result              result;

    /* check parameters */
    if (packet == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* exit early if the packet is empty */
    if (BLT_MediaPacket_GetPayloadSize(packet) == 0) return BLT_SUCCESS;
    
    /* compare the media format with the current format */
    if (media_type->sample_rate     != self->media_type.sample_rate   ||
        media_type->channel_count   != self->media_type.channel_count ||
        media_type->bits_per_sample != self->media_type.bits_per_sample) {
        /* new format */
        
        /* check the format */
        if (media_type->sample_rate     == 0 ||
            media_type->channel_count   == 0 ||
            media_type->bits_per_sample == 0) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }

        /* check for the supported sample widths */
        if (media_type->bits_per_sample !=  8 &&
            media_type->bits_per_sample != 16 &&
            media_type->bits_per_sample != 24 &&
            media_type->bits_per_sample != 32) {
            return BLT_ERROR_INVALID_MEDIA_TYPE;
        }
                        
        /* update the audio unit */
        result = OsxAudioQueueOutput_SetStreamFormat(self, media_type);
        if (BLT_FAILED(result)) return result;
    }
    
    /* ensure we're not paused */
    OsxAudioQueueOutput_Resume(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));

    /* queue the packet */
    {
        UInt32               chunk;
        const unsigned char* payload = (const unsigned char*)BLT_MediaPacket_GetPayloadBuffer(packet);
        BLT_Size             payload_size = BLT_MediaPacket_GetPayloadSize(packet);

        while (payload_size) {
            /* wait for a buffer */
            AudioQueueBufferRef buffer = OsxAudioQueueOutput_WaitForBuffer(self);
            if (buffer == NULL) return BLT_ERROR_INTERNAL;
            
            /* copy as many bytes as we can into the buffer */
            chunk = buffer->mAudioDataBytesCapacity-buffer->mAudioDataByteSize;
            if (chunk > payload_size) {
                chunk = payload_size;
            }
            ATX_CopyMemory(((char*)buffer->mAudioData)+buffer->mAudioDataByteSize,
                           payload,
                           chunk);
            payload_size               -= chunk;
            payload                    += chunk;
            buffer->mAudioDataByteSize += chunk;
            
            /* queue the buffer if it is full */
            if (buffer->mAudioDataByteSize == buffer->mAudioDataBytesCapacity) {
                OSStatus status;
                
                /* mark the buffer as in-queue */
                buffer->mUserData = self;
                
                /* queue the buffer */
                ATX_LOG_FINER_1("enqueuing buffer %x", (int)buffer);
                status = AudioQueueEnqueueBuffer(self->audio_queue, buffer, 0, NULL);
                if (status != noErr) {
                    ATX_LOG_WARNING_1("AudioQueueEnqueueBuffer returned %x", status);
                }
                
                /* move on to the next buffer */
                self->buffer_index++;
                if (self->buffer_index == BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT) {
                    self->buffer_index = 0;
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
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
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
OsxAudioQueueOutput_Create(BLT_Module*              module,
                           BLT_Core*                core, 
                           BLT_ModuleParametersType parameters_type,
                           BLT_CString              parameters, 
                           BLT_MediaNode**          object)
{
    OsxAudioQueueOutput* self;
    
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
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */
    self->audio_queue                = NULL;
    self->audio_queue_started        = BLT_FALSE;
    self->buffer_index               = 0;
    self->media_type.sample_rate     = 0;
    self->media_type.channel_count   = 0;
    self->media_type.bits_per_sample = 0;

    /* create a lock */
    pthread_mutex_init(&self->lock, NULL);
    
    /* setup the expected media type */
    BLT_PcmMediaType_Init(&self->expected_media_type);
    self->expected_media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, OsxAudioQueueOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (self, OsxAudioQueueOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE   (self, OsxAudioQueueOutput, BLT_OutputNode);
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
    OsxAudioQueueOutput_Drain(self);

    /* stop the audio pump */
    OsxAudioQueueOutput_Stop(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));
    
    /* close the audio queue */
    if (self->audio_queue) {
        AudioQueueDispose(self->audio_queue, true);
    }
        
    /* destroy the lock */
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
    unsigned int i;
    
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* reset the queue */
    AudioQueueReset(self->audio_queue);

    /* reset the buffers */
    for (i=0; i<BLT_OSX_AUDIO_QUEUE_OUTPUT_BUFFER_COUNT; i++) {
        self->buffers[i]->mAudioDataByteSize = 0;
        self->buffers[i]->mUserData = NULL;
    }
    
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
    BLT_COMPILER_UNUSED(self);
    
    /* default value */
    status->delay.seconds     = 0;
    status->delay.nanoseconds = 0;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Start(BLT_MediaNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (self->audio_queue) {
        OSStatus status = AudioQueueStart(self->audio_queue, NULL);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueueStart failed (%x)", status);
        }
        self->audio_queue_started = BLT_TRUE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Stop(BLT_MediaNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (self->audio_queue) {
        OSStatus status = AudioQueueStop(self->audio_queue, true);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueueStop failed (%x)", status);
        }
        self->audio_queue_started = BLT_FALSE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxAudioQueueOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutput_Pause(BLT_MediaNode* _self)
{
    OsxAudioQueueOutput* self = ATX_SELF_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode);
    
    if (self->audio_queue && !self->paused) {
        OSStatus status = AudioQueuePause(self->audio_queue);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueuePause failed (%x)", status);
        }
        self->paused = BLT_TRUE;
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

    if (self->audio_queue && self->paused) {
        OSStatus status = AudioQueueStart(self->audio_queue, NULL);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioQueueStart failed (%x)", status);
        }
        self->paused = BLT_FALSE;
        self->audio_queue_started = BLT_TRUE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxAudioQueueOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxAudioQueueOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (OsxAudioQueueOutput, BLT_OutputNode)
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
    OsxAudioQueueOutput_GetStatus
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxAudioQueueOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   OsxAudioQueueOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
OsxAudioQueueOutputModule_Probe(BLT_Module*              self, 
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
    BLT_BaseModule_Attach,
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
BLT_Result 
BLT_OsxAudioQueueOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("OSX Audio Queue Output", NULL, 0, 
                                 &OsxAudioQueueOutputModule_BLT_ModuleInterface,
                                 &OsxAudioQueueOutputModule_ATX_ReferenceableInterface,
                                 object);
}
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

