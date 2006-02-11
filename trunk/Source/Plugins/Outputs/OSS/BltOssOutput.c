/*****************************************************************
|
|      File: BltOssOutput.c
|
|      OSS Output Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltOssOutput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputModule)
static const BLT_ModuleInterface OssOutputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutput)
static const BLT_MediaNodeInterface OssOutput_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputInputPort)
static const BLT_MediaPortInterface OssOutputInputPort_BLT_MediaPortInterface;
static const BLT_PacketConsumerInterface OssOutputInputPort_BLT_PacketConsumerInterface;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_OSS_OUTPUT_INVALID_HANDLE (-1)

/*----------------------------------------------------------------------
|    macros
+---------------------------------------------------------------------*/
#define DBG_LOG(l, m) BLT_LOG(BLT_LOG_CHANNEL_PLUGINS, l, m)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} OssOutputModule;

typedef enum {
    BLT_OSS_OUTPUT_STATE_CLOSED,
    BLT_OSS_OUTPUT_STATE_OPEN,
    BLT_OSS_OUTPUT_STATE_CONFIGURED
} OssOutputState;

typedef struct {
    BLT_BaseMediaNode base;
    OssOutputState    state;
    BLT_String        device_name;
    int               device_handle;
    BLT_Flags         device_flags;
    BLT_PcmMediaType  media_type;
    BLT_PcmMediaType  expected_media_type;
    BLT_Cardinal      bytes_before_trigger;
} OssOutput;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_OSS_OUTPUT_FLAG_CAN_TRIGGER  0x01
#define BLT_OSS_OUTPUT_WRITE_WATCHDOG    100

/*----------------------------------------------------------------------
|    prototypes
+---------------------------------------------------------------------*/
static BLT_Result OssOutput_Close(OssOutput* output);

/*----------------------------------------------------------------------
|    OssOutput_SetState
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_SetState(OssOutput* output, OssOutputState state)
{
    if (state != output->state) {
        DBG_LOG(1, ("OssOutput::SetState - from %d to %d\n",
                    output->state, state));
    }
    output->state = state;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_GetCaps
+---------------------------------------------------------------------*/
static void
OssOutput_GetCaps(OssOutput* output)
{
#if defined(SNDCTL_DSP_GETCAPS) && defined(SNDCTL_DSP_SETTRIGGER)
    int caps;
    if (ioctl(output->device_handle, SNDCTL_DSP_GETCAPS, &caps) == 0) {
        if (caps & DSP_CAP_TRIGGER) {
            int enable = ~PCM_ENABLE_OUTPUT;
            output->device_flags |= BLT_OSS_OUTPUT_FLAG_CAN_TRIGGER;
            ioctl(output->device_handle, SNDCTL_DSP_SETTRIGGER, &enable);
        } else {
            output->device_flags ^= BLT_OSS_OUTPUT_FLAG_CAN_TRIGGER;
        }
    }
#endif
}

