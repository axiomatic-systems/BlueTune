/*****************************************************************
|
|      Alsa: BltAlsaInput.c
|
|      ALSA Input Module
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
#include "BltAlsaInput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltModule.h"
#include "BltByteStreamProvider.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_ALSA_DEFAULT_BUFFER_SIZE 65536  /* frames */
#define BLT_ALSA_DEFAULT_PERIOD_SIZE 4096   /* frames */

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputModule)
static const BLT_ModuleInterface AlsaInputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInput)
static const BLT_InputStreamProviderInterface AlsaInput_BLT_InputStreamProviderInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputStream)
static const ATX_InputStreamInterface AlsaInputStream_ATX_InputStreamInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} AlsaInputModule;

typedef struct {
    ATX_Cardinal     reference_count;
    ATX_StringBuffer device_name;
    snd_pcm_t*       device_handle;
    BLT_PcmMediaType media_type;
    unsigned int     bytes_per_frame;
    ATX_Offset       position;
    ATX_Offset       size;
} AlsaInputStream;

typedef struct {
    BLT_BaseMediaNode base;
    ATX_InputStream   stream;
} AlsaInput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInput)
static const BLT_MediaNodeInterface AlsaInput_BLT_MediaNodeInterface;
static const BLT_MediaPortInterface AlsaInput_BLT_MediaPortInterface;
static BLT_Result AlsaInput_Destroy(AlsaInput* input);

