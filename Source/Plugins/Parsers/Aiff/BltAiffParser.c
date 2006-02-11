/*****************************************************************
|
|      File: BltAiffParser.c
|
|      AIFF Parser Module
|
|      (c) 2002-2005 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltAiffParser.h"
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
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserModule)
static const BLT_ModuleInterface AiffParserModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParser)
static const BLT_MediaNodeInterface AiffParser_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserInputPort)
static const BLT_MediaPortInterface AiffParserInputPort_BLT_MediaPortInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserOutputPort)
static const BLT_MediaPortInterface AiffParserOutputPort_BLT_MediaPortInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef unsigned long AiffChunkType;

typedef struct {
    BLT_BaseModule base;
    BLT_UInt32     aiff_type_id;
    BLT_UInt32     x_aiff_type_id;
} AiffParserModule;

typedef struct {
    BLT_MediaType media_type;
} AiffParserInputPort;

typedef struct {
    ATX_InputStream  stream;
    BLT_Size         size;
    BLT_PcmMediaType media_type;
} AiffParserOutputPort;

typedef struct {
    BLT_BaseMediaNode    base;
    AiffParserInputPort  input;
    AiffParserOutputPort output;
} AiffParser;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_AIFF_PARSER_PACKET_SIZE      4096
#define BLT_AIFF_CHUNK_HEADER_SIZE       8

#define BLT_AIFF_CHUNK_TYPE_FORM         0x464F524D  /* 'FORM' */
#define BLT_AIFF_CHUNK_TYPE_COMM         0x434F4D4D  /* 'COMM' */
#define BLT_AIFF_CHUNK_TYPE_SSND         0x53534E44  /* 'SSND' */

#define BLT_AIFF_COMM_CHUNK_MIN_DATA_SIZE    18
#define BLT_AIFC_COMM_CHUNK_MIN_DATA_SIZE    22
#define BLT_AIFF_SSND_CHUNK_MIN_SIZE     8

