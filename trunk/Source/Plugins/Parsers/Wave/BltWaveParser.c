/*****************************************************************
|
|      File: BltWaveParser.c
|
|      Wave Parser Module
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
#include "BltWaveParser.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltByteStreamProvider.h"
#include "BltByteStreamUser.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserModule)
static const BLT_ModuleInterface WaveParserModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParser)
static const BLT_MediaNodeInterface WaveParser_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserInputPort)
static const BLT_MediaPortInterface WaveParserInputPort_BLT_MediaPortInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserOutputPort)
static const BLT_MediaPortInterface WaveParserOutputPort_BLT_MediaPortInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule  base;
    BLT_UInt32      wav_type_id;
} WaveParserModule;

typedef struct {
    BLT_MediaType media_type;
} WaveParserInputPort;

typedef struct {
    ATX_InputStream stream;
    BLT_Size        size;
    BLT_MediaType*  media_type;
    unsigned int    block_size;
} WaveParserOutputPort;

typedef struct {
    BLT_BaseMediaNode    base;
    WaveParserInputPort  input;
    WaveParserOutputPort output;
} WaveParser;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_WAVE_HEADER_BUFFER_SIZE      32
#define BLT_WAVE_HEADER_RIFF_LOOKUP_SIZE 12
#define BLT_WAVE_HEADER_FMT_LOOKUP_SIZE  16
#define BLT_WAVE_HEADER_MAX_LOOKUP       524288

/*----------------------------------------------------------------------
|    WAVE tags
+---------------------------------------------------------------------*/
#define BLT_WAVE_FORMAT_UNKNOWN            0x0000
#define BLT_WAVE_FORMAT_PCM                0x0001
#define BLT_WAVE_FORMAT_IEEE_FLOAT         0x0003
#define BLT_WAVE_FORMAT_ALAW               0x0006
#define BLT_WAVE_FORMAT_MULAW              0x0007
#define BLT_WAVE_FORMAT_MPEG               0x0050
#define BLT_WAVE_FORMAT_MPEGLAYER3         0x0055
#define BLT_WAVE_FORMAT_EXTENSIBLE         0xFFFE
#define BLT_WAVE_FORMAT_DEVELOPMENT        0xFFFF