/*----------------------------------------------------------------------
|    AlsaInputStream_ParseName
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInputStream_ParseName(AlsaInputStream* stream,
                          BLT_String       name)
{
    /* the name format is: alsa:{option=value}[;{option=value}...] */

    ATX_String    device_name   = "default";
    unsigned int  channel_count = 2;     /* default = stereo */
    unsigned int  sample_rate   = 44100; /* default = cd sample rate */
    unsigned long duration      = 0;     /* no limit */
    
    /* make a copy of the name so that we can null-terminate options */
    char     name_buffer[1024]; 
    unsigned name_length = ATX_StringLength(name);
    char*    option = name_buffer;
    char*    look = option;
    char     c;

    /* copy the name in the buffer */
    if (name_length+1 >= sizeof(name_buffer) || name_length < 5) {
        return ATX_ERROR_INVALID_PARAMETERS;
    }
    ATX_CopyString(name_buffer, name+5);

    /* parse the options */
    do {
        c = *look;
        if (*look == ';' || *look == '\0') {
            long value = 0;

            /* force-terminate the option */
            *look = '\0';
            
            /* parse option */
            if (ATX_StringsEqualN(option, "device=", 7)) {
                device_name = option+7;
            } else if (ATX_StringsEqualN(option, "ch=", 3)) {
                ATX_ParseInteger(option+3, ATX_FALSE, &value);
                channel_count = value;
            } else if (ATX_StringsEqualN(option, "sr=", 3)) {
                ATX_ParseInteger(option+3, ATX_FALSE, &value);
                sample_rate = value;
            } else if (ATX_StringsEqualN(option, "duration=", 9)) {
                ATX_ParseInteger(option+9, ATX_FALSE, &value);
                duration = value;
            } 
        }

        look++;
    } while (c != '\0');

    /* device name */
    stream->device_name = ATX_DuplicateString(device_name);
    
    /* format */
    if (channel_count == 0) {
        BLT_Debug("AlsaInput - invalid channel count\n");
        return ATX_ERROR_INVALID_PARAMETERS;
    }
    if (sample_rate ==0) {
        BLT_Debug("AlsaInput - invalid sample rate\n");
        return ATX_ERROR_INVALID_PARAMETERS;
    }
    BLT_PcmMediaType_Init(&stream->media_type);
    stream->media_type.channel_count   = channel_count;
    stream->media_type.sample_rate     = sample_rate;
    stream->media_type.bits_per_sample = 16;

    /* stream size */
    {
        ATX_Int64 size;
        ATX_Int64_Set_Int32(size, duration);
        ATX_Int64_Mul_Int32(size, sample_rate);
        ATX_Int64_Mul_Int32(size, channel_count*2);
        ATX_Int64_Div_Int32(size, 1000);
        stream->size = ATX_Int64_Get_Int32(size);
    }

    /* debug info */
    BLT_Debug("AlsaInput - recording from '%s': ch=%d, sr=%d", 
              device_name,
              channel_count,
              sample_rate);
    if (duration == 0) {
        BLT_Debug("\n");
    } else {
        BLT_Debug(", duration=%dms (%d bytes)\n", duration, stream->size);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Construct
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInputStream_Construct(AlsaInputStream* stream, BLT_String name)
{
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_sw_params_t* sw_params;
    unsigned int         rate;
    snd_pcm_uframes_t    buffer_size = BLT_ALSA_DEFAULT_BUFFER_SIZE;
    snd_pcm_uframes_t    period_size = BLT_ALSA_DEFAULT_PERIOD_SIZE;
    int                  ior;
    BLT_Result           result;

    /* set initial values */
    stream->reference_count = 1;

    /* try to parse the name */
    result = AlsaInputStream_ParseName(stream, name);
    if (BLT_FAILED(result)) {
        BLT_Debug("AlsaInput - invalid name %s\n", name);
        return result;
    }

    /* compute some parameters */
    stream->bytes_per_frame = 
        stream->media_type.channel_count *
        stream->media_type.bits_per_sample/8;
        
    /* open the device */
    ior = snd_pcm_open(&stream->device_handle,
                       stream->device_name,
                       SND_PCM_STREAM_CAPTURE,
                       0);
    if (ior != 0) {
        BLT_Debug("AlsaInput - snd_pcm_open failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* allocate a new blank configuration */
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(stream->device_handle, hw_params);

    /* use interleaved access */
    ior = snd_pcm_hw_params_set_access(stream->device_handle, hw_params, 
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ior != 0) {
        BLT_Debug("AlsaInput - set 'access' failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* set the sample rate */
    rate = stream->media_type.sample_rate;
    ior = snd_pcm_hw_params_set_rate_near(stream->device_handle, 
                                          hw_params, 
                                          &rate, NULL);
    if (ior != 0) {
        BLT_Debug("AldaInput - set 'rate' failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* set the number of channels */
    ior = snd_pcm_hw_params_set_channels(stream->device_handle, hw_params,
                                         stream->media_type.channel_count);
    if (ior != 0) {
        BLT_Debug("AlsaInput - set 'channels' failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* set the sample format */
    ior = snd_pcm_hw_params_set_format(stream->device_handle, hw_params,
                                       SND_PCM_FORMAT_S16);
    if (ior != 0) {
        BLT_Debug("AlsaInput - set 'format' failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* set the period size */
    ior = snd_pcm_hw_params_set_period_size_near(stream->device_handle, 
						 hw_params,
						 &period_size,
						 NULL);
    if (ior != 0) {
        BLT_Debug("AlsaInput::Configure - set 'period size' failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* set the buffer size */
    ior = snd_pcm_hw_params_set_buffer_size_near(stream->device_handle,
                                                 hw_params, 
                                                 &buffer_size);
    if (ior != 0) {
        BLT_Debug("AlsaInput - set 'buffer size' failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* activate this configuration */
    ior = snd_pcm_hw_params(stream->device_handle, hw_params);
    if (ior != 0) {
        BLT_Debug("AlsaInput - snd_pcm_hw_params failed (%d)\n",
                  ior);
        return BLT_FAILURE;
    }

    /* configure the software parameters */
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(stream->device_handle, sw_params);

    /* set the buffer alignment */
    snd_pcm_sw_params_set_xfer_align(stream->device_handle, 
                                     sw_params, 1);

    /* activate the sofware parameters */
    ior = snd_pcm_sw_params(stream->device_handle, sw_params);
    if (ior != 0) {
        BLT_Debug("AlsaInput - snd_pcm_sw_params failed (%d)\n", ior);
        return BLT_FAILURE;
    }

    /* prepare the device */
    ior = snd_pcm_prepare(stream->device_handle);
    if (ior != 0) {
        BLT_Debug("AlsaOutput::Prepare: - snd_pcm_prepare failed (%d)\n",
                  ior);
        return BLT_FAILURE;
    }

    /* print status info */
    {
        snd_pcm_uframes_t val;
        if (rate != stream->media_type.sample_rate) {
            BLT_Debug("AlsaOutput::Prepare - actual sample = %d\n", rate);
        }
        BLT_Debug("AlsaStream::Prepare - actual buffer size = %d\n", 
                  buffer_size);
        BLT_Debug("AlsaOutput::Prepare - buffer size = %d\n", buffer_size); 
        snd_pcm_sw_params_get_start_threshold(sw_params, &val);
        BLT_Debug("AlsaOutput::Prepare - start threshold = %d\n", val); 
        snd_pcm_sw_params_get_stop_threshold(sw_params, &val);
        BLT_Debug("AlsaOutput::Prepare - stop threshold = %d\n", val); 
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Create
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInputStream_Create(BLT_String name, ATX_InputStream* object)
{
    AlsaInputStream* stream;
    BLT_Result       result;

    /* allocate the object */
    stream = (AlsaInputStream*)
        ATX_AllocateZeroMemory(sizeof(AlsaInputStream));
    if (stream == NULL) {
        ATX_CLEAR_OBJECT(object);
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct the object */
    result = AlsaInputStream_Construct(stream, name);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory((void*)(stream));
        ATX_CLEAR_OBJECT(object);
        return result;
    }

    ATX_INSTANCE(object) = (ATX_InputStreamInstance*)stream;
    ATX_INTERFACE(object) = &AlsaInputStream_ATX_InputStreamInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Destruct
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInputStream_Destruct(AlsaInputStream* stream)
{
    snd_pcm_drop(stream->device_handle);
    snd_pcm_close(stream->device_handle);
    ATX_FreeMemory((void*)stream->device_name);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInputStream_Destroy(AlsaInputStream* stream)
{
    /* destruct the stream */
    AlsaInputStream_Destruct(stream);

    /* free the memory */
    ATX_FreeMemory((void*)stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Read
+---------------------------------------------------------------------*/
ATX_METHOD
AlsaInputStream_Read(ATX_InputStreamInstance* instance, 
                     ATX_Any                  buffer,
                     ATX_Size                 bytes_to_read,
                     ATX_Size*                bytes_read)
{
    int               watchdog = 5; /* max number of retries */
    snd_pcm_sframes_t frames_to_read;
    snd_pcm_sframes_t frames_read;
    AlsaInputStream*  stream = (AlsaInputStream*)instance;
    
    /* default values */
    if (bytes_read) *bytes_read = 0;

    /* check if we have a size limit */
    if (stream->size != 0) {
        unsigned int max_read = stream->size - stream->position;
        if (bytes_to_read > max_read) {
            bytes_to_read = max_read;
        }
        if (bytes_to_read == 0) {
            /* no more bytes to read */
            return ATX_ERROR_EOS;
        }
    }

    /* read the samples from the input device */
    frames_to_read = bytes_to_read/stream->bytes_per_frame;

    /* try to read and handle overruns */
    do {
        frames_read = snd_pcm_readi(stream->device_handle, 
                                    buffer, frames_to_read);
        if (frames_read > 0) {
            unsigned int byte_count = frames_read*stream->bytes_per_frame;
            if (bytes_read) *bytes_read = byte_count;
            stream->position += byte_count;
            return ATX_SUCCESS;
        }
    
        /* if this failed, try to recover */
        {
            int               io_result;
            snd_pcm_status_t* status;
            snd_pcm_state_t   state;

            snd_pcm_status_alloca(&status);
            io_result = snd_pcm_status(stream->device_handle, status);
            if (io_result != 0) {
                return BLT_FAILURE;
            }
            state = snd_pcm_status_get_state(status);
            if (state == SND_PCM_STATE_XRUN) {
                BLT_Debug("AlsaInput::Read - **** OVERRUN *****\n");
            
                /* re-prepare the channel */
                io_result = snd_pcm_prepare(stream->device_handle);
                if (io_result != 0) return ATX_FAILURE;
            }
        }

        BLT_Debug("AlsaInput::Read - **** RETRY ****\n");

    } while (watchdog--);

    
    BLT_Debug("AlsaInput::Read - **** THE WATCHDOG BIT US *****\n");

    /* nothing can be read */
    if (bytes_read) *bytes_read = 0;
    return ATX_FAILURE;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Seek
+---------------------------------------------------------------------*/
ATX_METHOD
AlsaInputStream_Seek(ATX_InputStreamInstance* instance, ATX_Offset  offset)
{
    /* not seekable */
    ATX_COMPILER_UNUSED(instance);
    ATX_COMPILER_UNUSED(offset);

    return ATX_FAILURE;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_Tell
+---------------------------------------------------------------------*/
ATX_METHOD
AlsaInputStream_Tell(ATX_InputStreamInstance* instance, ATX_Offset* offset)
{
    AlsaInputStream* stream = (AlsaInputStream*)instance;

    /* return the current position */
    *offset = stream->position;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_GetSize
+---------------------------------------------------------------------*/
ATX_METHOD 
AlsaInputStream_GetSize(ATX_InputStreamInstance* instance, ATX_Size* size)
{
    AlsaInputStream* stream = (AlsaInputStream*)instance;

    *size = stream->size;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInputStream_GetAvailable
+---------------------------------------------------------------------*/
ATX_METHOD 
AlsaInputStream_GetAvailable(ATX_InputStreamInstance* instance, 
                             ATX_Size*                available)
{
    /* NOTE: not implemented yet */
    ATX_COMPILER_UNUSED(instance);

    *available = 0;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       ATX_InputStream interface
+---------------------------------------------------------------------*/
static const ATX_InputStreamInterface
AlsaInputStream_ATX_InputStreamInterface = {
    AlsaInputStream_GetInterface,
    AlsaInputStream_Read,
    AlsaInputStream_Seek,
    AlsaInputStream_Tell,
    AlsaInputStream_GetSize,
    AlsaInputStream_GetAvailable
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AlsaInputStream, reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputStream) 
ATX_INTERFACE_MAP_ADD(AlsaInputStream, ATX_InputStream)
ATX_INTERFACE_MAP_ADD(AlsaInputStream, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputStream)

/*----------------------------------------------------------------------
|    AlsaInput_Create
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInput_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_String               parameters, 
                 ATX_Object*              object)
{
    AlsaInput*                input;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    BLT_Result                result;

    BLT_Debug("AlsaInput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    input = ATX_AllocateZeroMemory(sizeof(AlsaInput));
    if (input == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&input->base, module, core);
    
    /* create a stream for the input */
    result = AlsaInputStream_Create(constructor->name, &input->stream);
    if (ATX_FAILED(result)) {
        goto failure;
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)input;
    ATX_INTERFACE(object) = (ATX_Interface*)&AlsaInput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;

failure:
    AlsaInput_Destroy(input);
    ATX_CLEAR_OBJECT(object);
    return result;
}

/*----------------------------------------------------------------------
|    AlsaInput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AlsaInput_Destroy(AlsaInput* input)
{
    BLT_Debug("AlsaInput::Destroy\n");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(&input->stream);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&input->base);

    /* free the object memory */
    ATX_FreeMemory(input);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AlsaInput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaInput_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    AlsaInput* input = (AlsaInput*)instance;

    /* update the stream info */
    if (((AlsaInputStream*)ATX_INSTANCE(&input->stream))->size) {
        BLT_StreamInfo info;

        info.mask = BLT_STREAM_INFO_MASK_SIZE;
        info.size = ((AlsaInputStream*)ATX_INSTANCE(&input->stream))->size;
        BLT_Stream_SetInfo(stream, &info);
    }
    
    /* call the inherited method */
    return BLT_BaseMediaNode_Activate((BLT_MediaNodeInstance*)&input->base, 
                                      stream);
}

/*----------------------------------------------------------------------
|       AlsaInput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaInput_GetPortByName(BLT_MediaNodeInstance* instance,
                        BLT_String             name,
                        BLT_MediaPort*         port)
{
    if (ATX_StringsEqual(name, "output")) {
        /* we implement the BLT_MediaPort interface ourselves */
        ATX_INSTANCE(port) = (BLT_MediaPortInstance*)instance;
        ATX_INTERFACE(port) = &AlsaInput_BLT_MediaPortInterface;
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    AlsaInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaInput_QueryMediaType(BLT_MediaPortInstance* instance,
                         BLT_Ordinal            index,
                         const BLT_MediaType**  media_type)
{
    AlsaInput* input = (AlsaInput*)instance;
    
    if (index == 0) {
        *media_type = (const BLT_MediaType*)
	    &((AlsaInputStream*)ATX_INSTANCE(&input->stream))->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       AlsaInput_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaInput_GetStream(BLT_InputStreamProviderInstance* instance,
                    ATX_InputStream*                 stream)
{
    AlsaInput* input = (AlsaInput*)instance;

    /* return our stream object */
    *stream = input->stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
AlsaInput_BLT_MediaNodeInterface = {
    AlsaInput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    AlsaInput_GetPortByName,
    AlsaInput_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AlsaInput, 
                                         "output", 
                                         STREAM_PULL, 
                                         OUT)
static const BLT_MediaPortInterface
AlsaInput_BLT_MediaPortInterface = {
    AlsaInput_GetInterface,
    AlsaInput_GetName,
    AlsaInput_GetProtocol,
    AlsaInput_GetDirection,
    AlsaInput_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamProviderInterface
AlsaInput_BLT_InputStreamProviderInterface = {
    AlsaInput_GetInterface,
    AlsaInput_GetStream
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AlsaInput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInput)
ATX_INTERFACE_MAP_ADD(AlsaInput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(AlsaInput, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(AlsaInput, BLT_InputStreamProvider)
ATX_INTERFACE_MAP_ADD(AlsaInput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInput)

/*----------------------------------------------------------------------
|       AlsaInputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AlsaInputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* we need a alsa name */
            if (constructor->name == NULL) return BLT_FAILURE;

            /* the input protocol should be NONE, and the output */
            /* protocol should be STREAM_PULL                    */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_NONE) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL)) {
                return BLT_FAILURE;
            }

            /* check the name */
            if (ATX_StringsEqualN(constructor->name, "alsa:", 5)) {
                /* this is an exact match for us */
                *match = BLT_MODULE_PROBE_MATCH_EXACT;
            } else {
                return BLT_FAILURE;
            }

            BLT_Debug("AlsaInputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AlsaInput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface AlsaInputModule_BLT_ModuleInterface = {
    AlsaInputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    AlsaInputModule_CreateInstance,
    AlsaInputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AlsaInputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AlsaInputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputModule) 
ATX_INTERFACE_MAP_ADD(AlsaInputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(AlsaInputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AlsaInputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_AlsaInputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Alsa Input", NULL, 0,
                                 &AlsaInputModule_BLT_ModuleInterface,
                                 object);
}
