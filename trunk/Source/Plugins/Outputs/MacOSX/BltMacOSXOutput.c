/*****************************************************************
|
|   MacOSX Output Module
|
|   (c) 2002-2007 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include <AudioUnit/AudioUnit.h>
#include <pthread.h>
#include <unistd.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltMacOSXOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.macosx")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_MACOSX_OUTPUT_PACKET_QUEUE_SIZE    32     /* packets */
#define BLT_MACOSX_OUTPUT_SLEEP_INTERVAL       100000 /* ms -> 0.1 secs */
#define BLT_MACOSX_OUTPUT_MAX_QUEUE_WAIT_COUNT (5000000/BLT_MACOSX_OUTPUT_SLEEP_INTERVAL) /* 5 secs */

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} MacOSXOutputModule;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    AudioUnit         audio_unit;
    pthread_mutex_t   lock;
    ATX_List*         packet_queue;
    BLT_PcmMediaType  expected_media_type;
    BLT_PcmMediaType  media_type;
    BLT_Boolean       paused;
} MacOSXOutput;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(MacOSXOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(MacOSXOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(MacOSXOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(MacOSXOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(MacOSXOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(MacOSXOutput, BLT_PacketConsumer)

BLT_METHOD MacOSXOutput_Resume(BLT_MediaNode* self);
BLT_METHOD MacOSXOutput_Stop(BLT_MediaNode* self);

/*----------------------------------------------------------------------
|    MacOSXOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
MacOSXOutput_Drain(MacOSXOutput* self)
{
    BLT_COMPILER_UNUSED(self);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_RenderCallback
+---------------------------------------------------------------------*/
static OSStatus     
MacOSXOutput_RenderCallback(void*						inRefCon,
                            AudioUnitRenderActionFlags*	ioActionFlags,
                            const AudioTimeStamp*		inTimeStamp,
                            UInt32						inBusNumber,
                            UInt32						inNumberFrames,
                            AudioBufferList*			ioData)
{
    MacOSXOutput* self = (MacOSXOutput*)inRefCon;
    ATX_ListItem*  item;
    unsigned int   requested = ioData->mBuffers[0].mDataByteSize;
    unsigned char* out = (unsigned char*)ioData->mBuffers[0].mData;
    
    BLT_COMPILER_UNUSED(ioActionFlags);
    BLT_COMPILER_UNUSED(inTimeStamp);
    BLT_COMPILER_UNUSED(inBusNumber);
    BLT_COMPILER_UNUSED(inNumberFrames);
                
    /* sanity check on the parameters */
    if (ioData == NULL || ioData->mNumberBuffers == 0) return 0;
    
    /* lock the packet queue */
    pthread_mutex_lock(&self->lock);
    
    /* return now if we're paused */
    if (self->paused) goto end;
    
    /* abort early if we have no packets */
    if (ATX_List_GetItemCount(self->packet_queue) == 0) goto end;
        
    /* fill as much as we can */
    while (requested && (item = ATX_List_GetFirstItem(self->packet_queue))) {
        BLT_MediaPacket*        packet = ATX_ListItem_GetData(item);
        const BLT_PcmMediaType* media_type;
        BLT_Size                payload_size;
        
        /* get the packet info */
        BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
        
        /* compute how much to copy from this packet */
        payload_size = BLT_MediaPacket_GetPayloadSize(packet);
        if (payload_size <= requested) {
            /* copy the entire payload and remove the packet from the queue */
            ATX_CopyMemory(out, BLT_MediaPacket_GetPayloadBuffer(packet), payload_size);
            ATX_List_RemoveItem(self->packet_queue, item);            
            requested -= payload_size;
            out += payload_size;
        } else {
            /* only copy a portion of the payload */
            ATX_CopyMemory(out, BLT_MediaPacket_GetPayloadBuffer(packet), requested);
            BLT_MediaPacket_SetPayloadOffset(packet, BLT_MediaPacket_GetPayloadOffset(packet)+requested);
            requested = 0;
            out += requested;
        }
    }
   
end:
    /* fill whatever is left with silence */    
    if (requested) {
        ATX_LOG_FINEST_1("MacOSXOutput::RenderCallback - filling with %d bytes of silence", requested);
        ATX_SetMemory(out, 0, requested);
    }
    
    pthread_mutex_unlock(&self->lock);
        
    return 0;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_QueueItemDestructor
+---------------------------------------------------------------------*/
static void 
MacOSXOutput_QueueItemDestructor(ATX_ListDataDestructor* self, 
                                 ATX_Any                 data, 
                                 ATX_UInt32              type)
{
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(type);
    
    BLT_MediaPacket_Release((BLT_MediaPacket*)data);
}

/*----------------------------------------------------------------------
|    MacOSXOutput_QueuePacket
+---------------------------------------------------------------------*/
static BLT_Result
MacOSXOutput_QueuePacket(MacOSXOutput* self, BLT_MediaPacket* packet)
{
    BLT_Result result = BLT_SUCCESS;
    unsigned int watchdog = BLT_MACOSX_OUTPUT_MAX_QUEUE_WAIT_COUNT;
    
    /* lock the queue */
    pthread_mutex_lock(&self->lock);
    
    /* wait for some space in the queue */
    while (ATX_List_GetItemCount(self->packet_queue) >= BLT_MACOSX_OUTPUT_PACKET_QUEUE_SIZE) {
        pthread_mutex_unlock(&self->lock);
        usleep(BLT_MACOSX_OUTPUT_SLEEP_INTERVAL);
        pthread_mutex_lock(&self->lock);
        
        if (--watchdog == 0) {
            ATX_LOG_WARNING("MaxOSXOutput::QueuePacket - *** the watchdog bit us ***");
            goto end;
        }
    }
    
    /* add the packet to the queue */
    ATX_List_AddData(self->packet_queue, packet);
    
    /* keep a reference to the packet */
    BLT_MediaPacket_AddReference(packet);
    
end:
    /* unlock the queue */
    pthread_mutex_unlock(&self->lock);
    
    return result;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_SetStreamFormat
+---------------------------------------------------------------------*/
static BLT_Result
MacOSXOutput_SetStreamFormat(MacOSXOutput*           self, 
                             const BLT_PcmMediaType* media_type)
{
    ComponentResult             result;
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
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* drain any pending packets before we switch */
    result = MacOSXOutput_Drain(self);
    if (BLT_FAILED(result)) return result;

    /* set the audio unit property */
    pthread_mutex_lock(&self->lock);
    result = AudioUnitSetProperty(self->audio_unit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &audio_desc,
                                  sizeof(audio_desc));
    pthread_mutex_unlock(&self->lock);
    if (result != noErr) {
        ATX_LOG_WARNING("MacOSXOutput::SetStreamFormat - AudioUnitSetProperty failed");
        return BLT_FAILURE;
    }
    
    /* copy the format */
    self->media_type = *media_type;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_PutPacket(BLT_PacketConsumer* _self,
                       BLT_MediaPacket*    packet)
{
    MacOSXOutput*           self = ATX_SELF(MacOSXOutput, BLT_PacketConsumer);
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
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

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
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
                        
        /* update the audio unit */
        result = MacOSXOutput_SetStreamFormat(self, media_type);
        if (BLT_FAILED(result)) return result;
    }
    
    /* ensure we're not paused */
    MacOSXOutput_Resume(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));

    /* queue the packet */
    return MacOSXOutput_QueuePacket(self, packet);
}

/*----------------------------------------------------------------------
|    MacOSXOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_QueryMediaType(BLT_MediaPort*        _self,
                            BLT_Ordinal           index,
                            const BLT_MediaType** media_type)
{
    MacOSXOutput* self = ATX_SELF(MacOSXOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
MacOSXOutput_Create(BLT_Module*              module,
                    BLT_Core*                core, 
                    BLT_ModuleParametersType parameters_type,
                    BLT_CString              parameters, 
                    BLT_MediaNode**          object)
{
    MacOSXOutput*        self;
    AudioUnit            audio_unit = NULL;
    Component            component;
    ComponentDescription component_desc;
    ComponentResult      result;
    
    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* get the default output audio unit */
    ATX_SetMemory(&component_desc, 0, sizeof(component_desc));
    component_desc.componentType         = kAudioUnitType_Output;
    component_desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
    component_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    component_desc.componentFlags        = 0;
    component_desc.componentFlagsMask    = 0;
    component = FindNextComponent(NULL, &component_desc);
    if (component == NULL) {
        ATX_LOG_WARNING("MacOSXOutput::Create - FindNextComponent failed");
        return BLT_FAILURE;
    }
    
    /* open the audio unit (we will initialize it later) */
    result = OpenAComponent(component, &audio_unit);
    if (result != noErr) {
        ATX_LOG_WARNING_1("MacOSXOutput::Create - OpenAComponent failed (%d)", result);
        return BLT_FAILURE;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(MacOSXOutput));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */
    self->audio_unit                 = audio_unit;
    self->media_type.sample_rate     = 0;
    self->media_type.channel_count   = 0;
    self->media_type.bits_per_sample = 0;

    /* create a lock */
    pthread_mutex_init(&self->lock, NULL);
    
    /* create the packet queue */
    {
        ATX_ListDataDestructor destructor = { NULL, MacOSXOutput_QueueItemDestructor };
        ATX_List_CreateEx(&destructor, &self->packet_queue);
    }
    
    /* setup the expected media type */
    BLT_PcmMediaType_Init(&self->expected_media_type);
    self->expected_media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, MacOSXOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (self, MacOSXOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE   (self, MacOSXOutput, BLT_OutputNode);
    ATX_SET_INTERFACE   (self, MacOSXOutput, BLT_MediaPort);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
MacOSXOutput_Destroy(MacOSXOutput* self)
{
    /* drain the queue */
    MacOSXOutput_Drain(self);

    /* stop the audio pump */
    MacOSXOutput_Stop(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));
    
    /* close the audio unit */
    if (self->audio_unit) {
        ComponentResult result;
        
        pthread_mutex_lock(&self->lock);
        result = CloseComponent(self->audio_unit);
        pthread_mutex_unlock(&self->lock);
        if (result != noErr) {
            ATX_LOG_WARNING_1("MacOSXOutput::Destroy - CloseComponent failed (%d)", result);
        }
    }
    
    /* destroy the queue */
    ATX_List_Destroy(self->packet_queue);
    
    /* destroy the lock */
    pthread_mutex_destroy(&self->lock);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|   MacOSXOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_GetPortByName(BLT_MediaNode*  _self,
                           BLT_CString     name,
                           BLT_MediaPort** port)
{
    MacOSXOutput* self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Seek(BLT_MediaNode* _self,
                  BLT_SeekMode*  mode,
                  BLT_SeekPoint* point)
{
    MacOSXOutput*   self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult result;
    
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    pthread_mutex_lock(&self->lock);

    /* flush the queue */
    ATX_List_Clear(self->packet_queue);
    
    /* reset the device */
    result = AudioUnitReset(self->audio_unit, kAudioUnitScope_Input, 0);
    if (result != noErr) {
        ATX_LOG_WARNING_1("MacOSXOutput::Stop - AudioUnitReset failed (%d)", result);
    }

    pthread_mutex_unlock(&self->lock);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_GetStatus(BLT_OutputNode*       _self,
                       BLT_OutputNodeStatus* status)
{
    MacOSXOutput* self = ATX_SELF(MacOSXOutput, BLT_OutputNode);
    BLT_COMPILER_UNUSED(self);
    
    /* default value */
    status->delay.seconds     = 0;
    status->delay.nanoseconds = 0;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Start(BLT_MediaNode* _self)
{
    MacOSXOutput*   self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult result;
    
    /* start the audio unit */
    pthread_mutex_lock(&self->lock);
    result = AudioOutputUnitStart(self->audio_unit);
    pthread_mutex_unlock(&self->lock);
    if (result != noErr) {
        ATX_LOG_WARNING_1("MacOSXOutput::Start - AudioUnitOutputStart failed (%d)", result);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Stop(BLT_MediaNode* _self)
{
    MacOSXOutput*   self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult result;
    
    pthread_mutex_lock(&self->lock);

    /* flush the queue */
    ATX_List_Clear(self->packet_queue);

    /* stop the and reset audio unit */
    result = AudioOutputUnitStop(self->audio_unit);
    if (result != noErr) {
        ATX_LOG_WARNING_1("MacOSXOutput::Stop - AudioUnitOutputStop failed (%d)", result);
    }
    result = AudioUnitReset(self->audio_unit, kAudioUnitScope_Input, 0);
    if (result != noErr) {
        ATX_LOG_WARNING_1("MacOSXOutput::Stop - AudioUnitReset failed (%d)", result);
    }
    
    pthread_mutex_unlock(&self->lock);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Pause(BLT_MediaNode* _self)
{
    MacOSXOutput*   self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult result;
    
    if (!self->paused) {
        pthread_mutex_lock(&self->lock);
        self->paused = BLT_TRUE;
        result = AudioOutputUnitStop(self->audio_unit);
        if (result != noErr) {
            ATX_LOG_WARNING_1("MacOSXOutput::Pause - AudioUnitOutputStop failed (%d)", result);
        }
        pthread_mutex_unlock(&self->lock);
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MacOSXOutput_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Resume(BLT_MediaNode* _self)
{
    MacOSXOutput*   self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult result;

    if (self->paused) {
        pthread_mutex_lock(&self->lock);
        self->paused = BLT_FALSE;
        result = AudioOutputUnitStart(self->audio_unit);
        if (result != noErr) {
            ATX_LOG_WARNING_1("MacOSXOutput::Resume - AudioUnitOutputStart failed (%d)", result);
        }
        pthread_mutex_unlock(&self->lock);
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       MacOSXOutput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    MacOSXOutput* self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult        result;
    AURenderCallbackStruct callback;
    
    BLT_COMPILER_UNUSED(stream);
        
    ATX_LOG_FINER("MacOSXOutput::Activate");

    /* initialize the output */
    if (self->audio_unit) {
        result = AudioUnitInitialize(self->audio_unit);
        if (result != noErr) {
            ATX_LOG_WARNING_1("MacOSXOutput::Activate - AudioUnitInitialize failed (%d)", result);
            return BLT_FAILURE;
        }
    }

    /* setup the callback */
    callback.inputProc = MacOSXOutput_RenderCallback;
    callback.inputProcRefCon = _self;
    result = AudioUnitSetProperty(self->audio_unit, 
                                  kAudioUnitProperty_SetRenderCallback, 
                                  kAudioUnitScope_Input, 
                                  0,
                                  &callback, 
                                  sizeof(callback));
    if (result != noErr) {
        ATX_LOG_SEVERE_1("MacOSXOutput::Activate - AudioUnitSetProperty failed when setting callback (%d)", result);
        return BLT_FAILURE;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       MacOSXOutput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutput_Deactivate(BLT_MediaNode* _self)
{
    MacOSXOutput* self = ATX_SELF_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ComponentResult result;

    ATX_LOG_FINER("MacOSXOutput::Deactivate");

    /* un-initialize the device */
    if (self->audio_unit) {
        result = AudioUnitUninitialize(self->audio_unit);
        if (result != noErr) {
            ATX_LOG_WARNING_1("MacOSXOutput::Deactivate - AudioUnitUninitialize failed (%d)", result);
            return BLT_FAILURE;
        }
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(MacOSXOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(MacOSXOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (MacOSXOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT   (MacOSXOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT   (MacOSXOutput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(MacOSXOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(MacOSXOutput, BLT_MediaPort)
    MacOSXOutput_GetName,
    MacOSXOutput_GetProtocol,
    MacOSXOutput_GetDirection,
    MacOSXOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(MacOSXOutput, BLT_PacketConsumer)
    MacOSXOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(MacOSXOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    MacOSXOutput_GetPortByName,
    MacOSXOutput_Activate,
    MacOSXOutput_Deactivate,
    MacOSXOutput_Start,
    MacOSXOutput_Stop,
    MacOSXOutput_Pause,
    MacOSXOutput_Resume,
    MacOSXOutput_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(MacOSXOutput, BLT_OutputNode)
    MacOSXOutput_GetStatus
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(MacOSXOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   MacOSXOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
MacOSXOutputModule_Probe(BLT_Module*              self, 
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

            /* the name should be 'macosx:<n>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "macosx:", 6)) {
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(MacOSXOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(MacOSXOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(MacOSXOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(MacOSXOutputModule, MacOSXOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(MacOSXOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    MacOSXOutputModule_CreateInstance,
    MacOSXOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define MacOSXOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(MacOSXOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_MacOSXOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("MacOSX Output", NULL, 0, 
                                 &MacOSXOutputModule_BLT_ModuleInterface,
                                 &MacOSXOutputModule_ATX_ReferenceableInterface,
                                 object);
}