/*----------------------------------------------------------------------
|       WaveParser_ParseHeader
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParser_ParseHeader(WaveParser*      parser, 
                       ATX_InputStream* stream,
                       BLT_Size*        header_size,
                       BLT_StreamInfo*  stream_info)
{
    unsigned char buffer[BLT_WAVE_HEADER_BUFFER_SIZE];
    BLT_Size      bytes_read;
    BLT_Offset    position;
    BLT_Cardinal  bytes_per_second = 0;
    BLT_Result    result;
    
    /* check that we have a stream */
    if (ATX_OBJECT_IS_NULL(stream)) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* rewind the byte stream */
    ATX_InputStream_Seek(stream, 0);
    position = 0;
    *header_size = 0;

    /* no initial info yet */
    stream_info->mask = 0;

    /* read the wave header */
    result = ATX_InputStream_Read(stream, buffer, 
                                  BLT_WAVE_HEADER_RIFF_LOOKUP_SIZE,
                                  &bytes_read);
    if (BLT_FAILED(result) || bytes_read != BLT_WAVE_HEADER_RIFF_LOOKUP_SIZE) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    position += BLT_WAVE_HEADER_RIFF_LOOKUP_SIZE;

    /* parse the header */
    if (buffer[0] != 'R' ||
        buffer[1] != 'I' ||
        buffer[2] != 'F' ||
        buffer[3] != 'F') {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    if (buffer[ 8] != 'W' ||
        buffer[ 9] != 'A' ||
        buffer[10] != 'V' ||
        buffer[11] != 'E') {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    do {
        unsigned long chunk_size;
        unsigned int  format_tag;
        result = ATX_InputStream_Read(stream, buffer, 8, &bytes_read);
        if (BLT_FAILED(result) || bytes_read != 8) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
        position += 8;
        chunk_size = ATX_BytesToInt32Le(buffer+4);

        if (buffer[0] == 'f' &&
            buffer[1] == 'm' &&
            buffer[2] == 't' &&
            buffer[3] == ' ') {
            /* 'fmt ' chunk */
            result = ATX_InputStream_Read(stream, buffer, 
                                          BLT_WAVE_HEADER_FMT_LOOKUP_SIZE, 
                                          &bytes_read);
            if (BLT_FAILED(result) || 
                bytes_read != BLT_WAVE_HEADER_FMT_LOOKUP_SIZE) {
                return BLT_ERROR_INVALID_MEDIA_FORMAT;
            }
            position += BLT_WAVE_HEADER_FMT_LOOKUP_SIZE;

            format_tag = ATX_BytesToInt16Le(buffer);
            switch (format_tag) {
              case BLT_WAVE_FORMAT_PCM:
              case BLT_WAVE_FORMAT_IEEE_FLOAT:
                {
                    /* read the media type */
                    BLT_PcmMediaType media_type;
                    BLT_PcmMediaType_Init(&media_type);
                    BLT_MediaType_Free(parser->output.media_type);
                    media_type.channel_count   = ATX_BytesToInt16Le(buffer+2);
                    media_type.sample_rate     = ATX_BytesToInt32Le(buffer+4);
                    media_type.bits_per_sample = (BLT_UInt8)(8*((ATX_BytesToInt16Le(buffer+14)+7)/8));
                    if (format_tag == BLT_WAVE_FORMAT_IEEE_FLOAT) {
                        media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_FLOAT_LE;
                    } else {
                        media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE;
                    }
                    BLT_MediaType_Clone((const BLT_MediaType*)&media_type, &parser->output.media_type);

                    /* compute the block size */
                    parser->output.block_size = media_type.channel_count*media_type.bits_per_sample/8;

                    /* update the stream info */
                    stream_info->sample_rate   = media_type.sample_rate;
                    stream_info->channel_count = media_type.channel_count;
                    stream_info->data_type     = "PCM";
                    stream_info->mask |=
                        BLT_STREAM_INFO_MASK_SAMPLE_RATE   |
                        BLT_STREAM_INFO_MASK_CHANNEL_COUNT |
                        BLT_STREAM_INFO_MASK_DATA_TYPE;
                    bytes_per_second = 
                        media_type.channel_count *
                        media_type.sample_rate   *
                        media_type.bits_per_sample/8;
                }
                break;

              default:
                return BLT_ERROR_UNSUPPORTED_CODEC;
            }

            chunk_size -= BLT_WAVE_HEADER_FMT_LOOKUP_SIZE;
        } else if (buffer[0] == 'd' &&
                   buffer[1] == 'a' &&
                   buffer[2] == 't' &&
                   buffer[3] == 'a') {
            /* 'data' chunk */
            parser->output.size = chunk_size;
            *header_size = position;

            /* compute stream info */
            if (chunk_size) {
                stream_info->size = chunk_size;
            } else {
                ATX_InputStream_GetSize(stream, &stream_info->size);
            }
            stream_info->mask |= BLT_STREAM_INFO_MASK_SIZE;
            if (stream_info->size != 0 && bytes_per_second != 0) {
                ATX_Int64 duration;
                ATX_Int64_Set_Int32(duration, stream_info->size);
                ATX_Int64_Mul_Int32(duration, 1000);
                ATX_Int64_Div_Int32(duration, bytes_per_second);
                stream_info->duration = ATX_Int64_Get_Int32(duration);
                stream_info->mask |= BLT_STREAM_INFO_MASK_DURATION;
            } else {
                stream_info->duration = 0;
            }
            stream_info->nominal_bitrate = 
            stream_info->average_bitrate = 
            stream_info->instant_bitrate = bytes_per_second*8;
            stream_info->mask |= 
                BLT_STREAM_INFO_MASK_NOMINAL_BITRATE |
                BLT_STREAM_INFO_MASK_AVERAGE_BITRATE |
                BLT_STREAM_INFO_MASK_INSTANT_BITRATE;
            return BLT_SUCCESS;
        }

        /* skip chunk */
        ATX_InputStream_Seek(stream, chunk_size+position);
        position = chunk_size+position;
    } while (position < BLT_WAVE_HEADER_MAX_LOOKUP);

    return BLT_ERROR_INVALID_MEDIA_FORMAT;
}

/*----------------------------------------------------------------------
|       WaveParserInputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParserInputPort_SetStream(BLT_InputStreamUserInstance* instance,
                              ATX_InputStream*             stream,
                              const BLT_MediaType*         media_type)
{
    WaveParser*    parser = (WaveParser*)instance;
    BLT_Size       header_size;
    BLT_StreamInfo stream_info;
    BLT_Result     result;
    BLT_COMPILER_UNUSED(media_type);

    /* check media type */
    if (media_type == NULL || media_type->id != parser->input.media_type.id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(&parser->output.stream);

    /* parse the stream header */
    result = WaveParser_ParseHeader(parser, 
                                    stream, 
                                    &header_size,
                                    &stream_info);
    if (BLT_FAILED(result)) return result;

    /* update the stream info */
    if (stream_info.mask && !ATX_OBJECT_IS_NULL(&parser->base.context)) {
        BLT_Stream_SetInfo(&parser->base.context, &stream_info);
    }

    /* create a substream */
    return ATX_SubInputStream_Create(stream, 
                                     header_size, 
                                     parser->output.size,
                                     NULL,
                                     &parser->output.stream);
}

/*----------------------------------------------------------------------
|    WaveParserInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParserInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                   BLT_Ordinal            index,
                                   const BLT_MediaType**  media_type)
{
    WaveParser* parser = (WaveParser*)instance;
    
    if (index == 0) {
        *media_type = &parser->input.media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamUserInterface
WaveParserInputPort_BLT_InputStreamUserInterface = {
    WaveParserInputPort_GetInterface,
    WaveParserInputPort_SetStream
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WaveParserInputPort, 
                                         "input",
                                         STREAM_PULL,
                                         IN)
static const BLT_MediaPortInterface
WaveParserInputPort_BLT_MediaPortInterface = {
    WaveParserInputPort_GetInterface,
    WaveParserInputPort_GetName,
    WaveParserInputPort_GetProtocol,
    WaveParserInputPort_GetDirection,
    WaveParserInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserInputPort)
ATX_INTERFACE_MAP_ADD(WaveParserInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(WaveParserInputPort, BLT_InputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserInputPort)

/*----------------------------------------------------------------------
|       WaveParserOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParserOutputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                    BLT_Ordinal            index,
                                    const BLT_MediaType**  media_type)
{
    WaveParser* parser = (WaveParser*)instance;
    
    if (index == 0) {
        *media_type = parser->output.media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       WaveParserOutputPort_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParserOutputPort_GetStream(BLT_InputStreamProviderInstance* instance,
                               ATX_InputStream*                 stream)
{
    WaveParser* parser = (WaveParser*)instance;

    *stream = parser->output.stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WaveParserOutputPort, 
                                         "output",
                                         STREAM_PULL,
                                         OUT)
static const BLT_MediaPortInterface
WaveParserOutputPort_BLT_MediaPortInterface = {
    WaveParserOutputPort_GetInterface,
    WaveParserOutputPort_GetName,
    WaveParserOutputPort_GetProtocol,
    WaveParserOutputPort_GetDirection,
    WaveParserOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamProviderInterface
WaveParserOutputPort_BLT_InputStreamProviderInterface = {
    WaveParserOutputPort_GetInterface,
    WaveParserOutputPort_GetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserOutputPort)
ATX_INTERFACE_MAP_ADD(WaveParserOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(WaveParserOutputPort, BLT_InputStreamProvider)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserOutputPort)

/*----------------------------------------------------------------------
|    WaveParser_Create
+---------------------------------------------------------------------*/
static BLT_Result
WaveParser_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  ATX_Object*              object)
{
    WaveParser* parser;

    BLT_Debug("WaveParser::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    parser = ATX_AllocateZeroMemory(sizeof(WaveParser));
    if (parser == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&parser->base, module, core);

    /* construct the object */
    BLT_MediaType_Init(&parser->input.media_type,
                       ((WaveParserModule*)ATX_INSTANCE(module))->wav_type_id);
    ATX_CLEAR_OBJECT(&parser->output.stream);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)parser;
    ATX_INTERFACE(object) = (ATX_Interface*)&WaveParser_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WaveParser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
WaveParser_Destroy(WaveParser* parser)
{
    BLT_Debug("WaveParser::Destroy\n");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(&parser->output.stream);

    /* free the media type extensions */
    BLT_MediaType_Free(parser->output.media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&parser->base);

    /* free the object memory */
    ATX_FreeMemory(parser);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       WaveParser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParser_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    WaveParser* parser = (WaveParser*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)parser;
        ATX_INTERFACE(port) = &WaveParserInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)parser;
        ATX_INTERFACE(port) = &WaveParserOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    WaveParser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParser_Seek(BLT_MediaNodeInstance* instance,
                BLT_SeekMode*          mode,
                BLT_SeekPoint*         point)
{
    WaveParser* parser = (WaveParser*)instance;

    /* estimate the seek point */
    if (ATX_OBJECT_IS_NULL(&parser->base.context)) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(&parser->base.context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_SAMPLE) ||
        !(point->mask & BLT_SEEK_POINT_MASK_OFFSET)) {
        return BLT_FAILURE;
    }

    /* align the offset to the nearest sample */
    point->offset -= point->offset%(parser->output.block_size);

    /* seek to the estimated offset */
    /* seek into the input stream (ignore return value) */
    ATX_InputStream_Seek(&parser->output.stream, point->offset);
    
    /* set the mode so that the nodes down the chaine know the seek has */
    /* already been done on the stream                                  */
    *mode = BLT_SEEK_MODE_IGNORE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
WaveParser_BLT_MediaNodeInterface = {
    WaveParser_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    WaveParser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    WaveParser_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(WaveParser, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParser)
ATX_INTERFACE_MAP_ADD(WaveParser, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(WaveParser, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParser)

/*----------------------------------------------------------------------
|       WaveParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParserModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    WaveParserModule* module = (WaveParserModule*)instance;
    BLT_Registry      registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".wav" file extension */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".wav",
                                            "audio/wav");
    if (BLT_FAILED(result)) return result;

    /* get the type id for "audio/wav" */
    result = BLT_Registry_GetIdForName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/wav",
        &module->wav_type_id);
    if (BLT_FAILED(result)) return result;
    
    BLT_Debug("Wave Parser Module::Attach (audio/wav type = %d\n",
              module->wav_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       WaveParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
WaveParserModule_Probe(BLT_ModuleInstance*      instance, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    WaveParserModule* module = (WaveParserModule*)instance;
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* we need the input protocol to be STREAM_PULL and the output */
            /* protocol to be STREAM_PULL                                  */
             if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL)) {
                return BLT_FAILURE;
            }

            /* we need the input media type to be 'audio/wav' */
            if (constructor->spec.input.media_type->id != module->wav_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unknown at this point */
            if (constructor->spec.output.media_type->id != 
                BLT_MEDIA_TYPE_ID_UNKNOWN) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "WaveParser")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not out name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 10;
            }

            BLT_Debug("WaveParserModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(WaveParser)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(WaveParser, "Wave Parser", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface WaveParserModule_BLT_ModuleInterface = {
    WaveParserModule_GetInterface,
    BLT_BaseModule_GetInfo,
    WaveParserModule_Attach,
    WaveParserModule_CreateInstance,
    WaveParserModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define WaveParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(WaveParserModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserModule) 
ATX_INTERFACE_MAP_ADD(WaveParserModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(WaveParserModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(WaveParserModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_WaveParserModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return WaveParserModule_Create(object);
}
