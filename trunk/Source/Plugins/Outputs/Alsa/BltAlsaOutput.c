/*****************************************************************
|
|      File: BltAlsaOutput.c
|
|      ALSA Output Module
|
|      (c) 2002-2005 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <alsa/asoundlib.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltAlsaOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"
#include "BltDebug.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputModule)
static const BLT_ModuleInterface AlsaOutputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutput)
static const BLT_MediaNodeInterface AlsaOutput_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputInputPort)
static const BLT_MediaPortInterface AlsaOutputInputPort_BLT_MediaPortInterface;
static const BLT_PacketConsumerInterface AlsaOutputInputPort_BLT_PacketConsumerInterface;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_ALSA_DEFAULT_BUFFER_TIME    500000 /* 0.5 secs */
#define BLT_ALSA_DEFAULT_PERIOD_SIZE    4096

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} AlsaOutputModule;

typedef enum {
    BLT_ALSA_OUTPUT_STATE_CLOSED,
    BLT_ALSA_OUTPUT_STATE_OPEN,
    BLT_ALSA_OUTPUT_STATE_CONFIGURED,
    BLT_ALSA_OUTPUT_STATE_PREPARED
} AlsaOutputState;

typedef struct {
    BLT_BaseMediaNode base;
    AlsaOutputState   state;
    ATX_StringBuffer  device_name;
    snd_pcm_t*        device_handle;
    BLT_PcmMediaType  media_type;
} AlsaOutput;

/*----------------------------------------------------------------------
|    prototypes
+---------------------------------------------------------------------*/
static BLT_Result AlsaOutput_Close(AlsaOutput* output);

