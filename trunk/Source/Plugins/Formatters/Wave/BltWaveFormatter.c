/*****************************************************************
|
|      File: BltWaveFormatter.c
|
|      Wave Formatter Module
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
#include "BltWaveFormatter.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltByteStreamUser.h"
#include "BltByteStreamProvider.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
    BLT_UInt32     wav_type_id;
} WaveFormatterModule;

typedef struct {
    BLT_Size         size;
    BLT_PcmMediaType media_type;
} WaveFormatterInput;

typedef struct {
    BLT_MediaType    media_type;
    ATX_OutputStream stream;
} WaveFormatterOutput;

typedef struct {
    BLT_BaseMediaNode   base;
    WaveFormatterInput  input;
    WaveFormatterOutput output;
} WaveFormatter;

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define WAVE_FORMAT_PCM 1
#define BLT_WAVE_FORMATTER_RIFF_HEADER_SIZE 44

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterModule)
static const BLT_ModuleInterface WaveFormatterModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatter)
static const BLT_MediaNodeInterface WaveFormatter_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterInputPort)
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterOutputPort)
static BLT_Result WaveFormatter_Destroy(WaveFormatter* formatter);

/*----------------------------------------------------------------------
|    WaveFormatter_WriteWavHeader
+---------------------------------------------------------------------*/
static BLT_Result
WaveFormatter_WriteWavHeader(WaveFormatter*          formatter, 
                             const BLT_PcmMediaType* pcm_type)
{
    unsigned char buffer[4];

    /* RIFF tag */
    ATX_OutputStream_Write(&formatter->output.stream, "RIFF", 4, NULL);

    /* RIFF chunk size */
    ATX_BytesFromInt32Le(buffer, formatter->input.size + 8+16+12);
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    ATX_OutputStream_Write(&formatter->output.stream, "WAVE", 4, NULL);

    ATX_OutputStream_Write(&formatter->output.stream, "fmt ", 4, NULL);

    ATX_BytesFromInt32Le(buffer, 16L);
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    ATX_BytesFromInt16Le(buffer, WAVE_FORMAT_PCM);
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 2, NULL);

    /* number of channels */
    ATX_BytesFromInt16Le(buffer, (short)pcm_type->channel_count);        
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 2, NULL);

    /* sample rate */
    ATX_BytesFromInt32Le(buffer, pcm_type->sample_rate);       
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    /* bytes per second */
    ATX_BytesFromInt32Le(buffer, 
                         pcm_type->sample_rate * 
                         pcm_type->channel_count * 
                         (pcm_type->bits_per_sample/8));
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    /* alignment   */
    ATX_BytesFromInt16Le(buffer, (short)(pcm_type->channel_count * 
                                         (pcm_type->bits_per_sample/8)));     
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 2, NULL);

    /* bits per sample */
    ATX_BytesFromInt16Le(buffer, pcm_type->bits_per_sample);               
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 2, NULL);

    ATX_OutputStream_Write(&formatter->output.stream, "data", 4, NULL);

    /* data size */
    ATX_BytesFromInt32Le(buffer, formatter->input.size);        
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WaveFormatter_UpdateWavHeader
+---------------------------------------------------------------------*/
static BLT_Result
WaveFormatter_UpdateWavHeader(WaveFormatter* formatter)
{
    BLT_Result    result;
    unsigned char buffer[4];

    BLT_Debug("WaveFormatter::UpdateWavHeader - size = %d\n", 
              formatter->input.size);

    result = ATX_OutputStream_Seek(&formatter->output.stream, 4);
    ATX_BytesFromInt32Le(buffer, formatter->input.size + 8+16+12);
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    result = ATX_OutputStream_Seek(&formatter->output.stream, 40);
    if (BLT_FAILED(result)) return result;

    ATX_BytesFromInt32Le(buffer, formatter->input.size);        
    ATX_OutputStream_Write(&formatter->output.stream, buffer, 4, NULL);

    BLT_Debug("WaveFormatter::UpdateWavHeader - updated\n");

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WaveFormatterInputPort_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatterInputPort_GetStream(BLT_OutputStreamProviderInstance* instance,
                                 ATX_OutputStream*                 stream,
                                 const BLT_MediaType*              media_type)
{
    WaveFormatter* formatter = (WaveFormatter*)instance;

    /* check that we have a stream */
    if (ATX_OBJECT_IS_NULL(&formatter->output.stream)) {
        return BLT_ERROR_PORT_HAS_NO_STREAM;
    }

    /* return our output stream */
    *stream = formatter->output.stream;
    ATX_REFERENCE_OBJECT(stream);

    /* we're providing the stream, but we *receive* the type */
    if (media_type) {
        if (media_type->id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        } else {
            /* copy the input type parameters */
            formatter->input.media_type = *(const BLT_PcmMediaType*)media_type;
        }
    }

    /* write a header unless the output stream already has some data */
    /* (this might be due to the fact that we're writing more than   */
    /*  one input stream into the same output stream                 */
    {
        ATX_Offset where = 0;
        ATX_OutputStream_Tell(&formatter->output.stream, &where);
        if (where == 0) { 
            WaveFormatter_WriteWavHeader(formatter, &formatter->input.media_type);
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WaveFormatterInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatterInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                      BLT_Ordinal            index,
                                      const BLT_MediaType**  media_type)
{
    WaveFormatter* formatter = (WaveFormatter*)instance;

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&formatter->input.media_type;
        return BLT_SUCCESS;
    } else {
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WaveFormatterInputPort, 
                                         "input",
                                         STREAM_PUSH,
                                         IN)
static const BLT_MediaPortInterface
WaveFormatterInputPort_BLT_MediaPortInterface = {
    WaveFormatterInputPort_GetInterface,
    WaveFormatterInputPort_GetName,
    WaveFormatterInputPort_GetProtocol,
    WaveFormatterInputPort_GetDirection,
    WaveFormatterInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_OutputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_OutputStreamProviderInterface
WaveFormatterInputPort_BLT_OutputStreamProviderInterface = {
    WaveFormatterInputPort_GetInterface,
    WaveFormatterInputPort_GetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterInputPort)
ATX_INTERFACE_MAP_ADD(WaveFormatterInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(WaveFormatterInputPort, BLT_OutputStreamProvider)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterInputPort)

/*----------------------------------------------------------------------
|    WaveFormatterOutputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatterOutputPort_SetStream(BLT_OutputStreamUserInstance* instance,
                                  ATX_OutputStream*             stream)
{
    WaveFormatter* formatter = (WaveFormatter*)instance;

    /* keep a reference to the stream */
    formatter->output.stream = *stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WaveFormatterOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatterOutputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                       BLT_Ordinal            index,
                                       const BLT_MediaType**  media_type)
{
    WaveFormatter* formatter = (WaveFormatter*)instance;
    if (index == 0) {
        *media_type = &formatter->output.media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WaveFormatterOutputPort,
                                         "output",
                                         STREAM_PUSH,
                                         OUT)
static const BLT_MediaPortInterface
WaveFormatterOutputPort_BLT_MediaPortInterface = {
    WaveFormatterOutputPort_GetInterface,
    WaveFormatterOutputPort_GetName,
    WaveFormatterOutputPort_GetProtocol,
    WaveFormatterOutputPort_GetDirection,
    WaveFormatterOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_OutputStreamUser interface
+---------------------------------------------------------------------*/
static const BLT_OutputStreamUserInterface
WaveFormatterOutputPort_BLT_OutputStreamUserInterface = {
    WaveFormatterOutputPort_GetInterface,
    WaveFormatterOutputPort_SetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterOutputPort)
ATX_INTERFACE_MAP_ADD(WaveFormatterOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(WaveFormatterOutputPort, BLT_OutputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterOutputPort)

/*----------------------------------------------------------------------
|    WaveFormatter_Create
+---------------------------------------------------------------------*/
static BLT_Result
WaveFormatter_Create(BLT_Module*              module,
                     BLT_Core*                core, 
                     BLT_ModuleParametersType parameters_type,
                     BLT_CString              parameters, 
                     ATX_Object*              object)
{
    WaveFormatter* formatter;

    BLT_Debug("WaveFormatter::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    formatter = ATX_AllocateZeroMemory(sizeof(WaveFormatter));
    if (formatter == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&formatter->base, module, core);

    /* setup the input media type */
    BLT_PcmMediaType_Init(&formatter->input.media_type);
    formatter->input.media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE;

    /* setup the output media type */
    BLT_MediaType_Init(&formatter->output.media_type, 
                       ((WaveFormatterModule*)
                        ATX_INSTANCE(module))->wav_type_id);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)formatter;
    ATX_INTERFACE(object) = (ATX_Interface*)&WaveFormatter_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WaveFormatter_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
WaveFormatter_Destroy(WaveFormatter* formatter)
{
    BLT_Debug("WaveFormatter::Destroy\n");

    /* update the header if needed */
    if (!ATX_OBJECT_IS_NULL(&formatter->output.stream)) {
        ATX_Offset where = 0;
        ATX_OutputStream_Tell(&formatter->output.stream, &where);
        formatter->input.size = where;
        if (formatter->input.size >= BLT_WAVE_FORMATTER_RIFF_HEADER_SIZE) {
            formatter->input.size -= BLT_WAVE_FORMATTER_RIFF_HEADER_SIZE;
        }

        /* update the header */
        WaveFormatter_UpdateWavHeader(formatter);

        /* set the stream back to its original position */
        ATX_OutputStream_Seek(&formatter->output.stream, where);
    }

    /* release the stream */
    ATX_RELEASE_OBJECT(&formatter->output.stream);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&formatter->base);

    /* free the object memory */
    ATX_FreeMemory(formatter);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       WaveFormatter_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatter_GetPortByName(BLT_MediaNodeInstance* instance,
                            BLT_CString            name,
                            BLT_MediaPort*         port)
{
    WaveFormatter* formatter = (WaveFormatter*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)formatter;
        ATX_INTERFACE(port) = &WaveFormatterInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)formatter;
        ATX_INTERFACE(port) = &WaveFormatterOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
WaveFormatter_BLT_MediaNodeInterface = {
    WaveFormatter_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    WaveFormatter_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(WaveFormatter, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatter)
ATX_INTERFACE_MAP_ADD(WaveFormatter, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(WaveFormatter, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatter)

/*----------------------------------------------------------------------
|       WaveFormatterModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatterModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    WaveFormatterModule* module = (WaveFormatterModule*)instance;
    BLT_Registry         registry;
    BLT_Result           result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the .wav file extensions */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".wav",
                                            "audio/wav");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/wav" type */
    result = BLT_Registry_RegisterName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/wav",
        &module->wav_type_id);
    if (BLT_FAILED(result)) return result;
    
    BLT_Debug("WaveFormatterModule::Attach (audio/wav type = %d)\n", 
              module->wav_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       WaveFormatterModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
WaveFormatterModule_Probe(BLT_ModuleInstance*      instance, 
                          BLT_Core*                core,
                          BLT_ModuleParametersType parameters_type,
                          BLT_AnyConst             parameters,
                          BLT_Cardinal*            match)
{
    WaveFormatterModule* module = (WaveFormatterModule*)instance;
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be STREAM_PUSH and the */
            /* output protocol should be STREAM_PUSH            */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH)) {
                return BLT_FAILURE;
            }

            /* the input type should be audio/pcm */
            if (constructor->spec.input.media_type->id !=
                BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "WaveFormatter")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */

                /* the output type should be audio/wav */
                if (constructor->spec.output.media_type->id !=
                    module->wav_type_id) {
                    return BLT_FAILURE;
                }

                *match = BLT_MODULE_PROBE_MATCH_MAX - 10;
            }

            BLT_Debug("WaveFormatterModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(WaveFormatter)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(WaveFormatter, "Wave Formatter", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface WaveFormatterModule_BLT_ModuleInterface = {
    WaveFormatterModule_GetInterface,
    BLT_BaseModule_GetInfo,
    WaveFormatterModule_Attach,
    WaveFormatterModule_CreateInstance,
    WaveFormatterModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define WaveFormatterModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(WaveFormatterModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterModule) 
ATX_INTERFACE_MAP_ADD(WaveFormatterModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(WaveFormatterModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveFormatterModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_WaveFormatterModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return WaveFormatterModule_Create(object);
}