/*----------------------------------------------------------------------
|       Aiff_ReadChunkHeader
+---------------------------------------------------------------------*/
static BLT_Result
Aiff_ReadChunkHeader(ATX_InputStream* stream,
                     AiffChunkType*   chunk_type,
                     ATX_Size*        chunk_size,
                     ATX_Size*        data_size)
{
    unsigned char buffer[BLT_AIFF_CHUNK_HEADER_SIZE];
    ATX_Result    result;

    /* try to read one header */
    result = ATX_InputStream_ReadFully(stream, buffer, sizeof(buffer));
    if (BLT_FAILED(result)) return result;
    
    /* parse chunk type */
    *chunk_type = ATX_BytesToInt32Be(buffer);

    /* compute data size */
    *data_size = ATX_BytesToInt32Be(&buffer[4]);
    
    /* the chunk size is the data size plus padding */
    *chunk_size = *data_size + ((*data_size)&0x1);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       Aiff_ParseExtended
+---------------------------------------------------------------------*/
static unsigned long 
Aiff_ParseExtended(unsigned char * buffer)
{
   unsigned long mantissa;
   unsigned long last = 0;
   unsigned char exp;

   mantissa = ATX_BytesToInt32Be(buffer+2);
   exp = 30 - *(buffer+1);
   while (exp--) {
       last = mantissa;
       mantissa >>= 1;
   }
   if (last & 0x00000001) mantissa++;
   return mantissa;
}

/*----------------------------------------------------------------------
|       AiffParser_OnCommChunk
+---------------------------------------------------------------------*/
static BLT_Result
AiffParser_OnCommChunk(AiffParser*      parser, 
                       ATX_InputStream* stream, 
                       ATX_Size         data_size, 
                       BLT_Boolean      is_aifc)
{
    unsigned char  comm_buffer[BLT_AIFC_COMM_CHUNK_MIN_DATA_SIZE];
    unsigned short num_channels;
    unsigned long  num_sample_frames;
    unsigned short sample_size;
    unsigned long  sample_rate;
    BLT_Size       header_size;
    BLT_Boolean    is_float = BLT_FALSE;
    BLT_Result     result;

    /* read the chunk data */
    if (is_aifc) {
        header_size = BLT_AIFC_COMM_CHUNK_MIN_DATA_SIZE;
    } else {
        header_size = BLT_AIFF_COMM_CHUNK_MIN_DATA_SIZE;
    }
    result = ATX_InputStream_ReadFully(stream, comm_buffer, header_size); 
    if (ATX_FAILED(result)) return result;

    /* skip any unread part */
    if (header_size != data_size) {
        ATX_InputStream_Skip(stream, data_size-header_size);
    }

    /* parse the fields */
    num_channels      = ATX_BytesToInt16Be(comm_buffer);
    num_sample_frames = ATX_BytesToInt32Be(&comm_buffer[2]);
    sample_size       = ATX_BytesToInt16Be(&comm_buffer[6]);
    sample_rate       = Aiff_ParseExtended(&comm_buffer[8]);

    /* check the compression type for AIFC chunks */
    if (is_aifc) {
        if (comm_buffer[18] == 'N' &&
            comm_buffer[19] == 'O' &&
            comm_buffer[20] == 'N' &&
            comm_buffer[21] == 'E') {
            is_float = BLT_FALSE;
        } else if (comm_buffer[18] == 'f' &&
                   comm_buffer[19] == 'l' &&
                   comm_buffer[20] == '3' &&
                   comm_buffer[21] == '2') {
            is_float = BLT_TRUE;
        } else {
            return BLT_ERROR_UNSUPPORTED_FORMAT;
        }
    }

    /* compute sample format */
    sample_size = 8*((sample_size+7)/8);
    if (is_float) {
        parser->output.media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_FLOAT_BE;
    } else {
        parser->output.media_type.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE;
    }

    /* update the format */
    parser->output.media_type.channel_count   = num_channels;
    parser->output.media_type.bits_per_sample = (BLT_UInt8)sample_size;
    parser->output.media_type.sample_rate     = sample_rate;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AiffParser_OnSsndChunk
+---------------------------------------------------------------------*/
static BLT_Result
AiffParser_OnSsndChunk(AiffParser* parser, ATX_InputStream* stream, BLT_Size size)
{
    unsigned char ssnd_buffer[BLT_AIFF_SSND_CHUNK_MIN_SIZE];
    unsigned long offset;
    unsigned long block_size;
    ATX_Offset    position;
    BLT_Result    result;

    /* read the chunk data */
    result = ATX_InputStream_ReadFully(stream, ssnd_buffer, sizeof(ssnd_buffer)); 
    if (ATX_FAILED(result)) return result;

    /* parse the fields */
    offset     = ATX_BytesToInt32Be(ssnd_buffer);
    block_size = ATX_BytesToInt32Be(&ssnd_buffer[4]);

    /* see where we are in the stream */
    result = ATX_InputStream_Tell(stream, &position);
    if (ATX_FAILED(result)) return result;

    /* create a sub stream */
    parser->output.size = size;
    return ATX_SubInputStream_Create(stream, 
                                     position+offset, 
                                     size,
                                     NULL,
                                     &parser->output.stream);
}

/*----------------------------------------------------------------------
|       AiffParser_ParseChunks
+---------------------------------------------------------------------*/
static BLT_Result
AiffParser_ParseChunks(AiffParser*      parser, 
                       ATX_InputStream* stream)
{
    AiffChunkType  chunk_type;
    ATX_Size       chunk_size;
    ATX_Size       data_size;
    BLT_Boolean    have_comm = BLT_FALSE;
    BLT_Boolean    have_ssnd = BLT_FALSE;
    BLT_Boolean    is_aifc = BLT_FALSE;
    BLT_StreamInfo stream_info;
    int            bytes_per_second;
    BLT_Result     result;
    
    /* check that we have a stream */
    if (ATX_OBJECT_IS_NULL(stream)) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* rewind the byte stream */
    ATX_InputStream_Seek(stream, 0);

    /* get the first chunk header */
    result = Aiff_ReadChunkHeader(stream, 
                                  &chunk_type,
                                  &chunk_size, 
                                  &data_size);
    if (BLT_FAILED(result)) return result;

    /* we expect a 'FORM' chunk here */
    if (chunk_type != BLT_AIFF_CHUNK_TYPE_FORM) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* read the FORM type */
    {
        unsigned char type_buffer[4];
        result = ATX_InputStream_ReadFully(stream, type_buffer, 4);
        if (BLT_FAILED(result)) return result;
        
        /* we support 'AIFF' and 'AIFC' */
        if (type_buffer[0] == 'A' &&
            type_buffer[1] == 'I' &&
            type_buffer[2] == 'F') {
            if (type_buffer[3] == 'F') {
                is_aifc = BLT_FALSE;
            } else if (type_buffer[3] == 'C') { 
                is_aifc = BLT_TRUE;
            } else {
                return BLT_ERROR_INVALID_MEDIA_FORMAT;
            }
        } else {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
    }

    /* iterate over all the chunks until we have found COMM and SSND */
    do {
        /* read the next chunk header */
        result = Aiff_ReadChunkHeader(stream, &chunk_type, &chunk_size, &data_size);
        if (BLT_FAILED(result)) return result;
        
        switch (chunk_type) {
            case BLT_AIFF_CHUNK_TYPE_COMM: 
                /* check the chunk size */
                if (data_size < (BLT_Size)(is_aifc ? BLT_AIFC_COMM_CHUNK_MIN_DATA_SIZE :
                                                     BLT_AIFF_COMM_CHUNK_MIN_DATA_SIZE)) {
                    return BLT_ERROR_UNSUPPORTED_FORMAT;
                }

                /* process the chunk */
                result = AiffParser_OnCommChunk(parser, stream, data_size, is_aifc);
                if (BLT_FAILED(result)) return result;
                have_comm = BLT_TRUE;
                break;

            case BLT_AIFF_CHUNK_TYPE_SSND: 
                /* check the chunk size */
                if (data_size < BLT_AIFF_SSND_CHUNK_MIN_SIZE) {
                    if (data_size == 0) {
                        /* this could be caused by a software that wrote a */
                        /* temporary header and was not able to update it  */
                        ATX_Size   input_size;
                        ATX_Offset position;
                        ATX_InputStream_GetSize(stream, &input_size);
                        ATX_InputStream_Tell(stream, &position);
                        if (input_size > (ATX_Size)(position+8)) {
                            data_size = input_size-position-8;
                        }
                    } else {
                        return BLT_ERROR_INVALID_MEDIA_FORMAT;
                    }
                }

                /* process the chunk */
                result = AiffParser_OnSsndChunk(parser, stream, data_size);
                if (BLT_FAILED(result)) return result;
                have_ssnd = BLT_TRUE;
                
                /* stop now if we have all we need */
                if (have_comm) break;

                /* skip the data (this should usually not happen, as the 
                   SSND chunk is typically the last one */
                ATX_InputStream_Skip(stream, data_size-BLT_AIFF_SSND_CHUNK_MIN_SIZE);
                break;

            default:
                /* ignore the chunk */
                result = ATX_InputStream_Skip(stream, chunk_size);
                if (ATX_FAILED(result)) return result;
        }
    } while (!have_comm || !have_ssnd);

    /* update the stream info */
    stream_info.mask =
        BLT_STREAM_INFO_MASK_SIZE          |
        BLT_STREAM_INFO_MASK_SAMPLE_RATE   |
        BLT_STREAM_INFO_MASK_CHANNEL_COUNT |
        BLT_STREAM_INFO_MASK_DATA_TYPE;
    stream_info.sample_rate   = parser->output.media_type.sample_rate;
    stream_info.channel_count = parser->output.media_type.channel_count;
    stream_info.data_type     = "PCM";
    stream_info.size          = data_size;
    bytes_per_second = 
        parser->output.media_type.channel_count *
        parser->output.media_type.sample_rate   *
        parser->output.media_type.bits_per_sample/8;

    if (stream_info.size != 0 && bytes_per_second != 0) {
        ATX_Int64 duration;
        ATX_Int64_Set_Int32(duration, stream_info.size);
        ATX_Int64_Mul_Int32(duration, 1000);
        ATX_Int64_Div_Int32(duration, bytes_per_second);
        stream_info.duration = ATX_Int64_Get_Int32(duration);
        stream_info.mask |= BLT_STREAM_INFO_MASK_DURATION;
    } else {
        stream_info.duration = 0;
    }
    stream_info.nominal_bitrate = 
    stream_info.average_bitrate = 
    stream_info.instant_bitrate = bytes_per_second*8;
    stream_info.mask |= 
        BLT_STREAM_INFO_MASK_NOMINAL_BITRATE |
        BLT_STREAM_INFO_MASK_AVERAGE_BITRATE |
        BLT_STREAM_INFO_MASK_INSTANT_BITRATE;
    if (stream_info.mask && !ATX_OBJECT_IS_NULL(&parser->base.context)) {
        BLT_Stream_SetInfo(&parser->base.context, &stream_info);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AiffParserInputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParserInputPort_SetStream(BLT_InputStreamUserInstance* instance,
                              ATX_InputStream*             stream,
                              const BLT_MediaType*         media_type)
{
    AiffParser* parser = (AiffParser*)instance;
    BLT_COMPILER_UNUSED(media_type);

    /* check media type */
    if (media_type == NULL || media_type->id != parser->input.media_type.id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(&parser->output.stream);

    /* parse the chunks */
    return AiffParser_ParseChunks(parser, stream);
}

/*----------------------------------------------------------------------
|    AiffParserInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParserInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                   BLT_Ordinal            index,
                                   const BLT_MediaType**  media_type)
{
    AiffParser* parser = (AiffParser*)instance;
    
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
AiffParserInputPort_BLT_InputStreamUserInterface = {
    AiffParserInputPort_GetInterface,
    AiffParserInputPort_SetStream
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AiffParserInputPort, 
                                         "input",
                                         STREAM_PULL,
                                         IN)
static const BLT_MediaPortInterface
AiffParserInputPort_BLT_MediaPortInterface = {
    AiffParserInputPort_GetInterface,
    AiffParserInputPort_GetName,
    AiffParserInputPort_GetProtocol,
    AiffParserInputPort_GetDirection,
    AiffParserInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserInputPort)
ATX_INTERFACE_MAP_ADD(AiffParserInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(AiffParserInputPort, BLT_InputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserInputPort)

/*----------------------------------------------------------------------
|    AiffParserOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParserOutputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                    BLT_Ordinal            index,
                                    const BLT_MediaType**  media_type)
{
    AiffParser* parser = (AiffParser*)instance;
    
    if (index == 0) {
        *media_type = (BLT_MediaType*)&parser->output.media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       AiffParserOutputPort_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParserOutputPort_GetStream(BLT_InputStreamProviderInstance* instance,
                               ATX_InputStream*                 stream)
{
    AiffParser* parser = (AiffParser*)instance;

    /* ensure we're at the start of the stream */
    ATX_InputStream_Seek(&parser->output.stream, 0);

    /* return the stream */
    *stream = parser->output.stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AiffParserOutputPort, 
                                         "output",
                                         STREAM_PULL,
                                         OUT)
static const BLT_MediaPortInterface
AiffParserOutputPort_BLT_MediaPortInterface = {
    AiffParserOutputPort_GetInterface,
    AiffParserOutputPort_GetName,
    AiffParserOutputPort_GetProtocol,
    AiffParserOutputPort_GetDirection,
    AiffParserOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamProviderInterface
AiffParserOutputPort_BLT_InputStreamProviderInterface = {
    AiffParserOutputPort_GetInterface,
    AiffParserOutputPort_GetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserOutputPort)
ATX_INTERFACE_MAP_ADD(AiffParserOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(AiffParserOutputPort, BLT_InputStreamProvider)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserOutputPort)

/*----------------------------------------------------------------------
|    AiffParser_Create
+---------------------------------------------------------------------*/
static BLT_Result
AiffParser_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  ATX_Object*              object)
{
    AiffParser* parser;

    BLT_Debug("AiffParser::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    parser = ATX_AllocateZeroMemory(sizeof(AiffParser));
    if (parser == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&parser->base, module, core);

    /* construct the object */
    BLT_MediaType_Init(&parser->input.media_type, 
                       ((AiffParserModule*)ATX_INSTANCE(module))->aiff_type_id);
    ATX_CLEAR_OBJECT(&parser->output.stream);
    BLT_PcmMediaType_Init(&parser->output.media_type);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)parser;
    ATX_INTERFACE(object) = (ATX_Interface*)&AiffParser_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AiffParser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AiffParser_Destroy(AiffParser* parser)
{
    BLT_Debug("AiffParser::Destroy\n");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(&parser->output.stream);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&parser->base);

    /* free the object memory */
    ATX_FreeMemory(parser);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AiffParser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParser_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    AiffParser* parser = (AiffParser*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)parser;
        ATX_INTERFACE(port) = &AiffParserInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)parser;
        ATX_INTERFACE(port) = &AiffParserOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    AiffParser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParser_Seek(BLT_MediaNodeInstance* instance,
                BLT_SeekMode*          mode,
                BLT_SeekPoint*         point)
{
    AiffParser* parser = (AiffParser*)instance;

    /* estimate the seek point */
    if (ATX_OBJECT_IS_NULL(&parser->base.context)) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(&parser->base.context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_SAMPLE) ||
        !(point->mask & BLT_SEEK_POINT_MASK_OFFSET)) {
        return BLT_FAILURE;
    }

    /* align the offset to the nearest sample */
    point->offset -= point->offset%4;

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
AiffParser_BLT_MediaNodeInterface = {
    AiffParser_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    AiffParser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    AiffParser_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AiffParser, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParser)
ATX_INTERFACE_MAP_ADD(AiffParser, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(AiffParser, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParser)

/*----------------------------------------------------------------------
|       AiffParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParserModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    AiffParserModule* module = (AiffParserModule*)instance;
    BLT_Registry      registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the file extensions */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".aif",
                                            "audio/aiff");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".aiff",
                                            "audio/aiff");
    if (BLT_FAILED(result)) return result;
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".aifc",
                                            "audio/aiff");
    if (BLT_FAILED(result)) return result;

    /* get the type id for "audio/aiff" and "audio/x-aiff" */
    result = BLT_Registry_GetIdForName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/aiff",
        &module->aiff_type_id);
    if (BLT_FAILED(result)) return result;
    
    result = BLT_Registry_RegisterName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-aiff",
        &module->x_aiff_type_id);
    if (BLT_FAILED(result)) return result;

    BLT_Debug("AiffParserModule::Attach (audio/aiff type = %d\n",
              module->aiff_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       AiffParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AiffParserModule_Probe(BLT_ModuleInstance*      instance, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    AiffParserModule* module = (AiffParserModule*)instance;
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

            /* we need the input media type to be 'audio/aiff' or 'audio/x-aiff' */
            if (constructor->spec.input.media_type->id != module->aiff_type_id &&
                constructor->spec.input.media_type->id != module->x_aiff_type_id) {
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
                if (ATX_StringsEqual(constructor->name, "AiffParser")) {
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

            BLT_Debug("AiffParserModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AiffParser)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(AiffParser, "AIFF Parser", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface AiffParserModule_BLT_ModuleInterface = {
    AiffParserModule_GetInterface,
    BLT_BaseModule_GetInfo,
    AiffParserModule_Attach,
    AiffParserModule_CreateInstance,
    AiffParserModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AiffParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(AiffParserModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserModule) 
ATX_INTERFACE_MAP_ADD(AiffParserModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(AiffParserModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(AiffParserModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_AiffParserModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return AiffParserModule_Create(object);
}
