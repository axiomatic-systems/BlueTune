/*****************************************************************
|
|      QNX Output Module
|
|      (c) 2002-2013 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <sys/asoundlib.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltQnxOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.qnx")

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(QnxOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(QnxOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(QnxOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(QnxOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(QnxOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(QnxOutput, BLT_PacketConsumer)

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_QNX_DEFAULT_BUFFER_TIME    500000 /* 0.5 secs */
#define BLT_QNX_DEFAULT_PERIOD_SIZE    4096

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} QnxOutputModule;

typedef enum {
    BLT_QNX_OUTPUT_STATE_CLOSED,
    BLT_QNX_OUTPUT_STATE_OPEN,
    BLT_QNX_OUTPUT_STATE_PREPARED
} QnxOutputState;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    QnxOutputState   state;
    ATX_String       device_name;
    snd_pcm_t*       device_handle;
    snd_pcm_info_t   device_info;
    BLT_PcmMediaType expected_media_type;
    BLT_PcmMediaType media_type;
    ATX_UInt64       media_time;      /* media time of the last received packet       */
    ATX_UInt64       next_media_time; /* media time just pas the last received packet */
} QnxOutput;

/*----------------------------------------------------------------------
|    prototypes
+---------------------------------------------------------------------*/
static BLT_Result QnxOutput_Close(QnxOutput* self);

