/*****************************************************************
|
|      Cdda: BltCddaInput.c
|
|      Cdda Input Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCddaInput.h"
#include "BltCddaDevice.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltModule.h"
#include "BltByteStreamProvider.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInputModule)
static const BLT_ModuleInterface CddaInputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInput)
static const BLT_InputStreamProviderInterface 
CddaInput_BLT_InputStreamProviderInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} CddaInputModule;

typedef struct {
    BLT_BaseMediaNode base;
    ATX_InputStream   stream;
    BLT_PcmMediaType  media_type;
    BLT_CddaDevice    device;
    BLT_Ordinal       track_index;
    BLT_CddaTrack*    track;
} CddaInput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInput)
static const BLT_MediaNodeInterface CddaInput_BLT_MediaNodeInterface;
static const BLT_MediaPortInterface CddaInput_BLT_MediaPortInterface;
static BLT_Result CddaInput_Destroy(CddaInput* input);

/*----------------------------------------------------------------------
|    CddaInput_Create
+---------------------------------------------------------------------*/
static BLT_Result
CddaInput_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_String               parameters, 
                 ATX_Object*              object)
{
    CddaInput*                input;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;

    BLT_Debug("CddaInput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* check name length */
    if (ATX_StringLength(constructor->name) < 6) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    input = ATX_AllocateZeroMemory(sizeof(CddaInput));
    if (input == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&input->base, module, core);

    /* setup the media type */
    BLT_PcmMediaType_Init(&input->media_type);
	input->media_type.sample_rate     = 44100;
    input->media_type.channel_count   = 2;
    input->media_type.bits_per_sample = 16;
    input->media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE;

    /* parse the name */
    {
        const char* c_index = &constructor->name[5];
        while (*c_index >= '0' && *c_index <= '9') {
            input->track_index = 10*input->track_index + *c_index++ -'0';
        }
    }
    BLT_Debug("CddaInput::Create - track index = %d\n", input->track_index);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)input;
    ATX_INTERFACE(object) = (ATX_Interface*)&CddaInput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    CddaInput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
CddaInput_Destroy(CddaInput* input)
{
    BLT_Debug("CddaInput::Destroy\n");

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&input->base);

    /* free the object memory */
    ATX_FreeMemory(input);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    CddaInput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
CddaInput_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    CddaInput*        input = (CddaInput*)instance;
    BLT_CddaTrackInfo track_info;
    BLT_StreamInfo    stream_info;
    BLT_Result        result;

    BLT_Debug("CddaInput::Activate\n");

    /* keep a reference to the stream */
    input->base.context = *stream;

    /* open the device */
    result = BLT_CddaDevice_Create(NULL, &input->device);
    if (BLT_FAILED(result)) return result;

    /* get track info */
    result = BLT_CddaDevice_GetTrackInfo(&input->device, 
                                         input->track_index,
                                         &track_info);
    if (BLT_FAILED(result)) return result;

    /* check that track is audio */
    if (track_info.type != BLT_CDDA_TRACK_TYPE_AUDIO) {
      return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* create a track object to read from */
    result = BLT_CddaTrack_Create(&input->device, 
				  input->track_index,
				  &input->track);
    if (BLT_FAILED(result)) return result;

    /* start with no info */
    stream_info.mask = 0;

    /* stream size */
    stream_info.size = track_info.duration.frames * BLT_CDDA_FRAME_SIZE;
    stream_info.mask |= BLT_STREAM_INFO_MASK_SIZE;

    /* stream duration */
    stream_info.duration = 
        (track_info.duration.frames*1000)/
        BLT_CDDA_FRAMES_PER_SECOND;
    stream_info.mask |= BLT_STREAM_INFO_MASK_DURATION;

    /* sample rate */
    stream_info.sample_rate = 44100;
    stream_info.mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;

    /* channel count */
    stream_info.channel_count = 2;
    stream_info.mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;

    /* bitrates */
    stream_info.nominal_bitrate = 8*44100*4;
    stream_info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;
    stream_info.average_bitrate = 8*44100*4;
    stream_info.mask |= BLT_STREAM_INFO_MASK_AVERAGE_BITRATE;
    stream_info.instant_bitrate = 8*44100*4;
    stream_info.mask |= BLT_STREAM_INFO_MASK_INSTANT_BITRATE;

    stream_info.data_type = "PCM";
    stream_info.mask |= BLT_STREAM_INFO_MASK_DATA_TYPE;

    /* notify the stream */
    BLT_Stream_SetInfo(stream, &stream_info);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    CddaInput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
CddaInput_Deactivate(BLT_MediaNodeInstance* instance)
{
    CddaInput* input = (CddaInput*)instance;

    BLT_Debug("CddaInput::Deactivate\n");

    /* destroy track */
    if (input->track != NULL) {
        BLT_CddaTrack_Destroy(input->track);
	input->track = NULL;
    }

    /* close device */
    ATX_DESTROY_OBJECT(&input->device);

    /* we're detached from the stream */
    ATX_CLEAR_OBJECT(&input->stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       CddaInput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
CddaInput_GetPortByName(BLT_MediaNodeInstance* instance,
                        BLT_String             name,
                        BLT_MediaPort*         port)
{
    if (ATX_StringsEqual(name, "output")) {
        /* we implement the BLT_MediaPort interface ourselves */
        ATX_INSTANCE(port) = (BLT_MediaPortInstance*)instance;
        ATX_INTERFACE(port) = &CddaInput_BLT_MediaPortInterface;
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    CddaInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
CddaInput_QueryMediaType(BLT_MediaPortInstance* instance,
                         BLT_Ordinal            index,
                         const BLT_MediaType**  media_type)
{
    CddaInput* input = (CddaInput*)instance;
    
    if (index == 0) {
        *media_type = (const BLT_MediaType*)&input->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       CddaInput_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
CddaInput_GetStream(BLT_InputStreamProviderInstance* instance,
                    ATX_InputStream*                 stream)
{
    CddaInput* input = (CddaInput*)instance;
    BLT_Result result;

    /* get the stream from the track */
    result = BLT_CddaTrack_GetStream(input->track, stream);
    if (BLT_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
CddaInput_BLT_MediaNodeInterface = {
    CddaInput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    CddaInput_GetPortByName,
    CddaInput_Activate,
    CddaInput_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(CddaInput,
                                         "output",
                                         STREAM_PULL,
                                         OUT)
static const BLT_MediaPortInterface
CddaInput_BLT_MediaPortInterface = {
    CddaInput_GetInterface,
    CddaInput_GetName,
    CddaInput_GetProtocol,
    CddaInput_GetDirection,
    CddaInput_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamProviderInterface
CddaInput_BLT_InputStreamProviderInterface = {
    CddaInput_GetInterface,
    CddaInput_GetStream
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(CddaInput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInput)
ATX_INTERFACE_MAP_ADD(CddaInput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(CddaInput, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(CddaInput, BLT_InputStreamProvider)
ATX_INTERFACE_MAP_ADD(CddaInput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInput)

/*----------------------------------------------------------------------
|       CddaInputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
CddaInputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* we need a cdda name */
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
            if (ATX_StringsEqualN(constructor->name, "cdda:", 5)) {
                /* this is an exact match for us */
                *match = BLT_MODULE_PROBE_MATCH_EXACT;
            } else {
                /* not us */
                return BLT_FAILURE;
            }

            BLT_Debug("CddaInputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(CddaInput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface CddaInputModule_BLT_ModuleInterface = {
    CddaInputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    CddaInputModule_CreateInstance,
    CddaInputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define CddaInputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(CddaInputModule, 
base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInputModule) 
ATX_INTERFACE_MAP_ADD(CddaInputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(CddaInputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(CddaInputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_CddaInputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Cdda Input", NULL, 0,
                                 &CddaInputModule_BLT_ModuleInterface,
                                 object);
}