/*----------------------------------------------------------------------
|    AlsaOutput_SetState
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_SetState(AlsaOutput* output, AlsaOutputState state)
{
    if (state != output->state) {
        BLT_Debug("AlsaOutput::SetState - from %d to %d\n",
                  output->state, state);
    }
    output->state = state;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Open
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Open(AlsaOutput* output)
{
    int io_result;

    BLT_Debug("AlsaOutput::Open\n");

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
        BLT_Debug("AlsaOutput::Open - snd_pcm_open\n");
        io_result = snd_pcm_open(&output->device_handle,
                                 output->device_name,
                                 SND_PCM_STREAM_PLAYBACK,
                                 0);
        if (io_result != 0) {
            output->device_handle = NULL;
            return BLT_FAILURE;
        }
        break;

      case BLT_ALSA_OUTPUT_STATE_OPEN:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        return BLT_FAILURE;
    }

    /* update the state */
    AlsaOutput_SetState(output, BLT_ALSA_OUTPUT_STATE_OPEN);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Close
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Close(AlsaOutput* output)
{
    BLT_Debug("AlsaOutput::Close\n");

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        /* wait for buffers to finish */
        BLT_Debug("AlsaOutput::Close - snd_pcm_drain\n");
        snd_pcm_drain(output->device_handle);
        /* FALLTHROUGH */

      case BLT_ALSA_OUTPUT_STATE_OPEN:
      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
        /* close the device */
        BLT_Debug("AlsaOutput::Close - snd_pcm_close\n");
        snd_pcm_close(output->device_handle);
        output->device_handle = NULL;
        break;
    }

    /* update the state */
    AlsaOutput_SetState(output, BLT_ALSA_OUTPUT_STATE_CLOSED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Drain(AlsaOutput* output)
{
    BLT_Debug("AlsaOutput::Drain\n");

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
      case BLT_ALSA_OUTPUT_STATE_OPEN:
      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        /* drain samples buffered by the driver (wait until they are played) */
        BLT_Debug("AlsaOutput::Drain - snd_pcm_drain\n");
        snd_pcm_drain(output->device_handle);
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Reset
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Reset(AlsaOutput* output)
{
    BLT_Debug("AlsaOutput::Reset\n");

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
      case BLT_ALSA_OUTPUT_STATE_OPEN:
      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        BLT_Debug("AlsaOutput::Reset - snd_pcm_drop\n");
        snd_pcm_drop(output->device_handle);
        AlsaOutput_SetState(output, BLT_ALSA_OUTPUT_STATE_CONFIGURED);
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Prepare
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Prepare(AlsaOutput* output)
{
    int ior;

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
      case BLT_ALSA_OUTPUT_STATE_OPEN:
    /* we need to be configured already for 'prepare' to work */
    return BLT_FAILURE;

      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
        /* prepare the device */
        BLT_Debug("AlsaOutput::Prepare - snd_pcm_prepare\n");

        ior = snd_pcm_prepare(output->device_handle);
        if (ior != 0) {
            BLT_Debug("AlsaOutput::Prepare: - snd_pcm_prepare failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }
        break;

      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        /* ignore */
        return BLT_SUCCESS;
    }

    /* update the state */
    AlsaOutput_SetState(output, BLT_ALSA_OUTPUT_STATE_PREPARED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Unprepare
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Unprepare(AlsaOutput* output)
{
    BLT_Result result;

    BLT_Debug("AlsaOutput::Unprepare\n");

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
      case BLT_ALSA_OUTPUT_STATE_OPEN:
      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
        /* ignore */
        break;

      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        /* drain any pending samples */
        result = AlsaOutput_Drain(output);
        if (BLT_FAILED(result)) return result;
        
        /* update the state */
        AlsaOutput_SetState(output, BLT_ALSA_OUTPUT_STATE_CONFIGURED);
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Configure
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Configure(AlsaOutput*             output, 
                     const BLT_PcmMediaType* format)
{
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_sw_params_t* sw_params;
    unsigned int         rate = format->sample_rate;
    unsigned int         buffer_time = BLT_ALSA_DEFAULT_BUFFER_TIME;
    snd_pcm_uframes_t    buffer_size = 0;
    snd_pcm_uframes_t    period_size = BLT_ALSA_DEFAULT_PERIOD_SIZE;
    snd_pcm_format_t     pcm_format_id = SND_PCM_FORMAT_UNKNOWN;
    int                  ior;
    BLT_Result           result;

    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_CLOSED:
        /* first, we need to open the device */
        result = AlsaOutput_Open(output);
        if (BLT_FAILED(result)) return result;

        /* FALLTHROUGH */

      case BLT_ALSA_OUTPUT_STATE_CONFIGURED:
      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        /* check to see if the format has changed */
        if (format->sample_rate     != output->media_type.sample_rate   ||
            format->channel_count   != output->media_type.channel_count ||
            format->bits_per_sample != output->media_type.bits_per_sample) {
            /* new format */

            /* check the format */
            if (format->sample_rate     == 0 ||
                format->channel_count   == 0 ||
                format->bits_per_sample == 0) {
                return BLT_ERROR_INVALID_MEDIA_FORMAT;
            }
        
            /* unprepare (forget current settings) */
            result = AlsaOutput_Unprepare(output);
            if (BLT_FAILED(result)) return result;
        } else {
            /* same format, do nothing */
            return BLT_SUCCESS;
        }
        
        /* FALLTHROUGH */

      case BLT_ALSA_OUTPUT_STATE_OPEN:
        /* configure the device with the new format */
        BLT_Debug("AlsaOutput::Configure\n");

        /* copy the format */
        output->media_type = *format;

        BLT_Debug("AlsaOutput::Configure - new format: sr=%d, ch=%d, bps=%d\n",
                  format->sample_rate,
                  format->channel_count,
                  format->bits_per_sample);

        /* allocate a new blank configuration */
        snd_pcm_hw_params_alloca(&hw_params);
        snd_pcm_hw_params_any(output->device_handle, hw_params);

        /* use interleaved access */
        ior = snd_pcm_hw_params_set_access(output->device_handle, hw_params, 
                                           SND_PCM_ACCESS_RW_INTERLEAVED);
        if (ior != 0) {
            BLT_Debug("AldaOutput::Configure - set 'access' failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* set the sample rate */
        ior = snd_pcm_hw_params_set_rate_near(output->device_handle, 
                                              hw_params, 
                                              &rate, NULL);
        if (ior != 0) {
            BLT_Debug("AldaOutput::Configure - set 'rate' failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* set the number of channels */
        ior = snd_pcm_hw_params_set_channels(output->device_handle, hw_params,
                                             format->channel_count);
        if (ior != 0) {
            BLT_Debug("AldaOutput::Configure - set 'channels' failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* set the sample format */
        switch (format->sample_format) {
        	case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE:
        		switch (format->bits_per_sample) {
        			case  8: pcm_format_id = SND_PCM_FORMAT_S8;      break;
        			case 16: pcm_format_id = SND_PCM_FORMAT_S16_LE;  break;
        			case 24: pcm_format_id = SND_PCM_FORMAT_S24_3LE; break;
        			case 32: pcm_format_id = SND_PCM_FORMAT_S32_LE;  break;
        		}
        		break;
        		
        	case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE:
        		switch (format->bits_per_sample) {
        			case  8: pcm_format_id = SND_PCM_FORMAT_U8;      break;
        			case 16: pcm_format_id = SND_PCM_FORMAT_U16_LE;  break;
        			case 24: pcm_format_id = SND_PCM_FORMAT_U24_3LE; break;
        			case 32: pcm_format_id = SND_PCM_FORMAT_U32_LE;  break;
        		}
        		break;

        	case BLT_PCM_SAMPLE_FORMAT_FLOAT_LE:
        		switch (format->bits_per_sample) {
        			case 32: pcm_format_id = SND_PCM_FORMAT_FLOAT_LE; break;
        		}
        		break;

        	case BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE:
        		switch (format->bits_per_sample) {
        			case  8: pcm_format_id = SND_PCM_FORMAT_S8;      break;
        			case 16: pcm_format_id = SND_PCM_FORMAT_S16_BE;  break;
        			case 24: pcm_format_id = SND_PCM_FORMAT_S24_3BE; break;
        			case 32: pcm_format_id = SND_PCM_FORMAT_S32_BE;  break;
        		}
        		break;
        		
        	case BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE:
        		switch (format->bits_per_sample) {
        			case  8: pcm_format_id = SND_PCM_FORMAT_U8;      break;
        			case 16: pcm_format_id = SND_PCM_FORMAT_U16_BE;  break;
        			case 24: pcm_format_id = SND_PCM_FORMAT_U24_3BE; break;
        			case 32: pcm_format_id = SND_PCM_FORMAT_U32_BE;  break;
        		}
        		break;

        	case BLT_PCM_SAMPLE_FORMAT_FLOAT_BE:
        		switch (format->bits_per_sample) {
        			case 32: pcm_format_id = SND_PCM_FORMAT_FLOAT_BE; break;
        		}
        		break;
        }

        if (pcm_format_id == SND_PCM_FORMAT_UNKNOWN) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        ior = snd_pcm_hw_params_set_format(output->device_handle, hw_params,
                                           pcm_format_id);
        if (ior != 0) {
            BLT_Debug("AldaOutput::Configure - set 'format' failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* set the period size */
        ior = snd_pcm_hw_params_set_period_size_near(output->device_handle, 
                                                     hw_params,
                                                     &period_size,
                                                     NULL);
        if (ior != 0) {
            BLT_Debug("AlsaOutput::Configure - set 'period size' failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }
        
                                                
        /* set the buffer time (duration) */
        ior = snd_pcm_hw_params_set_buffer_time_near(output->device_handle,
                                                     hw_params, 
                                                     &buffer_time,
                                                     NULL);
        if (ior != 0) {
            BLT_Debug("AlsaOutput::Configure - set 'buffer time' failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* get the actual buffer size */
        snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);

        /* activate this configuration */
        ior = snd_pcm_hw_params(output->device_handle, hw_params);
        if (ior != 0) {
            BLT_Debug("AlsaOutput::Configure: - snd_pcm_hw_params failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* configure the software parameters */
        snd_pcm_sw_params_alloca(&sw_params);
        snd_pcm_sw_params_current(output->device_handle, sw_params);

        /* set the start threshold to 1/2 the buffer size */
        snd_pcm_sw_params_set_start_threshold(output->device_handle, 
                                              sw_params, 
                                              buffer_size/2);

        /* set the buffer alignment */
        snd_pcm_sw_params_set_xfer_align(output->device_handle, 
                                         sw_params, 1);

        /* activate the sofware parameters */
        ior = snd_pcm_sw_params(output->device_handle, sw_params);
        if (ior != 0) {
            BLT_Debug("AlsaOutput::Configure - snd_pcm_sw_params failed (%d)\n",
                      ior);
            return BLT_FAILURE;
        }

        /* print status info */
        {
            snd_pcm_uframes_t val;
            BLT_Debug("AlsaOutput::Configure - sample type = %x\n", pcm_format_id);
            if (rate != format->sample_rate) {
                BLT_Debug("AlsaOutput::Configure - actual sample = %d\n", rate);
            }
            BLT_Debug("AlsaOutput::Configure - actual buffer time = %d\n", 
                      buffer_time);
            BLT_Debug("AlsaOutput::Configure - buffer size = %d\n", buffer_size); 
            snd_pcm_sw_params_get_start_threshold(sw_params, &val);
            BLT_Debug("AlsaOutput::Configure - start threshold = %d\n", val); 
            snd_pcm_sw_params_get_stop_threshold(sw_params, &val);
            BLT_Debug("AlsaOutput::Configure - stop threshold = %d\n", val); 
            snd_pcm_hw_params_get_period_size(hw_params, &val, NULL);
            BLT_Debug("AlsaOutput::Configure - period size = %d\n", val);
        }

        break;
    }

    /* update the state */
    AlsaOutput_SetState(output, BLT_ALSA_OUTPUT_STATE_CONFIGURED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Write
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Write(AlsaOutput* output, void* buffer, BLT_Size size)
{
    int          watchdog = 5;
    int          io_result;
    unsigned int sample_count;
    BLT_Result   result;

    /* ensure that the device is prepared */
    result = AlsaOutput_Prepare(output);
    if (BLT_FAILED(result)) return result;

    /* compute the number of samples */
    sample_count = size / (output->media_type.channel_count*
                           output->media_type.bits_per_sample/8);
                           
    /* write samples to the device and handle underruns */       
    do {
        io_result = snd_pcm_writei(output->device_handle, 
                                   buffer, sample_count);        
        if (io_result == (int)sample_count) return BLT_SUCCESS;

        /* we reach this point if the first write failed */
        {
            snd_pcm_status_t* status;
            snd_pcm_state_t   state;
            snd_pcm_status_alloca(&status);

            io_result = snd_pcm_status(output->device_handle, status);
            if (io_result != 0) {
                return BLT_FAILURE;
            }
            state = snd_pcm_status_get_state(status);
            if (state == SND_PCM_STATE_XRUN) {
                BLT_Debug("AlsaOutput::Write - **** UNDERRUN *****\n");
            
                /* re-prepare the channel */
                io_result = snd_pcm_prepare(output->device_handle);
                if (io_result != 0) {
                    return BLT_FAILURE;
                }
            } else {
                   BLT_Debug("AlsaOutput::Write - **** STATE = %d ****\n", state);
                }
        }
        
        BLT_Debug("AlsaOutput::Write - **** RETRY *****\n");

    } while(watchdog--);

    BLT_Debug("AlsaOutput::Write - **** THE WATCHDOG BIT US ****\n");
    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|    AlsaOutputInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutputInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                              BLT_MediaPacket*            packet)
{
    AlsaOutput*             output = (AlsaOutput*)instance;
    const BLT_PcmMediaType* media_type;
    BLT_ByteBuffer          buffer;
    BLT_Size                size;
    BLT_Result              result;

    /* check parameters */
    if (packet == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* check the payload buffer and size */
    buffer = BLT_MediaPacket_GetPayloadBuffer(packet);
    size = BLT_MediaPacket_GetPayloadSize(packet);
    if (size == 0) return BLT_SUCCESS;

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* configure the device for this format */
    result = AlsaOutput_Configure(output, media_type);
	if (BLT_FAILED(result)) return result;
	
    /* write the audio samples */
    return AlsaOutput_Write(output, buffer, size);
}

/*----------------------------------------------------------------------
|    AlsaOutputInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutputInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                                   BLT_Ordinal            index,
                                                   const BLT_MediaType**  media_type)
{
    BLT_COMPILER_UNUSED(instance);
    /*AlsaOutput* output = (AlsaOutput*)instance;*/

    if (index == 0) {
        *media_type = &BLT_GenericPcmMediaType;
        return BLT_SUCCESS;
    } else {
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AlsaOutputInputPort, 
                                         "input", 
                                         PACKET, 
                                         IN)
static const BLT_MediaPortInterface
AlsaOutputInputPort_BLT_MediaPortInterface = {
    AlsaOutputInputPort_GetInterface,
    AlsaOutputInputPort_GetName,
    AlsaOutputInputPort_GetProtocol,
    AlsaOutputInputPort_GetDirection,
    AlsaOutputInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
AlsaOutputInputPort_BLT_PacketConsumerInterface = {
    AlsaOutputInputPort_GetInterface,
    AlsaOutputInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputInputPort)
ATX_INTERFACE_MAP_ADD(AlsaOutputInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(AlsaOutputInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputInputPort)

/*----------------------------------------------------------------------
|    AlsaOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_String               parameters, 
                  ATX_Object*              object)
{
    AlsaOutput*           output;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(AlsaOutput));
    if (output == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&output->base, module, core);

    /* construct the object */
    output->state                      = BLT_ALSA_OUTPUT_STATE_CLOSED;
    output->device_handle              = NULL;
    output->media_type.sample_rate     = 0;
    output->media_type.channel_count   = 0;
    output->media_type.bits_per_sample = 0;

    /* parse the name */
    if (constructor->name && ATX_StringLength(constructor->name) > 5) {
        output->device_name = ATX_DuplicateString(constructor->name+5);
    } else {
        output->device_name = ATX_DuplicateString("default");
    }
    
    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)output;
    ATX_INTERFACE(object) = (ATX_Interface*)&AlsaOutput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AlsaOutput_Destroy(AlsaOutput* output)
{
    /* close the device */
    AlsaOutput_Close(output);

    /* free the name */
    ATX_FreeMemory(output->device_name);

    /* call the base destructor */
    BLT_BaseMediaNode_Destruct(&output->base);

    /* free the object memory */
    ATX_FreeMemory(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AlsaOutput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    AlsaOutput* output = (AlsaOutput*)instance;
    BLT_COMPILER_UNUSED(stream);
        
    BLT_Debug("AlsaOutput::Activate\n");

    /* open the device */
    AlsaOutput_Open(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AlsaOutput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Deactivate(BLT_MediaNodeInstance* instance)
{
    AlsaOutput* output = (AlsaOutput*)instance;

    BLT_Debug("AlsaOutput::Deactivate\n");

    /* close the device */
    AlsaOutput_Close(output);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|       AlsaOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Start(BLT_MediaNodeInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    BLT_Debug("AlsaOutput::Start\n");

    /* do nothing here, as the device is already open (Activate) */

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AlsaOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Stop(BLT_MediaNodeInstance* instance)
{
    AlsaOutput* output = (AlsaOutput*)instance;

    BLT_Debug("AlsaOutput::Stop\n");

    /* reset the device */
    AlsaOutput_Reset(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AlsaOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Pause(BLT_MediaNodeInstance* instance)
{
    AlsaOutput* output = (AlsaOutput*)instance;
        
    BLT_Debug("AlsaOutput::Pause\n");

    /* pause the device */
    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        snd_pcm_pause(output->device_handle, 1);
        break;

      default:
        /* ignore */
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AlsaOutput_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Resume(BLT_MediaNodeInstance* instance)
{
    AlsaOutput* output = (AlsaOutput*)instance;
        
    BLT_Debug("AlsaOutput::Resume\n");

    /* pause the device */
    switch (output->state) {
      case BLT_ALSA_OUTPUT_STATE_PREPARED:
        snd_pcm_pause(output->device_handle, 0);
        break;

      default:
        /* ignore */
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AlsaOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_String             name,
                         BLT_MediaPort*         port)
{
    AlsaOutput* output = (AlsaOutput*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)output;
        ATX_INTERFACE(port) = &AlsaOutputInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    AlsaOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutput_Seek(BLT_MediaNodeInstance* instance,
                    BLT_SeekMode*          mode,
                    BLT_SeekPoint*         point)
{
    AlsaOutput* output = (AlsaOutput*)instance;
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* ignore unless we're prepared */
    if (output->state != BLT_ALSA_OUTPUT_STATE_PREPARED) {
        return BLT_SUCCESS;
    }

    /* reset the device */
    AlsaOutput_Reset(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
AlsaOutput_BLT_MediaNodeInterface = {
    AlsaOutput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    AlsaOutput_GetPortByName,
    AlsaOutput_Activate,
    AlsaOutput_Deactivate,
    AlsaOutput_Start,
    AlsaOutput_Stop,
    AlsaOutput_Pause,
    AlsaOutput_Resume,
    AlsaOutput_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AlsaOutput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutput)
ATX_INTERFACE_MAP_ADD(AlsaOutput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(AlsaOutput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutput)

/*----------------------------------------------------------------------
|       AlsaOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaOutputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* the name should be 'alsa:<name>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "alsa:", 4)) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            BLT_Debug("AlsaOutputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AlsaOutput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface AlsaOutputModule_BLT_ModuleInterface = {
    AlsaOutputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    AlsaOutputModule_CreateInstance,
    AlsaOutputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AlsaOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AlsaOutputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputModule) 
ATX_INTERFACE_MAP_ADD(AlsaOutputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(AlsaOutputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaOutputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_AlsaOutputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("ALSA Output", NULL, 0, 
                                 &AlsaOutputModule_BLT_ModuleInterface,
                                 object);
}