/*----------------------------------------------------------------------
|    OssOutput_Open
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Open(OssOutput* output)
{
    int io_result;

    switch (output->state) {
      case BLT_OSS_OUTPUT_STATE_CLOSED:
        DBG_LOG(1, ("OssOutput::Open - %s\n", output->device_name));
        io_result = open(output->device_name, O_WRONLY);
        if (io_result < 0) {
            output->device_handle = BLT_OSS_OUTPUT_INVALID_HANDLE;
            switch (errno) {
              case ENOENT:
                return BLT_ERROR_NO_SUCH_DEVICE;

              case EACCES:
                return BLT_ERROR_ACCESS_DENIED;
                
              case EBUSY:
                return BLT_ERROR_DEVICE_BUSY;
                
              default:
                return BLT_FAILURE;
            }
        }
        output->device_handle = io_result;
        OssOutput_GetCaps(output);
        break;

      case BLT_OSS_OUTPUT_STATE_OPEN:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_OSS_OUTPUT_STATE_CONFIGURED:
        return BLT_FAILURE;
    }

    /* update the state */
    OssOutput_SetState(output, BLT_OSS_OUTPUT_STATE_OPEN);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_Close
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Close(OssOutput* output)
{
    switch (output->state) {
      case BLT_OSS_OUTPUT_STATE_CLOSED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_OSS_OUTPUT_STATE_CONFIGURED:
        /* wait for buffers to finish */
        DBG_LOG(1, ("OssOutput::Close (configured)\n"));
        ioctl(output->device_handle, SNDCTL_DSP_SYNC, 0);
        /* FALLTHROUGH */

      case BLT_OSS_OUTPUT_STATE_OPEN:
        /* close the device */
        DBG_LOG(1, ("OssOutput::Close\n"));
        close(output->device_handle);
        output->device_handle = BLT_OSS_OUTPUT_INVALID_HANDLE;
        break;
    }

    /* update the state */
    OssOutput_SetState(output, BLT_OSS_OUTPUT_STATE_CLOSED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_Drain
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Drain(OssOutput* output)
{
    switch (output->state) {
      case BLT_OSS_OUTPUT_STATE_CLOSED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_OSS_OUTPUT_STATE_CONFIGURED:
      case BLT_OSS_OUTPUT_STATE_OPEN:
        /* flush samples buffered by the driver */
        DBG_LOG(1, ("OssOutput::Drain\n"));
        ioctl(output->device_handle, SNDCTL_DSP_SYNC, 0);
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_Configure
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Configure(OssOutput* output)
{
    BLT_Result result;
    int        io_result;
    int        param;

    switch (output->state) {
      case BLT_OSS_OUTPUT_STATE_CLOSED:
        /* first, we need to open the device */
        result = OssOutput_Open(output);
        if (BLT_FAILED(result)) return result;

        /* FALLTHROUGH */

      case BLT_OSS_OUTPUT_STATE_OPEN:
        /* configure the device */

        /* format */
        switch (output->media_type.bits_per_sample) {
        	case 8: param  = AFMT_U8;     break;
        	case 16: param = AFMT_S16_NE; break;
        	default: return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }

        io_result = ioctl(output->device_handle, SNDCTL_DSP_SETFMT, &param);
        if (io_result != 0) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }

        /* sample rate */
        param = output->media_type.sample_rate;
        io_result = ioctl(output->device_handle, SNDCTL_DSP_SPEED, &param);
        if (io_result != 0) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }

        /* channels */
        param = output->media_type.channel_count == 2 ? 1 : 0;
        io_result = ioctl(output->device_handle, SNDCTL_DSP_STEREO, &param);
        if (io_result != 0) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        
        /* compute trigger */
        output->bytes_before_trigger = 
            (output->media_type.sample_rate *
             output->media_type.channel_count) / 4;

        /* set fragments */
        {
            int fragment = 0x7FFF000D;
            ioctl(output->device_handle, SNDCTL_DSP_SETFRAGMENT, &fragment);
        }
        break;

      case BLT_OSS_OUTPUT_STATE_CONFIGURED:
        /* ignore */
        return BLT_SUCCESS;
    }

    /* update the state */
    OssOutput_SetState(output, BLT_OSS_OUTPUT_STATE_CONFIGURED);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_SetFormat
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_SetFormat(OssOutput*              output,
                    const BLT_PcmMediaType* format)
{
    /* compare the media format with the current format */
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
        
        /* perform basic validity checks of the format */
        if (format->sample_format != BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }

        /* copy the format */
        output->media_type = *format;

        /* ensure that we can switch to the new format */
        switch (output->state) {
          case BLT_OSS_OUTPUT_STATE_CLOSED:
          case BLT_OSS_OUTPUT_STATE_OPEN:
            break;

          case BLT_OSS_OUTPUT_STATE_CONFIGURED:
            /* drain any pending samples */
            OssOutput_Drain(output);
            OssOutput_SetState(output, BLT_OSS_OUTPUT_STATE_OPEN);
            break;
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_Write
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Write(OssOutput* output, void* buffer, BLT_Size size)
{
    int        watchdog = BLT_OSS_OUTPUT_WRITE_WATCHDOG;
    BLT_Result result;

    /* ensure that the device is configured */
    result = OssOutput_Configure(output);
    if (BLT_FAILED(result)) return result;

#if defined(SNDCTL_DSP_SETTRIGGER)
    if (output->device_flags & BLT_OSS_OUTPUT_FLAG_CAN_TRIGGER) {
        if (output->bytes_before_trigger > size) {
            output->bytes_before_trigger -= size;
        } else {
            if (output->bytes_before_trigger != 0) {
                int enable = PCM_ENABLE_OUTPUT;
                ioctl(output->device_handle, SNDCTL_DSP_SETTRIGGER, &enable);
            }
            output->bytes_before_trigger = 0;
        }
    }
#endif

    while (size) {
        int nb_written = write(output->device_handle, buffer, size);
        if (nb_written == -1) {
            if (errno == EAGAIN) {
#if defined(SNDCTL_DSP_SETTRIGGER)
                if (output->device_flags & BLT_OSS_OUTPUT_FLAG_CAN_TRIGGER) {
                    /* we have set a trigger, and the buffer is full */
                    int enable = PCM_ENABLE_OUTPUT;
                    ioctl(output->device_handle, 
                          SNDCTL_DSP_SETTRIGGER,
                          &enable);
                }
#endif
                nb_written = 0;
            } else if (errno != EINTR) {
                return BLT_FAILURE;
            }
        }

        size -= nb_written;
        buffer = (void*)((char *)buffer + nb_written);
        if (watchdog-- == 0) return BLT_FAILURE;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutputInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutputInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                             BLT_MediaPacket*            packet)
{
    OssOutput*              output = (OssOutput*)instance;
    const BLT_PcmMediaType* media_type;
    BLT_Size                size;
    BLT_Result              result;

    /* check parameters */
    if (packet == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* check the payload size */
    size = BLT_MediaPacket_GetPayloadSize(packet);
    if (size == 0) return BLT_SUCCESS;

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* set the format of the samples */
    result = OssOutput_SetFormat(output, media_type);
    if (BLT_FAILED(result)) return result;

    /* send the payload to the drvice */
    result = OssOutput_Write(output, 
                             BLT_MediaPacket_GetPayloadBuffer(packet), 
                             size);
    if (BLT_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutputInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutputInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
				  BLT_Ordinal            index,
				  const BLT_MediaType**  media_type)
{
    OssOutput* output = (OssOutput*)instance;
    
    if (index == 0) {
        *media_type = (const BLT_MediaType*)&output->expected_media_type;
        return BLT_SUCCESS;
    } else {
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(OssOutputInputPort, 
                                         "input", 
                                         PACKET, 
                                         IN)
static const BLT_MediaPortInterface
OssOutputInputPort_BLT_MediaPortInterface = {
    OssOutputInputPort_GetInterface,
    OssOutputInputPort_GetName,
    OssOutputInputPort_GetProtocol,
    OssOutputInputPort_GetDirection,
    OssOutputInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
OssOutputInputPort_BLT_PacketConsumerInterface = {
    OssOutputInputPort_GetInterface,
    OssOutputInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputInputPort)
ATX_INTERFACE_MAP_ADD(OssOutputInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(OssOutputInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputInputPort)

/*----------------------------------------------------------------------
|    OssOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_String               parameters, 
                 ATX_Object*              object)
{
    OssOutput*                output;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;

    DBG_LOG(0, ("OssOutput::Create\n"));

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(OssOutput));
    if (output == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&output->base, module, core);

    /* construct the object */
    output->state                      = BLT_OSS_OUTPUT_STATE_CLOSED;
    output->device_name                = NULL;
    output->device_handle              = BLT_OSS_OUTPUT_INVALID_HANDLE;
    output->device_flags               = 0;
    output->media_type.sample_rate     = 0;
    output->media_type.channel_count   = 0;
    output->media_type.bits_per_sample = 0;
    output->media_type.sample_format   = 0;

    /* parse the name */
    if (constructor->name) {
        int index = 0;
        if (ATX_StringLength(constructor->name) < 5) {
            ATX_FreeMemory(output);
            return BLT_ERROR_INVALID_PARAMETERS;
        }
        if (constructor->name[4] >= '0' &&
            constructor->name[4] <= '9') {
            /* name is a soundcard index */
            const char* c_index = &constructor->name[4];
            while (*c_index >= '0' && *c_index <= '9') {
                index = 10*index + *c_index++ -'0';
            }
            if (index == 0) {
                output->device_name = ATX_DuplicateString("/dev/dsp");
            } else {
                char        device_name[32] = "/dev/dsp";
                const char* c_index = &constructor->name[4];
                char*       d_index = &device_name[8];
                while (*c_index >= '0' && *c_index <= '9') {
                    *d_index++ = *c_index++;
                }
                *d_index = '\0';
                output->device_name = ATX_DuplicateString(device_name);
            }
        } else {
            output->device_name = ATX_DuplicateString(constructor->name+4);
        }
    } else {
        output->device_name = ATX_DuplicateString("/dev/dsp");
    }

    /* setup the expected media type */
    BLT_PcmMediaType_Init(&output->expected_media_type);
    output->expected_media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)output;
    ATX_INTERFACE(object) = (ATX_Interface*)&OssOutput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
OssOutput_Destroy(OssOutput* output)
{
    DBG_LOG(0, ("OssOutput::Destroy\n"));

    /* close the device */
    OssOutput_Close(output);

    /* free the name */
    if (output->device_name) {
        ATX_FreeMemory((void*)output->device_name);
    }

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&output->base);

    /* free the object memory */
    ATX_FreeMemory(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       OssOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutput_Start(BLT_MediaNodeInstance* instance)
{
    OssOutput* output = (OssOutput*)instance;

    /* open the device */
    DBG_LOG(1, ("OssOutput::Start\n"));
    OssOutput_Open(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       OssOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutput_Stop(BLT_MediaNodeInstance* instance)
{
    OssOutput* output = (OssOutput*)instance;

    /* close the device */
    DBG_LOG(1, ("OssOutput::Stop\n"));
    OssOutput_Close(output);

    return BLT_SUCCESS;
}
 
/*----------------------------------------------------------------------
|    OssOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutput_Pause(BLT_MediaNodeInstance* instance)
{
    OssOutput* output = (OssOutput*)instance;

    switch (output->state) {
      case BLT_OSS_OUTPUT_STATE_CLOSED:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_OSS_OUTPUT_STATE_CONFIGURED:
      case BLT_OSS_OUTPUT_STATE_OPEN:
        /* reset the device (there is no IOCTL for pause */
        DBG_LOG(1, ("OssOutput::Pause (configured)\n"));
        ioctl(output->device_handle, SNDCTL_DSP_RESET, 0);
        return BLT_SUCCESS;
    }

    return BLT_SUCCESS;
}
                   
/*----------------------------------------------------------------------
|       OssOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutput_GetPortByName(BLT_MediaNodeInstance* instance,
                        BLT_String             name,
                        BLT_MediaPort*         port)
{
    OssOutput* output = (OssOutput*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)output;
        ATX_INTERFACE(port) = &OssOutputInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    OssOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutput_Seek(BLT_MediaNodeInstance* instance,
               BLT_SeekMode*          mode,
               BLT_SeekPoint*         point)
{
    OssOutput* output = (OssOutput*)instance;
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* reset the device */
    ioctl(output->device_handle, SNDCTL_DSP_RESET, 0);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OssOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutput_GetStatus(BLT_OutputNodeInstance* instance,
                    BLT_OutputNodeStatus*   status)
{
    OssOutput* output = (OssOutput*)instance;
    int        delay;
    int        io_result;

    /* ask the driver how much delay there is */
    io_result = ioctl(output->device_handle, SNDCTL_DSP_GETODELAY, &delay);
    if (io_result != 0) {
        return BLT_FAILURE;
    }

    /* convert delay from bytes to milliseconds */
    if (output->media_type.sample_rate &&
        output->media_type.bits_per_sample/8 &&
        output->media_type.channel_count) {
        unsigned long samples = delay/
            ((output->media_type.bits_per_sample/8)*
             output->media_type.channel_count);
        unsigned long delay_ms = 
            (samples*1000)/output->media_type.sample_rate;
        status->delay.seconds = delay_ms/1000;
        delay_ms -= (status->delay.seconds*1000);
        status->delay.nanoseconds = delay_ms*1000000;
    } else {
        status->delay.seconds = 0;
        status->delay.nanoseconds = 0;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
OssOutput_BLT_MediaNodeInterface = {
    OssOutput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    OssOutput_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    OssOutput_Start,
    OssOutput_Stop,
    OssOutput_Pause,
    BLT_BaseMediaNode_Resume,
    OssOutput_Seek
};

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
static const BLT_OutputNodeInterface
OssOutput_BLT_OutputNodeInterface = {
    OssOutput_GetInterface,
    OssOutput_GetStatus
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(OssOutput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutput)
ATX_INTERFACE_MAP_ADD(OssOutput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(OssOutput, BLT_OutputNode)
ATX_INTERFACE_MAP_ADD(OssOutput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutput)

/*----------------------------------------------------------------------
|       OssOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
OssOutputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* the name should be 'oss:<name>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "oss:", 4)) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            DBG_LOG(1, ("OssOutputModule::Probe - Ok [%d]\n", *match));
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(OssOutput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface OssOutputModule_BLT_ModuleInterface = {
    OssOutputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    OssOutputModule_CreateInstance,
    OssOutputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define OssOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(OssOutputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputModule) 
ATX_INTERFACE_MAP_ADD(OssOutputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(OssOutputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(OssOutputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_OssOutputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("OSS Output", NULL, 0, 
                                 &OssOutputModule_BLT_ModuleInterface,
                                 object);
}
