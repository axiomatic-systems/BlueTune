/*****************************************************************
|
|      File: BltTagParser.c
|
|      Tag Parser Module
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
#include "BltTagParser.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltByteStreamProvider.h"
#include "BltByteStreamUser.h"
#include "BltId3Parser.h"
#include "BltApeParser.h"
#include "BltEventListener.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserModule)
static const BLT_ModuleInterface TagParserModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParser)
static const BLT_MediaNodeInterface TagParser_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserInputPort)
static const BLT_MediaPortInterface TagParserInputPort_BLT_MediaPortInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserOutputPort)
static const BLT_MediaPortInterface TagParserOutputPort_BLT_MediaPortInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
    BLT_UInt32     mpeg_audio_type_id;
} TagParserModule;

typedef struct {
    BLT_MediaType   media_type;
    ATX_InputStream stream;
} TagParserInputPort;

typedef struct {
    BLT_MediaType media_type;
} TagParserOutputPort;

typedef struct {
    BLT_BaseMediaNode   base;
    TagParserInputPort  input;
    TagParserOutputPort output;
} TagParser;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_TAG_PARSER_MEDIA_TYPE_FLAGS_PARSED 1

/*----------------------------------------------------------------------
|       TagParserInputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
TagParserInputPort_SetStream(BLT_InputStreamUserInstance* instance,
                             ATX_InputStream*             stream,
                             const BLT_MediaType*         media_type)
{
    TagParser*     parser = (TagParser*)instance;
    BLT_Size       id3_header_size  = 0;
    BLT_Size       id3_trailer_size = 0;
    /*BLT_Size       ape_trailer_size = 0;*/
    BLT_Size       header_size      = 0;
    BLT_Size       trailer_size     = 0;
    BLT_Offset     stream_start;
    BLT_Size       stream_size;
    ATX_Properties stream_properties;
    BLT_Result     result;

    /* check media type */
    if (media_type == NULL || media_type->id != parser->output.media_type.id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* get a reference to the stream properties */
    result = BLT_Stream_GetProperties(&parser->base.context, &stream_properties);
    if (BLT_FAILED(result)) return result;

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(&parser->input.stream);

    /* remember the start of the stream */
    result = ATX_InputStream_Tell(stream, &stream_start);
    if (BLT_FAILED(result)) return result;

    /* get the stream size */
    result = ATX_InputStream_GetSize(stream, &stream_size);
    if (BLT_FAILED(result) || stream_size < (BLT_Size)stream_start) {
        stream_size = 0;
    } else {
        stream_size -= stream_start;
    }

    /* parse the ID3 header and trailer */
    result = BLT_Id3Parser_ParseStream(stream, 
                                       stream_start,
                                       stream_size,
                                       &id3_header_size,
                                       &id3_trailer_size,
                                       &stream_properties);
    if (BLT_SUCCEEDED(result)) {
        header_size = id3_header_size;
        trailer_size = id3_trailer_size;
        if (stream_size >= id3_header_size+id3_trailer_size) {
            stream_size -= id3_header_size+id3_trailer_size;
        }
    }

    /* parse APE tags */
#if 0 /* disable for now */
    result = BLT_ApeParser_ParseStream(stream,
                                       stream_start + header_size,
                                       stream_size,
                                       &ape_trailer_size,
                                       &stream_properties);
    if (BLT_SUCCEEDED(result)) {
        trailer_size += ape_trailer_size;
        if (stream_size >= ape_trailer_size) {
            stream_size -= ape_trailer_size;
        }
    } 
#endif

    if (header_size != 0 || trailer_size != 0) {
        /* create a sub stream without the header and the trailer */
        BLT_Debug("TagParserInputPort_SetStream: substream %ld [%ld - %ld]\n",
                  stream_size, header_size, trailer_size);
        result = ATX_SubInputStream_Create(stream, 
                                           header_size, 
                                           stream_size,
                                           NULL,
                                           &parser->input.stream);        
        if (ATX_FAILED(result)) return result;

        /* update the stream info */
        {
            BLT_StreamInfo info;
            info.size = stream_size;
            if (!ATX_OBJECT_IS_NULL(&parser->base.context)) {
                info.mask = BLT_STREAM_INFO_MASK_SIZE;
                BLT_Stream_SetInfo(&parser->base.context, &info);
            }
        }
    } else {
        /* keep a reference to this stream as-is */
        parser->input.stream = *stream;
        ATX_REFERENCE_OBJECT(stream);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    TagParserInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
TagParserInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                  BLT_Ordinal            index,
                                  const BLT_MediaType**  media_type)
{
    TagParser* parser = (TagParser*)instance;
    
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
TagParserInputPort_BLT_InputStreamUserInterface = {
    TagParserInputPort_GetInterface,
    TagParserInputPort_SetStream
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(TagParserInputPort, 
                                         "input",
                                         STREAM_PULL,
                                         IN)
static const BLT_MediaPortInterface
TagParserInputPort_BLT_MediaPortInterface = {
    TagParserInputPort_GetInterface,
    TagParserInputPort_GetName,
    TagParserInputPort_GetProtocol,
    TagParserInputPort_GetDirection,
    TagParserInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserInputPort)
ATX_INTERFACE_MAP_ADD(TagParserInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(TagParserInputPort, BLT_InputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserInputPort)

/*----------------------------------------------------------------------
|    TagParserOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
TagParserOutputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                   BLT_Ordinal            index,
                                   const BLT_MediaType**  media_type)
{
    TagParser* parser = (TagParser*)instance;
    
    if (index == 0) {
        *media_type = &parser->output.media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       TagParserOutputPort_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
TagParserOutputPort_GetStream(BLT_InputStreamProviderInstance* instance,
                              ATX_InputStream*                 stream)
{
    TagParser* parser = (TagParser*)instance;

    *stream = parser->input.stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(TagParserOutputPort, 
                                         "output",
                                         STREAM_PULL,
                                         OUT)
static const BLT_MediaPortInterface
TagParserOutputPort_BLT_MediaPortInterface = {
    TagParserOutputPort_GetInterface,
    TagParserOutputPort_GetName,
    TagParserOutputPort_GetProtocol,
    TagParserOutputPort_GetDirection,
    TagParserOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamProviderInterface
TagParserOutputPort_BLT_InputStreamProviderInterface = {
    TagParserOutputPort_GetInterface,
    TagParserOutputPort_GetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserOutputPort)
ATX_INTERFACE_MAP_ADD(TagParserOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(TagParserOutputPort, BLT_InputStreamProvider)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserOutputPort)

/*----------------------------------------------------------------------
|    TagParser_Create
+---------------------------------------------------------------------*/
static BLT_Result
TagParser_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_CString              parameters, 
                 ATX_Object*              object)
{
    TagParser* parser;

    BLT_Debug("TagParser::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    parser = ATX_AllocateZeroMemory(sizeof(TagParser));
    if (parser == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&parser->base, module, core);

    /* construct the object */
    BLT_MediaType_Init(&parser->input.media_type,
                       ((TagParserModule*)
                        ATX_INSTANCE(module))->mpeg_audio_type_id);
    ATX_CLEAR_OBJECT(&parser->input.stream);
    parser->output.media_type = parser->input.media_type;
    parser->output.media_type.flags = BLT_TAG_PARSER_MEDIA_TYPE_FLAGS_PARSED;

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)parser;
    ATX_INTERFACE(object) = (ATX_Interface*)&TagParser_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    TagParser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
TagParser_Destroy(TagParser* parser)
{
    BLT_Debug("TagParser::Destroy\n");

    /* release the reference to the stream */
    ATX_RELEASE_OBJECT(&parser->input.stream);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&parser->base);

    /* free the object memory */
    ATX_FreeMemory(parser);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       TagParser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
TagParser_GetPortByName(BLT_MediaNodeInstance* instance,
                        BLT_CString            name,
                        BLT_MediaPort*         port)
{
    TagParser* parser = (TagParser*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)parser;
        ATX_INTERFACE(port) = &TagParserInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)parser;
        ATX_INTERFACE(port) = &TagParserOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    TagParser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
TagParser_Seek(BLT_MediaNodeInstance* instance,
               BLT_SeekMode*          mode,
               BLT_SeekPoint*         point)
{
    TagParser* parser = (TagParser*)instance;
    BLT_Result result;

    /* estimate the seek offset from the other stream parameters */
    result = BLT_Stream_EstimateSeekPoint(&parser->base.context, *mode, point);
    if (BLT_FAILED(result)) return result;

    BLT_Debug("TagParser_Seek: seek offset = %d\n", (int)point->offset);

    /* seek into the input stream (ignore return value) */
    ATX_InputStream_Seek(&parser->input.stream, point->offset);

    /* set the mode so that the nodes down the chaine know the seek has */
    /* already been done on the stream                                  */
    *mode = BLT_SEEK_MODE_IGNORE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
TagParser_BLT_MediaNodeInterface = {
    TagParser_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    TagParser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    TagParser_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(TagParser, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParser)
ATX_INTERFACE_MAP_ADD(TagParser, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(TagParser, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParser)

/*----------------------------------------------------------------------
|       TagParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
TagParserModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    TagParserModule* module = (TagParserModule*)instance;
    BLT_Registry      registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".mp3" file extension */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".mp3",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;

    /* get the type id for "audio/mpeg" */
    result = BLT_Registry_GetIdForName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mpeg",
        &module->mpeg_audio_type_id);
    if (BLT_FAILED(result)) return result;
    
    BLT_Debug("TagParserModule::Attach (audio/mpeg type = %d)\n",
              module->mpeg_audio_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       TagParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
TagParserModule_Probe(BLT_ModuleInstance*      instance, 
                      BLT_Core*                core,
                      BLT_ModuleParametersType parameters_type,
                      BLT_AnyConst             parameters,
                      BLT_Cardinal*            match)
{
    TagParserModule* module = (TagParserModule*)instance;
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* we need the input protocol to b STREAM_PULL and the output */
            /* protocol to be STREAM_PULL                                 */
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

            /* the input type should be 'audio/mpeg' */
            if (constructor->spec.input.media_type->id != 
                module->mpeg_audio_type_id) {
                return BLT_FAILURE;
            }

            /* refuse to parse "parsed" streams (i.e streams that we have */
            /* already parsed                                             */
            if (constructor->spec.input.media_type->flags &
                BLT_TAG_PARSER_MEDIA_TYPE_FLAGS_PARSED) {
                BLT_Debug("TagParserModule::Probe - Already parsed\n");
                return BLT_FAILURE;
            }

            /* the output type should be unspecififed or audio/mpeg */
            if (!(constructor->spec.output.media_type->id ==
                  BLT_MEDIA_TYPE_ID_UNKNOWN) &&
                !(constructor->spec.output.media_type->id ==
                  module->mpeg_audio_type_id)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "TagParser")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 50;
            }

            BLT_Debug("TagParserModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(TagParser)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(TagParser, "Tag Parser", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface TagParserModule_BLT_ModuleInterface = {
    TagParserModule_GetInterface,
    BLT_BaseModule_GetInfo,
    TagParserModule_Attach,
    TagParserModule_CreateInstance,
    TagParserModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define TagParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(TagParserModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserModule) 
ATX_INTERFACE_MAP_ADD(TagParserModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(TagParserModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(TagParserModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_TagParserModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return TagParserModule_Create(object);
}
