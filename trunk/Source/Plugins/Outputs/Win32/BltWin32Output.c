/*****************************************************************
|
|   Win32 Output Module
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#ifndef STRICT
#define STRICT
#endif

#include <windows.h>
#include <assert.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltWin32Output.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.win32")

/*----------------------------------------------------------------------
|   options
+---------------------------------------------------------------------*/
#if !defined(_WIN32_WCE)
#define BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE 
#endif

#if defined(BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE)
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
const static GUID  BLT_WIN32_OUTPUT_KSDATAFORMAT_SUBTYPE_PCM = 
    {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
#endif

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(Win32OutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(Win32Output, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(Win32Output, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(Win32Output, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(Win32Output, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(Win32Output, BLT_PacketConsumer)

BLT_METHOD Win32Output_Resume(BLT_MediaNode* self);

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_MediaPacket* media_packet;
    WAVEHDR          wave_header;
} QueueBuffer;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} Win32OutputModule;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    UINT              device_id;
    HWAVEOUT          device_handle;
    BLT_PcmMediaType  expected_media_type;
    BLT_PcmMediaType  media_type;
    BLT_Boolean       paused;
    struct {
        ATX_List* packets;
    }                 free_queue;
    struct {
        ATX_List*    packets;
        BLT_Size     buffered;
        BLT_Size     max_buffered;
    }                 pending_queue;
} Win32Output;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_WIN32_OUTPUT_MAX_QUEUE_DURATION     3    /* seconds */
#define BLT_WIN32_OUTPUT_MAX_OPEN_RETRIES       10
#define BLT_WIN32_OUTPUT_OPEN_RETRY_SLEEP       30   /* milliseconds */
#define BLT_WIN32_OUTPUT_QUEUE_WAIT_SLEEP       100  /* milliseconds */ 
#define BLT_WIN32_OUTPUT_QUEUE_REQUEST_WATCHDOG 100
#define BLT_WIN32_OUTPUT_QUEUE_WAIT_WATCHDOG    50

/*----------------------------------------------------------------------
|    Win32Output_FreeQueueItem
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_FreeQueueItem(Win32Output* self, ATX_ListItem* item)
{
    QueueBuffer* queue_buffer = ATX_ListItem_GetData(item);

    /* unprepare the header */
    if (queue_buffer->wave_header.dwFlags & WHDR_PREPARED) {
        assert(queue_buffer->wave_header.dwFlags & WHDR_DONE);
        waveOutUnprepareHeader(self->device_handle, 
                               &queue_buffer->wave_header,
                               sizeof(WAVEHDR));
    }

    /* clear the header */
    queue_buffer->wave_header.dwFlags         = 0;
    queue_buffer->wave_header.dwBufferLength  = 0;
    queue_buffer->wave_header.dwBytesRecorded = 0;
    queue_buffer->wave_header.dwLoops         = 0;
    queue_buffer->wave_header.dwUser          = 0;
    queue_buffer->wave_header.lpData          = NULL;
    queue_buffer->wave_header.lpNext          = NULL;
    queue_buffer->wave_header.reserved        = 0;

    /* put the item on the free queue */
    ATX_List_AddItem(self->free_queue.packets, item);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_ReleaseQueueItem
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_ReleaseQueueItem(Win32Output* self, ATX_ListItem* item)
{
    QueueBuffer* queue_buffer = ATX_ListItem_GetData(item);

    /* free the queue item first */
    Win32Output_FreeQueueItem(self, item);

    /* release the media packet */
    /* NOTE: this needs to be done after the call to wavUnprepareHeader    */
    /* because the header's lpData field need to be valid when unpreparing */
    /* the header                                                          */
    if (queue_buffer->media_packet) {
        BLT_MediaPacket_Release(queue_buffer->media_packet);
        queue_buffer->media_packet = NULL;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_WaitForQueueItem
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_WaitForQueueItem(Win32Output* self, ATX_ListItem** item)
{
    ATX_Cardinal watchdog = BLT_WIN32_OUTPUT_QUEUE_WAIT_WATCHDOG;

    *item = ATX_List_GetFirstItem(self->pending_queue.packets);
    if (*item) {
        QueueBuffer* queue_buffer = (QueueBuffer*)ATX_ListItem_GetData(*item);
        DWORD volatile* flags = &queue_buffer->wave_header.dwFlags;
        assert(queue_buffer->wave_header.dwFlags & WHDR_PREPARED);
        while ((*flags & WHDR_DONE) == 0) {
            if (watchdog-- == 0) return BLT_FAILURE;
            Sleep(BLT_WIN32_OUTPUT_QUEUE_WAIT_SLEEP);
        }

        /* pop the item from the pending queue */
        ATX_List_DetachItem(self->pending_queue.packets, *item);
        self->pending_queue.buffered -= queue_buffer->wave_header.dwBufferLength;

        /*BLT_Debug("WaitForQueueItem: pending = %d (%d/%d buff), free = %d\n",
                  ATX_List_GetItemCount(self->pending_queue.packets),
                  self->pending_queue.buffered,
                  self->pending_queue.max_buffered,
                  ATX_List_GetItemCount(self->free_queue.packets));*/
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_RequestQueueItem
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_RequestQueueItem(Win32Output* self, ATX_ListItem** item)
{
    ATX_Cardinal watchdog = BLT_WIN32_OUTPUT_QUEUE_REQUEST_WATCHDOG;

    /* wait to the total pending buffers to be less than the max duration */
    while (self->pending_queue.buffered > 
           self->pending_queue.max_buffered) {
        BLT_Result    result;

        /* wait for the head of the queue to free up */
        result = Win32Output_WaitForQueueItem(self, item);
        if (BLT_FAILED(result)) {
            *item = NULL;
            return result;
        }
        /* the item should not be NULL */
        if (item == NULL) return BLT_ERROR_INTERNAL;

        Win32Output_ReleaseQueueItem(self, *item);

        if (watchdog-- == 0) return BLT_ERROR_INTERNAL;
    }

    /* if there is a buffer available in the free queue, return it */
    *item = ATX_List_GetLastItem(self->free_queue.packets);
    if (*item) {
        ATX_List_DetachItem(self->free_queue.packets, *item);
        return BLT_SUCCESS;
    }

    /* we get here is there ware no buffer in the free queue */
    {
        QueueBuffer* queue_buffer;
        queue_buffer = (QueueBuffer*)ATX_AllocateZeroMemory(sizeof(QueueBuffer));
        if (queue_buffer == NULL) {
            *item = NULL;
            return BLT_ERROR_OUT_OF_MEMORY;
        }
        *item = ATX_List_CreateItem(self->free_queue.packets);
        if (*item == NULL) {
            ATX_FreeMemory(queue_buffer);
            return BLT_ERROR_OUT_OF_MEMORY;
        }
        ATX_ListItem_SetData(*item, (ATX_Any)queue_buffer);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Drain
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Drain(Win32Output* self)
{
    ATX_ListItem* item;
    BLT_Result    result;
    
    /* make sure we're not paused */
    Win32Output_Resume(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));

    do {
        result = Win32Output_WaitForQueueItem(self, &item);
        if (BLT_SUCCEEDED(result) && item != NULL) {
            Win32Output_ReleaseQueueItem(self, item);
        }
    } while (BLT_SUCCEEDED(result) && item != NULL);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Open
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Open(Win32Output* self)
{
    MMRESULT     mm_result;
    BLT_Cardinal retry;

#if defined(BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE)
    /* used for 24 and 32 bits per sample */
    WAVEFORMATEXTENSIBLE format;
#else
    WAVEFORMATEX format;
#endif

    /* check current state */
    if (self->device_handle) {
        /* the device is already open */
        return BLT_SUCCESS;
    }

    /* fill in format structure */
#if defined(BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE)
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = self->media_type.channel_count;
    format.Format.nSamplesPerSec = self->media_type.sample_rate;
    format.Format.nBlockAlign = self->media_type.channel_count *
                                self->media_type.bits_per_sample/8;
    format.Format.nAvgBytesPerSec = format.Format.nBlockAlign *
                                    format.Format.nSamplesPerSec;
    format.Format.wBitsPerSample = self->media_type.bits_per_sample;
    format.Format.cbSize = 22;
    format.Samples.wValidBitsPerSample = self->media_type.bits_per_sample;
    if (self->media_type.channel_mask && self->media_type.channel_count > 2) {
        format.dwChannelMask = self->media_type.channel_mask;
    } else {
        switch (self->media_type.channel_count) {
            case 1:
                format.dwChannelMask = KSAUDIO_SPEAKER_MONO;
                break;

            case 2:
                format.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
                break;

            case 3:
                format.dwChannelMask = KSAUDIO_SPEAKER_STEREO |
                                       SPEAKER_FRONT_CENTER;
                break;

            case 4:
                format.dwChannelMask = KSAUDIO_SPEAKER_QUAD;
                break;

            case 6:
                format.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
                break;

            case 8:
                format.dwChannelMask = KSAUDIO_SPEAKER_7POINT1;
                break;

            default:
                format.dwChannelMask = SPEAKER_ALL;
        }
    }
    format.SubFormat = BLT_WIN32_OUTPUT_KSDATAFORMAT_SUBTYPE_PCM; 
#else
    format.wFormatTag      = WAVE_FORMAT_PCM;
    format.nChannels       = self->media_type.channel_count; 
    format.nSamplesPerSec  = self->media_type.sample_rate;
    format.nBlockAlign     = self->media_type.channel_count *
                             self->media_type.bits_per_sample/8;
    format.nAvgBytesPerSec = format.nBlockAlign*format.nSamplesPerSec;
    format.wBitsPerSample  = self->media_type.bits_per_sample;
    format.cbSize          = 0;
#endif

    /* try to open the device */
    for (retry = 0; retry < BLT_WIN32_OUTPUT_MAX_OPEN_RETRIES; retry++) {
        mm_result = waveOutOpen(&self->device_handle, 
                                self->device_id, 
                                (const struct tWAVEFORMATEX*)&format,
                                0, 0, WAVE_ALLOWSYNC);
        if (mm_result != MMSYSERR_ALLOCATED) break;
        Sleep(BLT_WIN32_OUTPUT_OPEN_RETRY_SLEEP);
    }

    if (mm_result == MMSYSERR_ALLOCATED) {
        self->device_handle = NULL;
        return BLT_ERROR_DEVICE_BUSY;
    }
    if (mm_result == MMSYSERR_BADDEVICEID || 
        mm_result == MMSYSERR_NODRIVER) {
        self->device_handle = NULL;
        return BLT_ERROR_NO_SUCH_DEVICE;
    }
    if (mm_result == WAVERR_BADFORMAT) {
        self->device_handle = NULL;
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    if (mm_result != MMSYSERR_NOERROR) {
        self->device_handle = NULL;
        return BLT_ERROR_OPEN_FAILED;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Close
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Close(Win32Output* self)
{
    /* shortcut */
    if (self->device_handle == NULL) {
        return BLT_SUCCESS;
    }

    /* wait for all buffers to be played */
    Win32Output_Drain(self);

    /* reset device */
    waveOutReset(self->device_handle);

    /* close the device */
    waveOutClose(self->device_handle);

    /* release all queued packets */
    {
        ATX_ListItem* item;
        while ((item = ATX_List_GetFirstItem(self->pending_queue.packets))) {
            ATX_List_DetachItem(self->pending_queue.packets, item);
            Win32Output_ReleaseQueueItem(self, item);
        }
    }

    /* clear the device handle */
    self->device_handle = NULL;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_PutPacket(BLT_PacketConsumer* _self,
                      BLT_MediaPacket*    packet)
{
    Win32Output*      self = ATX_SELF(Win32Output, BLT_PacketConsumer);
    BLT_PcmMediaType* media_type;
    QueueBuffer*      queue_buffer = NULL;
    ATX_ListItem*     queue_item = NULL;
    BLT_Result        result;
    MMRESULT          mm_result;

    /* check parameters */
    if (packet == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (BLT_MediaType**)&media_type);
    if (BLT_FAILED(result)) goto failed;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        result = BLT_ERROR_INVALID_MEDIA_FORMAT;
        goto failed;
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
        
        /* perform basic validity checks of the format */
        if (media_type->sample_format != BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        if (media_type->bits_per_sample != 8 &&
            media_type->bits_per_sample != 16 &&
            media_type->bits_per_sample != 24 &&
            media_type->bits_per_sample != 32) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }

        /* copy the format */
        self->media_type = *media_type;

        /* recompute the max queue buffer size */
        self->pending_queue.max_buffered = 
            BLT_WIN32_OUTPUT_MAX_QUEUE_DURATION *
            media_type->sample_rate *
            media_type->channel_count *
            (media_type->bits_per_sample/8);

        /* close the device */
        result = Win32Output_Close(self);
        if (BLT_FAILED(result)) goto failed;
    }

    /* ensure that the device is open */
    result = Win32Output_Open(self);
    if (BLT_FAILED(result)) goto failed;

    /* ensure we're not paused */
    Win32Output_Resume(&ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode));

    /* wait for space in the queue */
    result = Win32Output_RequestQueueItem(self, &queue_item);
    if (BLT_FAILED(result)) goto failed;
    queue_buffer = ATX_ListItem_GetData(queue_item);

    /* setup the queue element */
    queue_buffer->wave_header.lpData = 
        BLT_MediaPacket_GetPayloadBuffer(packet);
    queue_buffer->wave_header.dwBufferLength = 
        BLT_MediaPacket_GetPayloadSize(packet);
    assert((queue_buffer->wave_header.dwFlags & WHDR_PREPARED) == 0);
    mm_result = waveOutPrepareHeader(self->device_handle, 
                                     &queue_buffer->wave_header, 
                                     sizeof(WAVEHDR));
    if (mm_result != MMSYSERR_NOERROR) {
        goto failed;
    }
    queue_buffer->media_packet = packet;

    /* send the sample buffer to the driver */
    assert((queue_buffer->wave_header.dwFlags & WHDR_DONE) == 0);
    assert(queue_buffer->wave_header.dwFlags & WHDR_PREPARED);
    mm_result = waveOutWrite(self->device_handle, 
                             &queue_buffer->wave_header,
                             sizeof(WAVEHDR));
    if (mm_result != MMSYSERR_NOERROR) {
        goto failed;
    }

    /* queue the packet */
    ATX_List_AddItem(self->pending_queue.packets, queue_item);
    self->pending_queue.buffered += queue_buffer->wave_header.dwBufferLength;

    /* keep a reference to the packet */
    BLT_MediaPacket_AddReference(packet);

    return BLT_SUCCESS;

    failed:
        if (queue_item) {
            Win32Output_FreeQueueItem(self, queue_item);
        }
        return result;
}

/*----------------------------------------------------------------------
|    Win32Output_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_QueryMediaType(BLT_MediaPort*        _self,
                           BLT_Ordinal           index,
                           const BLT_MediaType** media_type)
{
    Win32Output* self = ATX_SELF(Win32Output, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    Win32Output_Create
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Create(BLT_Module*              module,
                   BLT_Core*                core, 
                   BLT_ModuleParametersType parameters_type,
                   BLT_CString              parameters, 
                   BLT_MediaNode**          object)
{
    Win32Output* self;
    /*
    BLT_MediaNodeConstructor* constructor = 
    (BLT_MediaNodeConstructor*)parameters; */

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(Win32Output));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */
    self->device_id                  = WAVE_MAPPER;
    self->device_handle              = NULL;
    self->media_type.sample_rate     = 0;
    self->media_type.channel_count   = 0;
    self->media_type.bits_per_sample = 0;
    self->pending_queue.buffered     = 0;
    self->pending_queue.max_buffered = 0;
    ATX_List_Create(&self->free_queue.packets);
    ATX_List_Create(&self->pending_queue.packets);

    /* setup the expected media type */
    BLT_PcmMediaType_Init(&self->expected_media_type);
    self->expected_media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, Win32Output, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, Win32Output, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(self, Win32Output, BLT_PacketConsumer);
    ATX_SET_INTERFACE(self, Win32Output, BLT_OutputNode);
    ATX_SET_INTERFACE(self, Win32Output, BLT_MediaPort);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Destroy(Win32Output* self)
{
    /* close the handle */
    Win32Output_Close(self);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|   Win32Output_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_GetPortByName(BLT_MediaNode*  _self,
                          BLT_CString     name,
                          BLT_MediaPort** port)
{
    Win32Output* self = ATX_SELF_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    Win32Output_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Seek(BLT_MediaNode* _self,
                 BLT_SeekMode*  mode,
                 BLT_SeekPoint* point)
{
    Win32Output* self = ATX_SELF_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* reset the device */
    waveOutReset(self->device_handle);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_GetStatus(BLT_OutputNode*       self,
                      BLT_OutputNodeStatus* status)
{
    /*Win32Output* self = (Win32Output*)instance;*/
    BLT_COMPILER_UNUSED(self);

    status->delay.seconds = 0;
    status->delay.nanoseconds = 0;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Stop(BLT_MediaNode* _self)
{
    Win32Output* self = ATX_SELF_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode);
    waveOutReset(self->device_handle);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Pause(BLT_MediaNode* _self)
{
    Win32Output* self = ATX_SELF_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode);
    if (!self->paused) {
        waveOutPause(self->device_handle);
        self->paused = BLT_TRUE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Resume(BLT_MediaNode* _self)
{
    Win32Output* self = ATX_SELF_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode);
    if (self->paused) {
        waveOutRestart(self->device_handle);
        self->paused = BLT_FALSE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Win32Output)
    ATX_GET_INTERFACE_ACCEPT_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(Win32Output, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(Win32Output, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT(Win32Output, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(Win32Output, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(Win32Output, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(Win32Output, BLT_MediaPort)
    Win32Output_GetName,
    Win32Output_GetProtocol,
    Win32Output_GetDirection,
    Win32Output_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Win32Output, BLT_PacketConsumer)
    Win32Output_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(Win32Output, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    Win32Output_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    Win32Output_Stop,
    Win32Output_Pause,
    Win32Output_Resume,
    Win32Output_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Win32Output, BLT_OutputNode)
    Win32Output_GetStatus
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(Win32Output, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   Win32OutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
Win32OutputModule_Probe(BLT_Module*              self, 
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

            /* the name should be 'wave:<name>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "wave:", 5)) {
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Win32OutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(Win32OutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(Win32OutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(Win32OutputModule, Win32Output)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(Win32OutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    Win32OutputModule_CreateInstance,
    Win32OutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define Win32OutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(Win32OutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Win32OutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Win32 Output", NULL, 0, 
                                 &Win32OutputModule_BLT_ModuleInterface,
                                 &Win32OutputModule_ATX_ReferenceableInterface,
                                 object);
}
