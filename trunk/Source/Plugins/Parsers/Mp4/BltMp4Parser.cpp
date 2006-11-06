/*****************************************************************
|
|   MP4 Parser Module
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "AP4.h"
#include "Atomix.h"
#include "BltConfig.h"
#include "BltMp4Parser.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltByteStreamUser.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.parsers.mp4")

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
struct Mp4ParserModule {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 mp4_type_id;
    BLT_UInt32 mp4es_type_id;
};

typedef struct {
    BLT_MediaType base;
    unsigned int  object_type_id;
    unsigned int  decoder_info_length;
    /* variable size array follows */
    unsigned char decoder_info[1]; /* could be more than 1 byte */
} BLT_Mpeg4AudioMediaType;

class Mp4StreamAdapter : public AP4_ByteStream {
public:
    Mp4StreamAdapter(ATX_InputStream* stream);

    // AP4_ByteStream methods
    virtual AP4_Result Read(void*     buffer, 
                            AP4_Size  bytes_to_read, 
                            AP4_Size* bytes_read);
    virtual AP4_Result Write(const void* buffer, 
                             AP4_Size    bytes_to_write, 
                             AP4_Size*   bytes_written);
    virtual AP4_Result Seek(AP4_Position position);
    virtual AP4_Result Tell(AP4_Position& position);
    virtual AP4_Result GetSize(AP4_Size& size);

    // AP4_Referenceable methods
    virtual void AddReference();
    virtual void Release();

private:
    // methods
    ~Mp4StreamAdapter();
    AP4_Result MapResult(ATX_Result result);

    // members
    ATX_InputStream* m_Stream;
    ATX_Cardinal     m_ReferenceCount;
};

// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_InputStreamUser);

    /* members */
    BLT_MediaType media_type;
    AP4_File*     mp4_file;
    AP4_Track*    mp4_track;
} Mp4ParserInput;

// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_Mpeg4AudioMediaType* media_type;
    BLT_Ordinal              sample;
    AP4_DataBuffer*          sample_buffer;
} Mp4ParserOutput;

// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    Mp4ParserInput  input;
    Mp4ParserOutput output;
    BLT_UInt32      mp4_type_id;
    BLT_UInt32      mp4es_type_id;
} Mp4Parser;

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::Mp4StreamAdapter
+---------------------------------------------------------------------*/
Mp4StreamAdapter::Mp4StreamAdapter(ATX_InputStream* stream) :
    m_Stream(stream),
    m_ReferenceCount(1)
{
    ATX_REFERENCE_OBJECT(stream);
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::~Mp4StreamAdapter
+---------------------------------------------------------------------*/
Mp4StreamAdapter::~Mp4StreamAdapter()
{
    ATX_RELEASE_OBJECT(m_Stream);
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::MapResult
+---------------------------------------------------------------------*/
AP4_Result
Mp4StreamAdapter::MapResult(ATX_Result result)
{
    switch (result) {
        case ATX_ERROR_EOS: return AP4_ERROR_EOS;
        default: return result;
    }
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::AddReference
+---------------------------------------------------------------------*/
void
Mp4StreamAdapter::AddReference()
{
    ++m_ReferenceCount;
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::Release
+---------------------------------------------------------------------*/
void
Mp4StreamAdapter::Release()
{
    if (--m_ReferenceCount == 0) {
        delete this;
    }
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::Read
+---------------------------------------------------------------------*/
AP4_Result
Mp4StreamAdapter::Read(void* buffer, AP4_Size bytes_to_read, AP4_Size* bytes_read)
{
    ATX_Result result = ATX_InputStream_ReadFully(m_Stream, buffer, bytes_to_read);
    if (ATX_SUCCEEDED(result) && bytes_read) *bytes_read = bytes_to_read;
    return MapResult(result);
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::Write
+---------------------------------------------------------------------*/
AP4_Result
Mp4StreamAdapter::Write(const void*, AP4_Size, AP4_Size*)
{
    return AP4_FAILURE;
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::Seek
+---------------------------------------------------------------------*/
AP4_Result
Mp4StreamAdapter::Seek(AP4_Position position)
{
    return MapResult(ATX_InputStream_Seek(m_Stream, position));
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::Tell
+---------------------------------------------------------------------*/
AP4_Result
Mp4StreamAdapter::Tell(AP4_Position& position)
{
    return MapResult(ATX_InputStream_Tell(m_Stream, &position));
}

/*----------------------------------------------------------------------
|   Mp4StreamAdapter::GetSize
+---------------------------------------------------------------------*/
AP4_Result
Mp4StreamAdapter::GetSize(AP4_Size& size)
{
    return MapResult(ATX_InputStream_GetSize(m_Stream, &size));
}

/*----------------------------------------------------------------------
|   Mp4ParserInput_Construct
+---------------------------------------------------------------------*/
static void
Mp4ParserInput_Construct(Mp4ParserInput* self, BLT_Module* module)
{
    Mp4ParserModule* mp4_parser_module = (Mp4ParserModule*)module;
    BLT_MediaType_Init(&self->media_type, mp4_parser_module->mp4_type_id);
    self->mp4_file = NULL;
    self->mp4_track = NULL;
}

/*----------------------------------------------------------------------
|   Mp4ParserInput_Destruct
+---------------------------------------------------------------------*/
static void
Mp4ParserInput_Destruct(Mp4ParserInput* self)
{
    delete self->mp4_file;
}

/*----------------------------------------------------------------------
|   Mp4ParserInput_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4ParserInput_SetStream(BLT_InputStreamUser* _self,
                         ATX_InputStream*     stream,
                         const BLT_MediaType* media_type)
{
    Mp4Parser* self = ATX_SELF_M(input, Mp4Parser, BLT_InputStreamUser);
    BLT_COMPILER_UNUSED(media_type);

    /* check media type */
    if (media_type == NULL || media_type->id != self->input.media_type.id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* if we had a file before, release it now */
    delete self->input.mp4_file;

    /* parse the MP4 file */
    ATX_LOG_FINE("Mp4ParserInput::SetStream - parsing MP4 file");
    Mp4StreamAdapter* stream_adapter = new Mp4StreamAdapter(stream);
    self->input.mp4_file = new AP4_File(*stream_adapter);
    stream_adapter->Release();

    /* find the audio track */
    AP4_Movie* movie = self->input.mp4_file->GetMovie();
    if (movie == NULL) {
        ATX_LOG_FINE("Mp4ParserInput::SetStream - no movie in file");
        goto fail;
    }
    self->input.mp4_track = movie->GetTrack(AP4_Track::TYPE_AUDIO);
    if (self->input.mp4_track == NULL) {
        ATX_LOG_FINE("Mp4ParserInput::SetStream - no audio track found");
        goto fail;
    }

    // check that the track is of the right type
    AP4_SampleDescription* sdesc = self->input.mp4_track->GetSampleDescription(0);
    if (sdesc == NULL) {
        ATX_LOG_FINE("Mp4ParserInput::SetStream - no sample description");
        goto fail;
    }
    if (sdesc->GetType() != AP4_SampleDescription::TYPE_MPEG) {
        ATX_LOG_FINE("Mp4ParserInput::SetStream - not an MPEG audio track");
        goto fail;
    }
    AP4_MpegSampleDescription* mpeg_desc = dynamic_cast<AP4_MpegSampleDescription*>(sdesc);
    AP4_AudioSampleDescription* audio_desc = dynamic_cast<AP4_AudioSampleDescription*>(sdesc);
    if (mpeg_desc == NULL || audio_desc == NULL) {
        ATX_LOG_FINE("Mp4ParserInput::SetStream - cannot find audio description or mpeg description");
        goto fail;
    }

    const AP4_DataBuffer* decoder_info = mpeg_desc->GetDecoderInfo();
    if (decoder_info == NULL) {
        ATX_LOG_FINE("Mp4ParserInput::SetStream - no decoder info");
        goto fail;
    }

    // update the stream
    BLT_StreamInfo stream_info;
    stream_info.duration = self->input.mp4_track->GetDurationMs();
    stream_info.channel_count   = audio_desc->GetChannelCount();
    stream_info.sample_rate     = audio_desc->GetSampleRate();
    stream_info.data_type = mpeg_desc->GetObjectTypeString(mpeg_desc->GetObjectTypeId());
    stream_info.average_bitrate = mpeg_desc->GetAvgBitrate();
    stream_info.nominal_bitrate = mpeg_desc->GetAvgBitrate();
    stream_info.mask = BLT_STREAM_INFO_MASK_DURATION        |
                       BLT_STREAM_INFO_MASK_AVERAGE_BITRATE |
                       BLT_STREAM_INFO_MASK_NOMINAL_BITRATE |
                       BLT_STREAM_INFO_MASK_CHANNEL_COUNT   |
                       BLT_STREAM_INFO_MASK_SAMPLE_RATE     |
                       BLT_STREAM_INFO_MASK_DATA_TYPE;
    BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);

    // setup the output media type
    self->output.media_type = (BLT_Mpeg4AudioMediaType*)ATX_AllocateZeroMemory(sizeof(BLT_Mpeg4AudioMediaType)+decoder_info->GetDataSize());
    BLT_MediaType_Init(&self->output.media_type->base, self->mp4es_type_id);
    self->output.media_type->base.extension_size = sizeof(BLT_Mpeg4AudioMediaType)-sizeof(BLT_MediaType);
    self->output.media_type->object_type_id      = mpeg_desc->GetObjectTypeId();
    self->output.media_type->decoder_info_length =  decoder_info->GetDataSize();
    ATX_CopyMemory(&self->output.media_type->decoder_info[0], decoder_info->GetData(), decoder_info->GetDataSize());

    return BLT_SUCCESS;

fail:
    delete self->input.mp4_file;
    self->input.mp4_file = NULL;
    return BLT_ERROR_INVALID_MEDIA_FORMAT;
}

/*----------------------------------------------------------------------
|   Mp4ParserInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4ParserInput_QueryMediaType(BLT_MediaPort*        _self,
                              BLT_Ordinal           index,
                              const BLT_MediaType** media_type)
{
    Mp4ParserInput* self = ATX_SELF(Mp4ParserInput, BLT_MediaPort);
    
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Mp4ParserInput)
    ATX_GET_INTERFACE_ACCEPT(Mp4ParserInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(Mp4ParserInput, BLT_InputStreamUser)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Mp4ParserInput, BLT_InputStreamUser)
    Mp4ParserInput_SetStream
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(Mp4ParserInput, 
                                         "input",
                                         STREAM_PULL,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(Mp4ParserInput, BLT_MediaPort)
    Mp4ParserInput_GetName,
    Mp4ParserInput_GetProtocol,
    Mp4ParserInput_GetDirection,
    Mp4ParserInput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   Mp4ParserOutput_Construct
+---------------------------------------------------------------------*/
static void
Mp4ParserOutput_Construct(Mp4ParserOutput* self)
{
    self->media_type    = NULL;
    self->sample        = 0;
    self->sample_buffer = new AP4_DataBuffer();
}

/*----------------------------------------------------------------------
|   Mp4ParserOutput_Destruct
+---------------------------------------------------------------------*/
static void
Mp4ParserOutput_Destruct(Mp4ParserOutput* self)
{
    /* release the sample buffer */
    delete self->sample_buffer;

    /* free the media type extensions */
    BLT_MediaType_Free((BLT_MediaType*)self->media_type);
}

/*----------------------------------------------------------------------
|   Mp4ParserOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4ParserOutput_QueryMediaType(BLT_MediaPort*        _self,
                               BLT_Ordinal           index,
                               const BLT_MediaType** media_type)
{
    Mp4ParserOutput* self = ATX_SELF(Mp4ParserOutput, BLT_MediaPort);
    
    if (index == 0) {
        *media_type = (BLT_MediaType*)self->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   Mp4ParserOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4ParserOutput_GetPacket(BLT_PacketProducer* _self,
                          BLT_MediaPacket**   packet)
{
    Mp4Parser* self = ATX_SELF_M(output, Mp4Parser, BLT_PacketProducer);

    *packet = NULL;
    if (self->input.mp4_track == NULL) {
        return BLT_ERROR_PORT_HAS_NO_DATA;
    } else {
        AP4_Sample sample;
        AP4_Result result = self->input.mp4_track->ReadSample(self->output.sample, sample, *self->output.sample_buffer);
        if (AP4_FAILED(result)) return BLT_ERROR_PORT_HAS_NO_DATA;

        AP4_Size packet_size = self->output.sample_buffer->GetDataSize();
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                            packet_size,
                                            (BLT_MediaType*)self->output.media_type,
                                            packet);
        if (BLT_FAILED(result)) return result;
        BLT_MediaPacket_SetPayloadSize(*packet, packet_size);
        void* buffer = BLT_MediaPacket_GetPayloadBuffer(*packet);
        ATX_CopyMemory(buffer, self->output.sample_buffer->GetData(), packet_size);
        self->output.sample++;

        return BLT_SUCCESS;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Mp4ParserOutput)
    ATX_GET_INTERFACE_ACCEPT(Mp4ParserOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(Mp4ParserOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(Mp4ParserOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(Mp4ParserOutput, BLT_MediaPort)
    Mp4ParserOutput_GetName,
    Mp4ParserOutput_GetProtocol,
    Mp4ParserOutput_GetDirection,
    Mp4ParserOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Mp4ParserOutput, BLT_PacketProducer)
    Mp4ParserOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   Mp4Parser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Mp4Parser_Destroy(Mp4Parser* self)
{
    ATX_LOG_FINE("Mp4Parser::Destroy");

    /* destruct the members */
    Mp4ParserInput_Destruct(&self->input);
    Mp4ParserOutput_Destruct(&self->output);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    delete self;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Mp4Parser_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4Parser_Deactivate(BLT_MediaNode* _self)
{
    ATX_LOG_FINER("Mp4Parser::Deactivate");

    /* call the base class method */
    BLT_BaseMediaNode_Deactivate(_self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   Mp4Parser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4Parser_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    Mp4Parser* self = ATX_SELF_EX(Mp4Parser, BLT_BaseMediaNode, BLT_MediaNode);

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
|   Mp4Parser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4Parser_Seek(BLT_MediaNode* /*_self*/,
               BLT_SeekMode*  /*mode*/,
               BLT_SeekPoint* /*point*/)
{
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Mp4Parser)
    ATX_GET_INTERFACE_ACCEPT_EX(Mp4Parser, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(Mp4Parser, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(Mp4Parser, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    Mp4Parser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    Mp4Parser_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    Mp4Parser_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(Mp4Parser, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   Mp4Parser_Construct
+---------------------------------------------------------------------*/
static void
Mp4Parser_Construct(Mp4Parser* self, BLT_Module* module, BLT_Core* core)
{
    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the members */
    Mp4ParserInput_Construct(&self->input, module);
    Mp4ParserOutput_Construct(&self->output);

    /* setup media types */
    Mp4ParserModule* mp4_parser_module = (Mp4ParserModule*)module;
    self->mp4_type_id   = mp4_parser_module->mp4_type_id;
    self->mp4es_type_id = mp4_parser_module->mp4es_type_id;

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, Mp4Parser, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, Mp4Parser, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  Mp4ParserInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  Mp4ParserInput,  BLT_InputStreamUser);
    ATX_SET_INTERFACE(&self->output, Mp4ParserOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, Mp4ParserOutput, BLT_PacketProducer);
}

/*----------------------------------------------------------------------
|   Mp4Parser_Create
+---------------------------------------------------------------------*/
static BLT_Result
Mp4Parser_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_AnyConst             parameters, 
                 BLT_MediaNode**          object)
{
    Mp4Parser* self;

    ATX_LOG_FINE("Mp4Parser::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate the object */
    self = new Mp4Parser();
    Mp4Parser_Construct(self, module, core);

    /* return the object */
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   Mp4ParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4ParserModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    Mp4ParserModule* self = ATX_SELF_EX(Mp4ParserModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*    registry;
    BLT_Result       result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".mp4" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mp4",
                                            "audio/mp4");
    if (BLT_FAILED(result)) return result;

    /* register the ".m4a" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".m4a",
                                            "audio/mp4");
    if (BLT_FAILED(result)) return result;

    /* get the type id for "audio/mp4" */
    result = BLT_Registry_GetIdForName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mp4",
        &self->mp4_type_id);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1("MP4 Parser Module::Attach (audio/mp4 type = %d", self->mp4_type_id);
    
    /* register the type id for "audio/vnd.bluetune.mp4-es" */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/vnd.bluetune.mp4-es",
        &self->mp4es_type_id);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1("MP4 Parser Module::Attach (audio/vnd.bluetune.mp4-es type = %d", self->mp4es_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   Mp4ParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
Mp4ParserModule_Probe(BLT_Module*              _self, 
                      BLT_Core*                core,
                      BLT_ModuleParametersType parameters_type,
                      BLT_AnyConst             parameters,
                      BLT_Cardinal*            match)
{
    Mp4ParserModule* self = ATX_SELF_EX(Mp4ParserModule, BLT_BaseModule, BLT_Module);
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

            /* we need the input media type to be 'audio/mp4' */
            if (constructor->spec.input.media_type->id != self->mp4_type_id) {
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
                if (ATX_StringsEqual(constructor->name, "Mp4Parser")) {
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

            ATX_LOG_FINE_1("Mp4ParserModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Mp4ParserModule)
    ATX_GET_INTERFACE_ACCEPT_EX(Mp4ParserModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(Mp4ParserModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(Mp4ParserModule, Mp4Parser)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(Mp4ParserModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    Mp4ParserModule_Attach,
    Mp4ParserModule_CreateInstance,
    Mp4ParserModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define Mp4ParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(Mp4ParserModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   node constructor
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(Mp4ParserModule, "MP4 Parser", 0)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Mp4ParserModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return Mp4ParserModule_Create(object);
}
