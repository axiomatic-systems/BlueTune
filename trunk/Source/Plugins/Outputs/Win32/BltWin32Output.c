/*****************************************************************
|
|      File: BltWin32Output.c
|
|      Win32 Output Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#ifndef STRICT
#define STRICT
#endif

#include <windows.h>
#include <assert.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltDebug.h"
#include "BltWin32Output.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|       options
+---------------------------------------------------------------------*/
#define BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE 

#if defined(BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE)
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
const static GUID  BLT_WIN32_OUTPUT_KSDATAFORMAT_SUBTYPE_PCM = 
    {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
#endif

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32OutputModule)
static const BLT_ModuleInterface Win32OutputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32Output)
static const BLT_MediaNodeInterface Win32Output_BLT_MediaNodeInterface;
static const BLT_MediaPortInterface Win32Output_BLT_MediaPortInterface;
static const BLT_PacketConsumerInterface Win32Output_BLT_PacketConsumerInterface;

BLT_METHOD Win32Output_Resume(BLT_MediaNodeInstance* instance);

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_MediaPacket* media_packet;
    WAVEHDR          wave_header;
} QueueBuffer;

typedef struct {
    BLT_BaseModule base;
} Win32OutputModule;

typedef struct {
    BLT_BaseMediaNode base;
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
Win32Output_FreeQueueItem(Win32Output* output, ATX_ListItem* item)
{
    QueueBuffer* queue_buffer = ATX_ListItem_GetData(item);

    /* unprepare the header */
    if (queue_buffer->wave_header.dwFlags & WHDR_PREPARED) {
        assert(queue_buffer->wave_header.dwFlags & WHDR_DONE);
        waveOutUnprepareHeader(output->device_handle, 
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
    ATX_List_AddItem(output->free_queue.packets, item);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_ReleaseQueueItem
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_ReleaseQueueItem(Win32Output* output, ATX_ListItem* item)
{
    QueueBuffer* queue_buffer = ATX_ListItem_GetData(item);

    /* free the queue item first */
    Win32Output_FreeQueueItem(output, item);

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
Win32Output_WaitForQueueItem(Win32Output* output, ATX_ListItem** item)
{
    ATX_Cardinal watchdog = BLT_WIN32_OUTPUT_QUEUE_WAIT_WATCHDOG;

    *item = ATX_List_GetFirstItem(output->pending_queue.packets);
    if (*item) {
        QueueBuffer* queue_buffer = (QueueBuffer*)ATX_ListItem_GetData(*item);
        DWORD volatile* flags = &queue_buffer->wave_header.dwFlags;
        assert(queue_buffer->wave_header.dwFlags & WHDR_PREPARED);
        while ((*flags & WHDR_DONE) == 0) {
            if (watchdog-- == 0) return BLT_FAILURE;
            Sleep(BLT_WIN32_OUTPUT_QUEUE_WAIT_SLEEP);
        }

        /* pop the item from the pending queue */
        ATX_List_DetachItem(output->pending_queue.packets, *item);
        output->pending_queue.buffered -= queue_buffer->wave_header.dwBufferLength;

        /*BLT_Debug("WaitForQueueItem: pending = %d (%d/%d buff), free = %d\n",
                  ATX_List_GetItemCount(output->pending_queue.packets),
                  output->pending_queue.buffered,
                  output->pending_queue.max_buffered,
                  ATX_List_GetItemCount(output->free_queue.packets));*/
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_RequestQueueItem
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_RequestQueueItem(Win32Output* output, ATX_ListItem** item)
{
    ATX_Cardinal watchdog = BLT_WIN32_OUTPUT_QUEUE_REQUEST_WATCHDOG;

    /* wait to the total pending buffers to be less than the max duration */
    while (output->pending_queue.buffered > 
           output->pending_queue.max_buffered) {
        BLT_Result    result;

        /* wait for the head of the queue to free up */
        result = Win32Output_WaitForQueueItem(output, item);
        if (BLT_FAILED(result)) {
            *item = NULL;
            return result;
        }
        /* the item should not be NULL */
        if (item == NULL) return BLT_ERROR_INTERNAL;

        Win32Output_ReleaseQueueItem(output, *item);

        if (watchdog-- == 0) return BLT_ERROR_INTERNAL;
    }

    /* if there is a buffer available in the free queue, return it */
    *item = ATX_List_GetLastItem(output->free_queue.packets);
    if (*item) {
        ATX_List_DetachItem(output->free_queue.packets, *item);
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
        *item = ATX_List_CreateItem(output->free_queue.packets);
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
Win32Output_Drain(Win32Output* output)
{
    ATX_ListItem* item;
    BLT_Result    result;
    
    /* make sure we're not paused */
    Win32Output_Resume((BLT_MediaNodeInstance*)output);

    do {
        result = Win32Output_WaitForQueueItem(output, &item);
        if (BLT_SUCCEEDED(result) && item != NULL) {
            Win32Output_ReleaseQueueItem(output, item);
        }
    } while (BLT_SUCCEEDED(result) && item != NULL);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Open
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Open(Win32Output* output)
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
    if (output->device_handle) {
        /* the device is already open */
        return BLT_SUCCESS;
    }

    /* fill in format structure */
#if defined(BLT_WIN32_OUTPUT_USE_WAVEFORMATEXTENSIBLE)
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = output->media_type.channel_count;
    format.Format.nSamplesPerSec = output->media_type.sample_rate;
    format.Format.nBlockAlign = output->media_type.channel_count *
                                output->media_type.bits_per_sample/8;
    format.Format.nAvgBytesPerSec = format.Format.nBlockAlign *
                                    format.Format.nSamplesPerSec;
    format.Format.wBitsPerSample = output->media_type.bits_per_sample;
    format.Format.cbSize = 22;
    format.Samples.wValidBitsPerSample = output->media_type.bits_per_sample;
    switch (output->media_type.channel_count) {
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

        case 5:
            format.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
            break;

        case 7:
            format.dwChannelMask = KSAUDIO_SPEAKER_7POINT1;
            break;

        default:
            format.dwChannelMask = SPEAKER_ALL;
    }
    format.SubFormat = BLT_WIN32_OUTPUT_KSDATAFORMAT_SUBTYPE_PCM; 
#else
    format.wFormatTag      = WAVE_FORMAT_PCM;
    format.nChannels       = output->media_type.channel_count; 
    format.nSamplesPerSec  = output->media_type.sample_rate;
    format.nBlockAlign     = output->media_type.channel_count *
                             output->media_type.bits_per_sample/8;
    format.nAvgBytesPerSec = format.nBlockAlign*format.nSamplesPerSec;
    format.wBitsPerSample  = output->media_type.bits_per_sample;
    format.cbSize          = 0;
#endif

    /* try to open the device */
    for (retry = 0; retry < BLT_WIN32_OUTPUT_MAX_OPEN_RETRIES; retry++) {
        mm_result = waveOutOpen(&output->device_handle, 
                                output->device_id, 
                                (const struct tWAVEFORMATEX*)&format,
                                0, 0, WAVE_ALLOWSYNC);
        if (mm_result != MMSYSERR_ALLOCATED) break;
        Sleep(BLT_WIN32_OUTPUT_OPEN_RETRY_SLEEP);
    }

    if (mm_result == MMSYSERR_ALLOCATED) {
        output->device_handle = NULL;
        return BLT_ERROR_DEVICE_BUSY;
    }
    if (mm_result == MMSYSERR_BADDEVICEID || 
        mm_result == MMSYSERR_NODRIVER) {
        output->device_handle = NULL;
        return BLT_ERROR_NO_SUCH_DEVICE;
    }
    if (mm_result == WAVERR_BADFORMAT) {
        output->device_handle = NULL;
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    if (mm_result != MMSYSERR_NOERROR) {
        output->device_handle = NULL;
        return BLT_ERROR_OPEN_FAILED;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Close
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Close(Win32Output* output)
{
    /* shortcut */
    if (output->device_handle == NULL) {
        return BLT_SUCCESS;
    }

    /* wait for all buffers to be played */
    Win32Output_Drain(output);

    /* reset device */
    waveOutReset(output->device_handle);

    /* close the device */
    waveOutClose(output->device_handle);

    /* release all queued packets */
    {
        ATX_ListItem* item;
        while ((item = ATX_List_GetFirstItem(output->pending_queue.packets))) {
            ATX_List_DetachItem(output->pending_queue.packets, item);
            Win32Output_ReleaseQueueItem(output, item);
        }
    }

    /* clear the device handle */
    output->device_handle = NULL;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_PutPacket(BLT_PacketConsumerInstance* instance,
                      BLT_MediaPacket*            packet)
{
    Win32Output*      output = (Win32Output*)instance;
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
    if (media_type->sample_rate     != output->media_type.sample_rate   ||
        media_type->channel_count   != output->media_type.channel_count ||
        media_type->bits_per_sample != output->media_type.bits_per_sample) {
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
        output->media_type = *media_type;

        /* recompute the max queue buffer size */
        output->pending_queue.max_buffered = 
            BLT_WIN32_OUTPUT_MAX_QUEUE_DURATION *
            media_type->sample_rate *
            media_type->channel_count *
            (media_type->bits_per_sample/8);

        /* close the device */
        result = Win32Output_Close(output);
        if (BLT_FAILED(result)) goto failed;
    }

    /* ensure that the device is open */
    result = Win32Output_Open(output);
    if (BLT_FAILED(result)) goto failed;

    /* ensure we're not paused */
    Win32Output_Resume((BLT_MediaNodeInstance*)instance);

    /* wait for space in the queue */
    result = Win32Output_RequestQueueItem(output, &queue_item);
    if (BLT_FAILED(result)) goto failed;
    queue_buffer = ATX_ListItem_GetData(queue_item);

    /* setup the queue element */
    queue_buffer->wave_header.lpData = 
        BLT_MediaPacket_GetPayloadBuffer(packet);
    queue_buffer->wave_header.dwBufferLength = 
        BLT_MediaPacket_GetPayloadSize(packet);
    assert((queue_buffer->wave_header.dwFlags & WHDR_PREPARED) == 0);
    mm_result = waveOutPrepareHeader(output->device_handle, 
                                     &queue_buffer->wave_header, 
                                     sizeof(WAVEHDR));
    if (mm_result != MMSYSERR_NOERROR) {
        goto failed;
    }
    queue_buffer->media_packet = packet;

    /* send the sample buffer to the driver */
    assert((queue_buffer->wave_header.dwFlags & WHDR_DONE) == 0);
    assert(queue_buffer->wave_header.dwFlags & WHDR_PREPARED);
    mm_result = waveOutWrite(output->device_handle, 
                             &queue_buffer->wave_header,
                             sizeof(WAVEHDR));
    if (mm_result != MMSYSERR_NOERROR) {
        goto failed;
    }

    /* queue the packet */
    ATX_List_AddItem(output->pending_queue.packets, queue_item);
    output->pending_queue.buffered += queue_buffer->wave_header.dwBufferLength;

    /* keep a reference to the packet */
    BLT_MediaPacket_AddReference(packet);

    return BLT_SUCCESS;

    failed:
        if (queue_item) {
            Win32Output_FreeQueueItem(output, queue_item);
        }
        return result;
}

/*----------------------------------------------------------------------
|    Win32Output_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_QueryMediaType(BLT_MediaPortInstance* instance,
                           BLT_Ordinal            index,
                           const BLT_MediaType**  media_type)
{
    Win32Output* output = (Win32Output*)instance;

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&output->expected_media_type;
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
                   ATX_Object*              object)
{
    Win32Output* output;
    /*
    BLT_MediaNodeConstructor* constructor = 
    (BLT_MediaNodeConstructor*)parameters; */

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(Win32Output));
    if (output == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&output->base, module, core);

    /* construct the object */
    output->device_id                  = WAVE_MAPPER;
    output->device_handle              = NULL;
    output->media_type.sample_rate     = 0;
    output->media_type.channel_count   = 0;
    output->media_type.bits_per_sample = 0;
    output->pending_queue.buffered     = 0;
    output->pending_queue.max_buffered = 0;
    ATX_List_Create(&output->free_queue.packets);
    ATX_List_Create(&output->pending_queue.packets);

    /* setup the expected media type */
    BLT_PcmMediaType_Init(&output->expected_media_type);
    output->expected_media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)output;
    ATX_INTERFACE(object) = (ATX_Interface*)&Win32Output_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Win32Output_Destroy(Win32Output* output)
{
    /* close the handle */
    Win32Output_Close(output);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&output->base);

    /* free the object memory */
    ATX_FreeMemory(output);

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|       Win32Output_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_GetPortByName(BLT_MediaNodeInstance* instance,
                          BLT_CString            name,
                          BLT_MediaPort*         port)
{
    Win32Output* output = (Win32Output*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)output;
        ATX_INTERFACE(port) = &Win32Output_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    Win32Output_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Seek(BLT_MediaNodeInstance* instance,
                 BLT_SeekMode*          mode,
                 BLT_SeekPoint*         point)
{
    Win32Output* output = (Win32Output*)instance;
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* reset the device */
    waveOutReset(output->device_handle);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_GetStatus(BLT_OutputNodeInstance* instance,
                      BLT_OutputNodeStatus*   status)
{
    /*Win32Output* output = (Win32Output*)instance;*/
    BLT_COMPILER_UNUSED(instance);

    status->delay.seconds = 0;
    status->delay.nanoseconds = 0;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Stop(BLT_MediaNodeInstance* instance)
{
    Win32Output* output = (Win32Output*)instance;
    waveOutReset(output->device_handle);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Pause(BLT_MediaNodeInstance* instance)
{
    Win32Output* output = (Win32Output*)instance;
    if (!output->paused) {
        waveOutPause(output->device_handle);
        output->paused = BLT_TRUE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Win32Output_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
Win32Output_Resume(BLT_MediaNodeInstance* instance)
{
    Win32Output* output = (Win32Output*)instance;
    if (output->paused) {
        waveOutRestart(output->device_handle);
        output->paused = BLT_FALSE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(Win32Output, "input", PACKET, IN)
static const BLT_MediaPortInterface
Win32Output_BLT_MediaPortInterface = {
    Win32Output_GetInterface,
    Win32Output_GetName,
    Win32Output_GetProtocol,
    Win32Output_GetDirection,
    Win32Output_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
Win32Output_BLT_PacketConsumerInterface = {
    Win32Output_GetInterface,
    Win32Output_PutPacket
};

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
Win32Output_BLT_MediaNodeInterface = {
    Win32Output_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    Win32Output_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    Win32Output_Stop,
    Win32Output_Pause,
    Win32Output_Resume,
    Win32Output_Seek
};

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
static const BLT_OutputNodeInterface
Win32Output_BLT_OutputNodeInterface = {
    Win32Output_GetInterface,
    Win32Output_GetStatus
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(Win32Output, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32Output)
ATX_INTERFACE_MAP_ADD(Win32Output, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(Win32Output, BLT_OutputNode)
ATX_INTERFACE_MAP_ADD(Win32Output, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(Win32Output, BLT_PacketConsumer)
ATX_INTERFACE_MAP_ADD(Win32Output, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32Output)

/*----------------------------------------------------------------------
|       Win32OutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
Win32OutputModule_Probe(BLT_ModuleInstance*      instance, 
                        BLT_Core*                core,
                        BLT_ModuleParametersType parameters_type,
                        BLT_AnyConst             parameters,
                        BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(instance);
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
|       template instantiations
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(Win32Output)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface Win32OutputModule_BLT_ModuleInterface = {
    Win32OutputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    Win32OutputModule_CreateInstance,
    Win32OutputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define Win32OutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(Win32OutputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32OutputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32OutputModule) 
ATX_INTERFACE_MAP_ADD(Win32OutputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(Win32OutputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Win32OutputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Win32OutputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Win32 Output", NULL, 0, 
                                 &Win32OutputModule_BLT_ModuleInterface,
                                 object);
}
