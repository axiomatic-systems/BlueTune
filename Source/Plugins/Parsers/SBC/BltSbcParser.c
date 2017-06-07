/*****************************************************************
|
|   SBC Parser Module
|
|   (c) 2002-2016 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltSbcParser.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltByteStreamUser.h"
#include "BltPacketProducer.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.parsers.sbc")

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_MediaTypeId sbc_type_id;
} SbcParserModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_InputStreamUser);
    
    /* members */
    ATX_InputStream* stream;
} SbcParserInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);
} SbcParserOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    SbcParserInput  input;
    SbcParserOutput output;
    BLT_MediaType   media_type;
    struct {
        unsigned int channels;
        unsigned int sampling_frequency;
        unsigned int bitrate;
    } stream_info;
} SbcParser;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_SBC_HEADER_SYNC_WORD          0x9C
#define BLT_SBC_CHANNEL_MODE_MONO         0
#define BLT_SBC_CHANNEL_MODE_DUAL_CHANNEL 1
#define BLT_SBC_CHANNEL_MODE_STEREO       2
#define BLT_SBC_CHANNEL_MODE_JOINT_STEREO 3

const unsigned int BLT_SbcSamplingFrequencies[4] = {
    16000,
    22050,
    44100,
    48000
};

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(SbcParserModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(SbcParser, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(SbcParser, ATX_Referenceable)

/*----------------------------------------------------------------------
|   SbcParserInput_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParserInput_SetStream(BLT_InputStreamUser* _self,
                         ATX_InputStream*     stream,
                         const BLT_MediaType* media_type)
{
    SbcParser* self = ATX_SELF_M(input, SbcParser, BLT_InputStreamUser);

    /* check media type */
    if (media_type == NULL || media_type->id != self->media_type.id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* keep a reference to the stream */
    self->input.stream = stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcParserInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParserInput_QueryMediaType(BLT_MediaPort*        _self,
                              BLT_Ordinal           index,
                              const BLT_MediaType** media_type)
{
    SbcParser* self = ATX_SELF_M(input, SbcParser, BLT_MediaPort);
    
    if (index == 0) {
        *media_type = &self->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcParserInput)
    ATX_GET_INTERFACE_ACCEPT(SbcParserInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SbcParserInput, BLT_InputStreamUser)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcParserInput, BLT_InputStreamUser)
    SbcParserInput_SetStream
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SbcParserInput, 
                                         "input",
                                         STREAM_PULL,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(SbcParserInput, BLT_MediaPort)
    SbcParserInput_GetName,
    SbcParserInput_GetProtocol,
    SbcParserInput_GetDirection,
    SbcParserInput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
 |   AdtsParserOutput_GetPacket
 +---------------------------------------------------------------------*/
BLT_METHOD
SbcParserOutput_GetPacket(BLT_PacketProducer* _self,
                          BLT_MediaPacket**   packet)
{
    SbcParser*   self = ATX_SELF_M(output, SbcParser, BLT_PacketProducer);
    BLT_UInt8    header[4];
    unsigned int frame_length = 0;
    BLT_UInt8*   packet_data = NULL;
    BLT_Result   result;
    
    /* default value */
    *packet = NULL;

    /* read the first4 bytes of header */
    result = ATX_InputStream_ReadFully(self->input.stream, header, 4);
    if (BLT_FAILED(result)) {
        if (result == ATX_ERROR_EOS) {
            return BLT_ERROR_EOS;
        } else {
            return result;
        }
    }
    
    /* check the sync word */
    if (header[0] != BLT_SBC_HEADER_SYNC_WORD) {
        ATX_LOG_FINE_1("invalid sync word (%02x)", header[0]);
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    
    /* compute the stream info and frame length */
    {
        unsigned int sampling_frequency = BLT_SbcSamplingFrequencies[(header[1] >> 6) & 3];
        unsigned int blocks             = 4*(1+((header[1] >> 4) & 3));
        unsigned int channel_mode       = (header[1] >> 2) & 3;
        unsigned int channels           = channel_mode == BLT_SBC_CHANNEL_MODE_MONO ? 1 : 2;
        /* unsigned int allocation_method  = (header[1] >> 1) & 1; */
        unsigned int subbands           = ((header[1]) & 1) ? 8 : 4;
        unsigned int bitpool            = header[2];
        unsigned int bitrate;
        
        frame_length = 4 + (4 * subbands * channels) / 8;
        if (channel_mode == BLT_SBC_CHANNEL_MODE_MONO || channel_mode == BLT_SBC_CHANNEL_MODE_DUAL_CHANNEL) {
            frame_length +=  (blocks * channels * bitpool) / 8;
        } else {
            frame_length += ((channel_mode == BLT_SBC_CHANNEL_MODE_JOINT_STEREO ? 1 : 0) * subbands + blocks * bitpool) / 8;
        }
        
        bitrate = 8 * (frame_length * sampling_frequency)/(subbands*blocks);
        
        /* update the stream info */
        {
            BLT_StreamInfo stream_info;
            stream_info.mask = BLT_STREAM_INFO_MASK_DATA_TYPE | BLT_STREAM_INFO_MASK_TYPE;
            stream_info.data_type = "SBC";
            stream_info.type      = BLT_STREAM_TYPE_AUDIO;

            if (channels != self->stream_info.channels) {
                self->stream_info.channels = channels;
                stream_info.mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
                stream_info.channel_count = channels;
            }

            if (sampling_frequency != self->stream_info.sampling_frequency) {
                self->stream_info.sampling_frequency = sampling_frequency;
                stream_info.mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;
                stream_info.sample_rate = sampling_frequency;
            }

            if (bitrate != self->stream_info.bitrate) {
                self->stream_info.bitrate = bitrate;
                stream_info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE |
                                    BLT_STREAM_INFO_MASK_AVERAGE_BITRATE |
                                    BLT_STREAM_INFO_MASK_INSTANT_BITRATE;
                stream_info.nominal_bitrate = bitrate;
                stream_info.average_bitrate = bitrate;
                stream_info.instant_bitrate = bitrate;
            }

            if (stream_info.mask && ATX_BASE(self, BLT_BaseMediaNode).context) {
                BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);
            }

        }
    }
    
    /* allocate a packet */
    result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                        frame_length,
                                        &self->media_type,
                                        packet);
    if (BLT_FAILED(result)) {
        return result;
    }
    
    /* copy data into the packet buffer */
    BLT_MediaPacket_SetPayloadSize(*packet, frame_length);
    packet_data = (BLT_UInt8*)BLT_MediaPacket_GetPayloadBuffer(*packet);
    ATX_CopyMemory(packet_data, header, 4);
    result = ATX_InputStream_ReadFully(self->input.stream, packet_data+4, frame_length-4);
    if (BLT_FAILED(result)) {
        BLT_MediaPacket_Release(*packet);
        return result;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcParserOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParserOutput_QueryMediaType(BLT_MediaPort*        _self,
                               BLT_Ordinal           index,
                               const BLT_MediaType** media_type)
{
    SbcParser* self = ATX_SELF_M(output, SbcParser, BLT_MediaPort);
    
    if (index == 0) {
        *media_type = &self->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcParserOutput)
    ATX_GET_INTERFACE_ACCEPT(SbcParserOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SbcParserOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SbcParserOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(SbcParserOutput, BLT_MediaPort)
    SbcParserOutput_GetName,
    SbcParserOutput_GetProtocol,
    SbcParserOutput_GetDirection,
    SbcParserOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcParserOutput, BLT_PacketProducer)
    SbcParserOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   SbcParser_Create
+---------------------------------------------------------------------*/
static BLT_Result
SbcParser_Create(BLT_Module*              module,
                 BLT_Core*                core,
                 BLT_ModuleParametersType parameters_type,
                 BLT_CString              parameters,
                 BLT_MediaNode**          object)
{
    SbcParser* self;

    ATX_LOG_FINE("SbcParser::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(SbcParser));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */
    BLT_MediaType_Init(&self->media_type, ((SbcParserModule*)module)->sbc_type_id);

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, SbcParser, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, SbcParser, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  SbcParserInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  SbcParserInput,  BLT_InputStreamUser);
    ATX_SET_INTERFACE(&self->output, SbcParserOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, SbcParserOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcParser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
SbcParser_Destroy(SbcParser* self)
{
    ATX_LOG_FINE("SbcParser::Destroy");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(self->input.stream);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SbcParser_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParser_Deactivate(BLT_MediaNode* _self)
{
    SbcParser* self = ATX_SELF_EX(SbcParser, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_FINER("SbcParser::Deactivate");

    /* release the stream */
    ATX_RELEASE_OBJECT(self->input.stream);

    /* call the base class method */
    BLT_BaseMediaNode_Deactivate(_self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcParser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParser_GetPortByName(BLT_MediaNode*  _self,
                        BLT_CString     name,
                        BLT_MediaPort** port)
{
    SbcParser* self = ATX_SELF_EX(SbcParser, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(&self->input, BLT_MediaPort);
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        *port = &ATX_BASE(&self->output, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|   SbcParser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParser_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    SbcParser* self = ATX_SELF_EX(SbcParser, BLT_BaseMediaNode, BLT_MediaNode);

    /* estimate the seek point */
    if (ATX_BASE(self, BLT_BaseMediaNode).context == NULL) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(ATX_BASE(self, BLT_BaseMediaNode).context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_OFFSET)) {
        return BLT_FAILURE;
    }

    /* seek to the estimated offset */
    /* seek into the input stream (ignore return value) */
    ATX_InputStream_Seek(self->input.stream, point->offset);
    
    /* set the mode so that the nodes down the chain know the seek has */
    /* already been done on the stream                                  */
    *mode = BLT_SEEK_MODE_IGNORE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcParser)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcParser, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcParser, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SbcParser, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    SbcParser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    SbcParser_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    SbcParser_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SbcParser, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   SbcParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParserModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    SbcParserModule* self = ATX_SELF_EX(SbcParserModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*     registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".sbc" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".sbc",
                                            "audio/SBC");
    if (BLT_FAILED(result)) return result;

    /* get the type id for "audio/SBC" */
    result = BLT_Registry_GetIdForName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/SBC",
        &self->sbc_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("Sbc Parser Module::Attach (audio/SBC type = %d)", self->sbc_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
SbcParserModule_Probe(BLT_Module*              _self, 
                      BLT_Core*                core,
                      BLT_ModuleParametersType parameters_type,
                      BLT_AnyConst             parameters,
                      BLT_Cardinal*            match)
{
    SbcParserModule* self = ATX_SELF_EX(SbcParserModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* we need the input protocol to be STREAM_PULL and the output */
            /* protocol to be PACKET                                       */
             if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* we need the input media type to be 'audio/SBC' */
            if (constructor->spec.input.media_type->id != self->sbc_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unknown or 'audio/SBC' at this point */
            if (constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN &&
                constructor->spec.output.media_type->id != self->sbc_type_id) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "SbcParser")) {
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

            ATX_LOG_FINE_1("SbcParserModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcParserModule)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcParserModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcParserModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(SbcParserModule, SbcParser)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SbcParserModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    SbcParserModule_Attach,
    SbcParserModule_CreateInstance,
    SbcParserModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define SbcParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SbcParserModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(SbcParserModule,
                                         "SBC Parser",
                                         "com.axiosys.parser.sbc",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