/*----------------------------------------------------------------------
|    QnxOutput_SetState
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_SetState(QnxOutput* self, QnxOutputState state)
{
    if (state != self->state) {
        ATX_LOG_FINER_2("state changed from %d to %d", self->state, state);
    }
    self->state = state;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_Open
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Open(QnxOutput* self)
{
    int io_result;

    ATX_LOG_FINE_1("openning output - name=%s", ATX_CSTR(self->device_name));

    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_CLOSED:
        ATX_LOG_FINER("snd_pcm_open");
        if (ATX_String_IsEmpty(&self->device_name)) {
            int rcard = 0;
            int rdevice = 0;
            io_result = snd_pcm_open_preferred(&self->device_handle, 
                                               &rcard, 
                                               &rdevice, 
                                               SND_PCM_OPEN_PLAYBACK);
        } else {
            io_result = snd_pcm_open_name(&self->device_handle,
                                          ATX_CSTR(self->device_name),
                                          SND_PCM_OPEN_PLAYBACK);
        }
        if (io_result != 0) {
            ATX_LOG_WARNING_1("snd_pcm_open failed (%s)", snd_strerror(io_result));
            self->device_handle = NULL;
            return BLT_FAILURE;
        }
        
        /* get the device info */
        io_result = snd_pcm_info(self->device_handle, &self->device_info);
        if (io_result != 0) {
            ATX_LOG_WARNING_1("snd_pcm_info failed: (%s)\n", snd_strerror(io_result));
        }
        break;

      case BLT_QNX_OUTPUT_STATE_OPEN:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_QNX_OUTPUT_STATE_PREPARED:
        return BLT_ERROR_INVALID_STATE;
    }

    /* update the state */
    QnxOutput_SetState(self, BLT_QNX_OUTPUT_STATE_OPEN);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_Close
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Close(QnxOutput* self)
{
    ATX_LOG_FINER("closing output");

    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_CLOSED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_QNX_OUTPUT_STATE_PREPARED:
        /* wait for buffers to finish */
        ATX_LOG_FINER("calling snd_pcm_plugin_flush");
        snd_pcm_plugin_flush(self->device_handle, SND_PCM_CHANNEL_PLAYBACK);
        /* FALLTHROUGH */

      case BLT_QNX_OUTPUT_STATE_OPEN:
        /* close the device */
        ATX_LOG_FINER("snd_pcm_close");
        snd_pcm_close(self->device_handle);
        self->device_handle = NULL;
        break;
    }

    /* update the state */
    QnxOutput_SetState(self, BLT_QNX_OUTPUT_STATE_CLOSED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Drain(QnxOutput* self)
{
    ATX_LOG_FINER("draining output");

    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_CLOSED:
      case BLT_QNX_OUTPUT_STATE_OPEN:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_QNX_OUTPUT_STATE_PREPARED:
        /* drain samples buffered by the driver (wait until they are played) */
        ATX_LOG_FINER("snd_pcm_drain");
        snd_pcm_plugin_flush(self->device_handle, SND_PCM_CHANNEL_PLAYBACK);
        break;
    }
    
    /* update the state */
    QnxOutput_SetState(self, BLT_QNX_OUTPUT_STATE_OPEN);    
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_Reset
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Reset(QnxOutput* self)
{
    ATX_LOG_FINER("resetting output");

    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_CLOSED:
      case BLT_QNX_OUTPUT_STATE_OPEN:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_QNX_OUTPUT_STATE_PREPARED:
        ATX_LOG_FINER("snd_pcm_plugin_playback_drain");
        snd_pcm_plugin_playback_drain(self->device_handle);
        QnxOutput_SetState(self, BLT_QNX_OUTPUT_STATE_OPEN);
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_Configure
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Configure(QnxOutput*              self, 
                    const BLT_PcmMediaType* format)
{
    snd_pcm_channel_params_t params;
    snd_pcm_channel_info_t   info;
    int                      io_result;
    BLT_Result               result;

    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_CLOSED:
        /* first, we need to open the device */
        result = QnxOutput_Open(self);
        if (BLT_FAILED(result)) return result;

        /* FALLTHROUGH */

      case BLT_QNX_OUTPUT_STATE_PREPARED:
        /* check to see if the format has changed */
        if (format->sample_rate     != self->media_type.sample_rate   ||
            format->channel_count   != self->media_type.channel_count ||
            format->bits_per_sample != self->media_type.bits_per_sample) {
            /* new format */

            /* check the format */
            if (format->sample_rate     == 0 ||
                format->channel_count   == 0 ||
                format->bits_per_sample == 0) {
                return BLT_ERROR_INVALID_MEDIA_FORMAT;
            }        
            
            /* drain any pending samples */
            QnxOutput_Drain(self);
        } else {
            /* same format, do nothing */
            return BLT_SUCCESS;
        }
        
        /* FALLTHROUGH */

      case BLT_QNX_OUTPUT_STATE_OPEN:
        /* configure the device with the new format */
        ATX_LOG_FINER("configuring device");

        /* copy the format */
        self->media_type = *format;

        ATX_LOG_FINE_3("new format: sr=%d, ch=%d, bps=%d",
                       format->sample_rate,
                       format->channel_count,
                       format->bits_per_sample);

        /* get the current info */
        ATX_SetMemory(&info, 0, sizeof(info));
        info.channel = SND_PCM_CHANNEL_PLAYBACK;
        io_result = snd_pcm_plugin_info(self->device_handle, &info);
        if (io_result < 0) {
            ATX_LOG_WARNING_1("snd_pcm_plugin_info failed (%s)\n", snd_strerror(io_result));
            return BLT_FAILURE;
        }
    
        /* setup the params */
        ATX_SetMemory(&params, 0, sizeof(params));
        params.channel             = SND_PCM_CHANNEL_PLAYBACK;
        params.mode                = SND_PCM_MODE_BLOCK;
        params.start_mode          = SND_PCM_START_FULL; /* start when the whole queue is filled */
        params.stop_mode           = SND_PCM_STOP_ROLLOVER;
        params.buf.block.frag_size = info.max_fragment_size;
        params.buf.block.frags_min = 1;
        params.buf.block.frags_max = -1;
        
        /* set the sample format */
        params.format.interleave   = 1;
        params.format.rate         = format->sample_rate;
        params.format.voices       = format->channel_count;
        switch (format->sample_format) {
        	case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE:
                ATX_LOG_FINE("sample format is BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE");
        		switch (format->bits_per_sample) {
        			case  8: params.format.format = SND_PCM_SFMT_S8;     break;
        			case 16: params.format.format = SND_PCM_SFMT_S16_LE; break;
        			case 24: params.format.format = SND_PCM_SFMT_S24_LE; break;
        			case 32: params.format.format = SND_PCM_SFMT_S32_LE; break;
        		}
        		break;
        		
        	case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE:
                ATX_LOG_FINE("sample format is BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE");
        		switch (format->bits_per_sample) {
        			case  8: params.format.format = SND_PCM_SFMT_U8;     break;
        			case 16: params.format.format = SND_PCM_SFMT_U16_LE; break;
        			case 24: params.format.format = SND_PCM_SFMT_U24_LE; break;
        			case 32: params.format.format = SND_PCM_SFMT_U32_LE; break;
        		}
        		break;

        	case BLT_PCM_SAMPLE_FORMAT_FLOAT_LE:
                ATX_LOG_FINE("sample format is BLT_PCM_SAMPLE_FORMAT_FLOAT_LE");
        		switch (format->bits_per_sample) {
        			case 32: params.format.format = SND_PCM_SFMT_FLOAT_LE; break;
        		}
        		break;

        	case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE:
                ATX_LOG_FINE("sample format is BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE");
        		switch (format->bits_per_sample) {
        			case  8: params.format.format = SND_PCM_SFMT_S8;     break;
        			case 16: params.format.format = SND_PCM_SFMT_S16_BE; break;
        			case 24: params.format.format = SND_PCM_SFMT_S24_BE; break;
        			case 32: params.format.format = SND_PCM_SFMT_S32_BE; break;
        		}
        		break;
        		
        	case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE:
                ATX_LOG_FINE("sample format is BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE");
        		switch (format->bits_per_sample) {
        			case  8: params.format.format = SND_PCM_SFMT_U8;     break;
        			case 16: params.format.format = SND_PCM_SFMT_U16_BE; break;
        			case 24: params.format.format = SND_PCM_SFMT_U24_BE; break;
        			case 32: params.format.format = SND_PCM_SFMT_U32_BE; break;
        		}
        		break;

        	case BLT_PCM_SAMPLE_FORMAT_FLOAT_BE:
                ATX_LOG_FINE("sample format is BLT_PCM_SAMPLE_FORMAT_FLOAT_LE");
        		switch (format->bits_per_sample) {
        			case 32: params.format.format = SND_PCM_SFMT_FLOAT_BE; break;
        		}
        		break;
        }

        /* configure the device */
        ATX_LOG_FINE("configuring device");
        io_result = snd_pcm_plugin_params(self->device_handle, &params);
        if (io_result != 0) {
            ATX_LOG_WARNING_1("snd_pcm_plugin_params failed (%s)", snd_strerror(io_result));
            return BLT_FAILURE;
        }
        ATX_LOG_FINE("preparing device");
        io_result = snd_pcm_plugin_prepare(self->device_handle, SND_PCM_CHANNEL_PLAYBACK);
        if (io_result != 0) {
            ATX_LOG_WARNING_1("snd_pcm_plugin_prepare failed (%s)", snd_strerror(io_result));
            return BLT_FAILURE;
        }
        
        /* copy the format */
        self->media_type = *format;
        
        /* get the actual parameters */
        {
            snd_pcm_channel_setup_t setup;
            ATX_SetMemory(&setup, 0, sizeof(setup));
            setup.channel = SND_PCM_CHANNEL_PLAYBACK;
            io_result = snd_pcm_channel_setup(self->device_handle, &setup);
            if (io_result == 0) {
                ATX_LOG_FINE_1("frag_size = %d", setup.buf.block.frag_size);
                ATX_LOG_FINE_1("frags     = %d", setup.buf.block.frags);
                ATX_LOG_FINE_1("frags_min = %d", setup.buf.block.frags_min);
                ATX_LOG_FINE_1("frags_max = %d", setup.buf.block.frags_max);
            }
        }

        break;
    }

    /* update the state */
    QnxOutput_SetState(self, BLT_QNX_OUTPUT_STATE_PREPARED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_PutPacket(BLT_PacketConsumer* _self,
                    BLT_MediaPacket*    packet)
{
    QnxOutput*              self = ATX_SELF(QnxOutput, BLT_PacketConsumer);
    const BLT_PcmMediaType* media_type;
    BLT_ByteBuffer          buffer;
    BLT_Size                size;
    BLT_Result              result;
    int                     io_result;

    /* check parameters */
    if (packet == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* check the payload buffer and size */
    buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
    size = BLT_MediaPacket_GetPayloadSize(packet);
    if (size == 0) return BLT_SUCCESS;

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)(const void*)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* configure the device for this format */
    result = QnxOutput_Configure(self, media_type);
	if (BLT_FAILED(result)) return result;
	
    /* update the media time */
    {
        BLT_TimeStamp ts = BLT_MediaPacket_GetTimeStamp(packet);
        ATX_UInt64    ts_nanos = BLT_TimeStamp_ToNanos(ts);
        BLT_TimeStamp packet_duration;
        if (media_type->sample_rate   && 
            media_type->channel_count && 
            media_type->bits_per_sample) {
            unsigned int sample_count = BLT_MediaPacket_GetPayloadSize(packet)/
                                        (media_type->channel_count*media_type->bits_per_sample/8);
            packet_duration = BLT_TimeStamp_FromSamples(sample_count, media_type->sample_rate);            
        } else {
            packet_duration = BLT_TimeStamp_FromSeconds(0);
        }
        if (ts_nanos == 0) {
            self->media_time = self->next_media_time;
        } else {
            self->media_time = ts_nanos;
        }
        self->next_media_time = self->media_time+BLT_TimeStamp_ToNanos(packet_duration);
    }
    
    {
        static long counter = 0;
        ATX_TimeInterval sleep = {0, 100000000};
        counter += size;
        printf("WROTE %u\n", counter);
        ATX_System_Sleep(&sleep);
    }
    
    /* write the audio samples */
    io_result = snd_pcm_plugin_write(self->device_handle, buffer, size);
    if (io_result != (int)size) {
        snd_pcm_channel_status_t status;
        ATX_SetMemory(&status, 0, sizeof(status));
        status.channel = SND_PCM_CHANNEL_PLAYBACK;
        ATX_LOG_WARNING_1("snd_pcm_plugin_write failed (io_result = %d)", io_result);
        io_result = snd_pcm_plugin_status(self->device_handle, &status);
        if (io_result == 0) {
            ATX_LOG_WARNING_1("status = %d", status.status);
        }
        return BLT_FAILURE;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_QueryMediaType(BLT_MediaPort*        _self,
                          BLT_Ordinal           index,
                          const BLT_MediaType** media_type)
{
    QnxOutput* self = ATX_SELF(QnxOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    QnxOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_AnyConst             parameters, 
                 BLT_MediaNode**          object)
{
    QnxOutput*               output;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;

    ATX_LOG_FINE("creating output");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(QnxOutput));
    if (output == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(output, BLT_BaseMediaNode), module, core);

    /* construct the object */
    output->state                      = BLT_QNX_OUTPUT_STATE_CLOSED;
    output->device_handle              = NULL;
    output->media_type.sample_rate     = 0;
    output->media_type.channel_count   = 0;
    output->media_type.bits_per_sample = 0;

    /* parse the name */
    if (constructor->name == NULL ||
        ATX_StringsEqual(constructor->name, "qnx:default") ||
        ATX_StringLength(constructor->name) <= 4) {
        ATX_LOG_FINE("using default qnx output");
        ATX_INIT_STRING(output->device_name);
    } else {
        ATX_LOG_FINE_1("using named qnx output: %s", constructor->name+4);
        output->device_name = ATX_String_Create(constructor->name+4);
    }
    
    /* setup the expected media type */
    BLT_PcmMediaType_Init(&output->expected_media_type);

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(output, QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(output, QnxOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(output, QnxOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE(output, QnxOutput, BLT_OutputNode);
    ATX_SET_INTERFACE(output, QnxOutput, BLT_MediaPort);
    *object = &ATX_BASE_EX(output, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
QnxOutput_Destroy(QnxOutput* self)
{
    ATX_LOG_FINE("destroying output");

    /* close the device */
    QnxOutput_Close(self);

    /* free the name */
    ATX_String_Destruct(&self->device_name);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       QnxOutput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(stream);
        
    ATX_LOG_FINER("activating output");

    /* open the device */
    QnxOutput_Open(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       QnxOutput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_Deactivate(BLT_MediaNode* _self)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_FINER("deactivating output");

    /* close the device */
    QnxOutput_Close(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       QnxOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_Stop(BLT_MediaNode* _self)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_FINER("stopping output");

    /* reset the device */
    QnxOutput_Reset(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       QnxOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_Pause(BLT_MediaNode* _self)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);
        
    ATX_LOG_FINER("pausing output");

    /* pause the device */
    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_PREPARED:
        snd_pcm_playback_pause(self->device_handle);
        break;

      default:
        /* ignore */
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       QnxOutput_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_Resume(BLT_MediaNode* _self)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);
        
    ATX_LOG_FINER("resuming output");

    /* pause the device */
    switch (self->state) {
      case BLT_QNX_OUTPUT_STATE_PREPARED:
        snd_pcm_playback_resume(self->device_handle);
        break;

      default:
        /* ignore */
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   QnxOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_GetPortByName(BLT_MediaNode*  _self,
                        BLT_CString     name,
                        BLT_MediaPort** port)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    QnxOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_Seek(BLT_MediaNode* _self,
               BLT_SeekMode*  mode,
               BLT_SeekPoint* point)
{
    QnxOutput* self = ATX_SELF_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* ignore unless we're prepared */
    if (self->state != BLT_QNX_OUTPUT_STATE_PREPARED) {
        return BLT_SUCCESS;
    }

    /* reset the device */
    QnxOutput_Reset(self);

    /* update the media time */
    if (point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) {
        self->media_time = BLT_TimeStamp_ToNanos(point->time_stamp);
        self->next_media_time = self->media_time;
    } else {
        self->media_time = 0;
        self->next_media_time = 0;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    QnxOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutput_GetStatus(BLT_OutputNode*       _self,
                    BLT_OutputNodeStatus* status)
{
#if 0
    QnxOutput*       self = ATX_SELF(QnxOutput, BLT_OutputNode);
    snd_pcm_status_t* pcm_status;
    snd_pcm_sframes_t delay = 0;
    int               io_result;
#endif

    /* default values */
    status->media_time.seconds = 0;
    status->media_time.nanoseconds = 0;
    status->flags = 0;

#if 0
    /* get the driver status */
    snd_pcm_status_alloca_no_assert(&pcm_status);
    io_result = snd_pcm_status(self->device_handle, pcm_status);
    if (io_result != 0) {
        return BLT_FAILURE;
    }
    delay = snd_pcm_status_get_delay(pcm_status);
    if (delay == 0) {
        /* workaround buggy drivers */
        io_result = snd_pcm_delay(self->device_handle, &delay);
        if (io_result != 0) {
            return BLT_FAILURE;
        }
    }
    
    if (delay > 0 && self->media_type.sample_rate) {
        ATX_UInt64 media_time_samples = (self->next_media_time * 
                                         (ATX_UInt64)self->media_type.sample_rate)/
                                         (ATX_UInt64)1000000000;
        ATX_UInt64 media_time_ns;
        if (delay <= (snd_pcm_sframes_t)media_time_samples) {
            media_time_samples -= delay;
        } else {
            media_time_samples = 0;
        }
        media_time_ns = (media_time_samples*(ATX_UInt64)1000000000)/self->media_type.sample_rate;
        status->media_time = BLT_TimeStamp_FromNanos(media_time_ns);
    } else {
        status->media_time = BLT_TimeStamp_FromNanos(self->next_media_time);
    }
    
    /* return the computed media time */
    ATX_LOG_FINEST_3("delay = %lld samples, input port time = %lld, media time = %lld", (ATX_UInt64)delay, self->next_media_time, BLT_TimeStamp_ToNanos(status->media_time));
    
#endif

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(QnxOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(QnxOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(QnxOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT(QnxOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(QnxOutput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(QnxOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(QnxOutput, BLT_MediaPort)
    QnxOutput_GetName,
    QnxOutput_GetProtocol,
    QnxOutput_GetDirection,
    QnxOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(QnxOutput, BLT_PacketConsumer)
    QnxOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(QnxOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    QnxOutput_GetPortByName,
    QnxOutput_Activate,
    QnxOutput_Deactivate,
    BLT_BaseMediaNode_Start,
    QnxOutput_Stop,
    QnxOutput_Pause,
    QnxOutput_Resume,
    QnxOutput_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(QnxOutput, BLT_OutputNode)
    QnxOutput_GetStatus,
    NULL
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(QnxOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|       QnxOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
QnxOutputModule_Probe(BLT_Module*              self, 
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
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be PACKET and the */
            /* output protocol should be NONE              */
            if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* the input type should be unknown, or audio/pcm */
            if (!(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* the name should be 'qnx:<name>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "qnx:", 4)) {
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(QnxOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(QnxOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(QnxOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(QnxOutputModule, QnxOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(QnxOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    QnxOutputModule_CreateInstance,
    QnxOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define QnxOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(QnxOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_QnxOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("QNX Output", NULL, 0, 
                                 &QnxOutputModule_BLT_ModuleInterface,
                                 &QnxOutputModule_ATX_ReferenceableInterface,
                                 object);
}
