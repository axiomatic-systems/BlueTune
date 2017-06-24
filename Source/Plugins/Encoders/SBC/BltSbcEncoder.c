/*****************************************************************
|
|   BlueTune - SBC Encoder Module
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
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltReplayGain.h"
#include "BltCommonMediaTypes.h"

#include "BltSbcEncoder.h"

#include "sbc_encoder.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.encoders.sbc")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_SBC_ENCODER_PCM_BUFFER_SIZE 512
#define BLT_SBC_ENCODER_MAX_FRAME_SIZE  512

#define BLT_SBC_ENCODER_DEFAULT_BITRATE         328
#define BLT_SBC_ENCODER_DEFAULT_ALLOCATION_MODE SBC_LOUDNESS

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_MediaTypeId sbc_type_id;
} SbcEncoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_PcmMediaType media_type;
    BLT_Int16        pcm_buffer[BLT_SBC_ENCODER_PCM_BUFFER_SIZE/2];
    unsigned int     pcm_buffer_fullness;
} SbcEncoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_UInt8     sbc_frame[BLT_SBC_ENCODER_MAX_FRAME_SIZE];
    BLT_MediaType media_type;
    ATX_List*     packets;
} SbcEncoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(ATX_PropertyListener);

    /* members */
    SBC_ENC_PARAMS             encoder;
    SbcEncoderInput            input;
    SbcEncoderOutput           output;
    unsigned int               sbc_bitrate;
    unsigned int               sbc_allocation_mode;
    ATX_PropertyListenerHandle sbc_bitrate_property_listener_handle;
    ATX_PropertyListenerHandle sbc_allocation_mode_property_listener_handle;
} SbcEncoder;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(SbcEncoderModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(SbcEncoder, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(SbcEncoder, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(SbcEncoder, ATX_PropertyListener)

static BLT_Result SbcEncoder_Init(SbcEncoder* self);

/*----------------------------------------------------------------------
|   SbcEncoderInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoderInput_PutPacket(BLT_PacketConsumer* _self,
                          BLT_MediaPacket*    packet)
{
    SbcEncoder*             self          = ATX_SELF_M(input, SbcEncoder, BLT_PacketConsumer);
    const BLT_MediaType*    input_type    = NULL;
    const BLT_PcmMediaType* pcm_type      = NULL;
    BLT_MediaPacket*        output_packet = NULL;
    const BLT_UInt8*        data          = (const BLT_UInt8*)BLT_MediaPacket_GetPayloadBuffer(packet);
    BLT_Size                data_size     = BLT_MediaPacket_GetPayloadSize(packet);
    BLT_Result              result;

    /* check that we're getting PCM */
    BLT_MediaPacket_GetMediaType(packet, &input_type);
    if (input_type->id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    pcm_type = (const BLT_PcmMediaType*)input_type;
    if (pcm_type->channel_count > 2     ||
        pcm_type->bits_per_sample != 16 ||
        pcm_type->sample_format != BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE ||
        (pcm_type->sample_rate != 16000 &&
         pcm_type->sample_rate != 32000 &&
         pcm_type->sample_rate != 44100 &&
         pcm_type->sample_rate != 48000)) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* init/re-init the encoder if the parameters have changed */
    if (pcm_type->channel_count != self->input.media_type.channel_count ||
        pcm_type->sample_rate   != self->input.media_type.sample_rate) {
        self->input.media_type = *pcm_type;
        SbcEncoder_Init(self);
    }

    /* process the packet until we have consumed all its data */
    while (data_size) {
        unsigned int target_buffer_size = 2*pcm_type->channel_count*128;
        unsigned int buffer_space_available = target_buffer_size-self->input.pcm_buffer_fullness;
        unsigned int chunk = buffer_space_available < data_size ? buffer_space_available : data_size;
        ATX_CopyMemory(&self->input.pcm_buffer[self->input.pcm_buffer_fullness/2], data, chunk);
        data      += chunk;
        data_size -= chunk;
        self->input.pcm_buffer_fullness += chunk;

        /* pad with zeros on the last buffer */
        if ((BLT_MediaPacket_GetFlags(packet) & BLT_MEDIA_PACKET_FLAG_END_OF_STREAM) && data_size == 0) {
            unsigned int padding = target_buffer_size-self->input.pcm_buffer_fullness;
            if (padding) {
                ATX_SetMemory(&self->input.pcm_buffer[self->input.pcm_buffer_fullness/2], 0, padding);
            }
            self->input.pcm_buffer_fullness = target_buffer_size;
        }

        /* if we have filled the buffer, encode it */
        if (self->input.pcm_buffer_fullness == target_buffer_size) {
            self->encoder.ps16PcmBuffer     = (SINT16*)&self->input.pcm_buffer[0];
            self->encoder.pu8Packet         = (UINT8*)&self->output.sbc_frame[0];
            self->input.pcm_buffer_fullness = 0;
            SBC_Encoder(&self->encoder);

            /* create a packet for the output */
            result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                                self->encoder.u16PacketLength,
                                                (BLT_MediaType*)&self->output.media_type,
                                                &output_packet);
            if (BLT_FAILED(result)) return result;
            BLT_MediaPacket_SetPayloadSize(output_packet, self->encoder.u16PacketLength);
            ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(output_packet),
                           self->encoder.pu8Packet,
                           self->encoder.u16PacketLength);

            /* copy the timestamp */
            BLT_MediaPacket_SetTimeStamp(output_packet, BLT_MediaPacket_GetTimeStamp(packet));

            /* copy the flags */
            BLT_MediaPacket_SetFlags(output_packet, BLT_MediaPacket_GetFlags(packet));

            /* add to the output packet list */
            ATX_List_AddData(self->output.packets, output_packet);
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcEncoderInput)
    ATX_GET_INTERFACE_ACCEPT(SbcEncoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SbcEncoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcEncoderInput, BLT_PacketConsumer)
    SbcEncoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SbcEncoderInput,
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(SbcEncoderInput, BLT_MediaPort)
    SbcEncoderInput_GetName,
    SbcEncoderInput_GetProtocol,
    SbcEncoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   SbcEncoderOutput_Flush
+---------------------------------------------------------------------*/
static BLT_Result
SbcEncoderOutput_Flush(SbcEncoder* self)
{
    ATX_ListItem* item;
    while ((item = ATX_List_GetFirstItem(self->output.packets))) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) BLT_MediaPacket_Release(packet);
        ATX_List_RemoveItem(self->output.packets, item);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoderOutput_GetPacket(BLT_PacketProducer* _self,
                           BLT_MediaPacket**   packet)
{
    SbcEncoder*   self = ATX_SELF_M(output, SbcEncoder, BLT_PacketProducer);
    ATX_ListItem* packet_item;

    /* default return */
    *packet = NULL;

    /* check if we have a packet available */
    packet_item = ATX_List_GetFirstItem(self->output.packets);
    if (packet_item) {
        *packet = (BLT_MediaPacket*)ATX_ListItem_GetData(packet_item);
        ATX_List_RemoveItem(self->output.packets, packet_item);
        return BLT_SUCCESS;
    }

    return BLT_ERROR_PORT_HAS_NO_DATA;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcEncoderOutput)
    ATX_GET_INTERFACE_ACCEPT(SbcEncoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SbcEncoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SbcEncoderOutput,
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(SbcEncoderOutput, BLT_MediaPort)
    SbcEncoderOutput_GetName,
    SbcEncoderOutput_GetProtocol,
    SbcEncoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcEncoderOutput, BLT_PacketProducer)
    SbcEncoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   SbcEncoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
SbcEncoder_SetupPorts(SbcEncoder* self, BLT_MediaTypeId sbc_type_id)
{
    ATX_Result result;

    /* create a list of output packets */
    result = ATX_List_Create(&self->output.packets);
    if (ATX_FAILED(result)) return result;

    /* setup the output port */
    BLT_MediaType_Init(&self->output.media_type, sbc_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoder_Init
+---------------------------------------------------------------------*/
static BLT_Result
SbcEncoder_Init(SbcEncoder* self)
{
    ATX_SetMemory(&self->encoder, 0, sizeof(self->encoder));
    switch (self->input.media_type.sample_rate) {
        case 16000:
            self->encoder.s16SamplingFreq = SBC_sf16000;
            break;

        case 32000:
            self->encoder.s16SamplingFreq = SBC_sf32000;
            break;

        case 44100:
            self->encoder.s16SamplingFreq = SBC_sf44100;
            break;

        case 48000:
            self->encoder.s16SamplingFreq = SBC_sf48000;
            break;

        default:
            return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    if (self->input.media_type.channel_count == 2) {
        self->encoder.s16ChannelMode = SBC_JOINT_STEREO;
    } else {
        self->encoder.s16ChannelMode = SBC_MONO;
    }
    self->encoder.s16NumOfSubBands    = 8;
    self->encoder.s16NumOfBlocks      = 16;
    self->encoder.s16AllocationMethod = self->sbc_allocation_mode;
    self->encoder.u16BitRate          = self->sbc_bitrate;
    self->encoder.u8NumPacketToEncode = 1;

    SBC_Encoder_Init(&self->encoder);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
SbcEncoder_Create(BLT_Module*              module,
                  BLT_Core*                core,
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters,
                  BLT_MediaNode**          object)
{
    SbcEncoder*       self;
    BLT_Result        result;

    ATX_LOG_FINE("SbcEncoder::Create");

    /* check parameters */
    if (parameters == NULL ||
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(SbcEncoder));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup the input and output ports */
    result = SbcEncoder_SetupPorts(self, ((SbcEncoderModule*)module)->sbc_type_id);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(self);
        *object = NULL;
        return result;
    }

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, SbcEncoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(self, SbcEncoder, ATX_PropertyListener);
    ATX_SET_INTERFACE(&self->input,  SbcEncoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  SbcEncoderInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, SbcEncoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, SbcEncoderOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
SbcEncoder_Destroy(SbcEncoder* self)
{
    ATX_LOG_FINE("SbcEncoder::Destroy");

    /* release any packet we may hold */
    SbcEncoderOutput_Flush(self);
    ATX_List_Destroy(self->output.packets);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoder_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    SbcEncoder* self = ATX_SELF_EX(SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|   SbcEncoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoder_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    SbcEncoder* self = ATX_SELF_EX(SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    /* remove any packets in the output list */
    SbcEncoderOutput_Flush(self);

    /* reset the encoder */

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoder_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoder_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    SbcEncoder* self = ATX_SELF_EX(SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode);

    /* keep a reference to the stream */
    ATX_BASE(self, BLT_BaseMediaNode).context = stream;

    /* listen to settings on the new stream */
    if (stream) {
        ATX_Properties* properties;
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).context, &properties))) {
            ATX_PropertyValue property;
            ATX_Properties_AddListener(properties,
                                       BLT_SBC_ENCODER_BITRATE_PROPERTY,
                                       &ATX_BASE(self, ATX_PropertyListener),
                                       &self->sbc_bitrate_property_listener_handle);
            ATX_Properties_AddListener(properties,
                                       BLT_SBC_ENCODER_ALLOCATION_MODE_PROPERTY,
                                       &ATX_BASE(self, ATX_PropertyListener),
                                       &self->sbc_allocation_mode_property_listener_handle);

            /* read the initial values of the replay gain info */
            self->sbc_bitrate         = BLT_SBC_ENCODER_DEFAULT_BITRATE;
            self->sbc_allocation_mode = BLT_SBC_ENCODER_DEFAULT_ALLOCATION_MODE;

            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(properties, BLT_SBC_ENCODER_BITRATE_PROPERTY, &property)) &&
                property.type == ATX_PROPERTY_VALUE_TYPE_INTEGER) {
                self->sbc_bitrate = (unsigned int)property.data.integer;
            }
            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(properties, BLT_SBC_ENCODER_ALLOCATION_MODE_PROPERTY, &property)) &&
                property.type == ATX_PROPERTY_VALUE_TYPE_INTEGER) {
                self->sbc_allocation_mode = (unsigned int)property.data.integer;
            }
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoder_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoder_Deactivate(BLT_MediaNode* _self)
{
    SbcEncoder* self = ATX_SELF_EX(SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode);

    /* reset info */
    self->sbc_bitrate         = BLT_SBC_ENCODER_DEFAULT_BITRATE;
    self->sbc_allocation_mode = BLT_SBC_ENCODER_DEFAULT_ALLOCATION_MODE;

    /* remove our listener */
    if (ATX_BASE(self, BLT_BaseMediaNode).context) {
        ATX_Properties* properties;
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).context, &properties))) {
            ATX_Properties_RemoveListener(properties, self->sbc_bitrate_property_listener_handle);
            ATX_Properties_RemoveListener(properties, self->sbc_allocation_mode_property_listener_handle);
        }
    }

    /* we're detached from the stream */
    ATX_BASE(self, BLT_BaseMediaNode).context = NULL;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcEncoder)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcEncoder, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(SbcEncoder, ATX_PropertyListener)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SbcEncoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    SbcEncoder_GetPortByName,
    SbcEncoder_Activate,
    SbcEncoder_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    SbcEncoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SbcEncoder,
                                         BLT_BaseMediaNode,
                                         reference_count)

/*----------------------------------------------------------------------
|   SbcEncoder_OnPropertyChanged
+---------------------------------------------------------------------*/
BLT_VOID_METHOD
SbcEncoder_OnPropertyChanged(ATX_PropertyListener*    _self,
                             ATX_CString              name,
                             const ATX_PropertyValue* value)
{
    SbcEncoder* self = ATX_SELF(SbcEncoder, ATX_PropertyListener);

    if (name && (value == NULL || value->type == ATX_PROPERTY_VALUE_TYPE_INTEGER)) {
        if (ATX_StringsEqual(name, BLT_SBC_ENCODER_BITRATE_PROPERTY)) {
            self->sbc_bitrate = (unsigned int)(value ? value->data.integer : BLT_SBC_ENCODER_DEFAULT_BITRATE);
        } else if (ATX_StringsEqual(name, BLT_SBC_ENCODER_ALLOCATION_MODE_PROPERTY)) {
            self->sbc_allocation_mode = (unsigned int)(value ? value->data.integer : BLT_SBC_ENCODER_DEFAULT_ALLOCATION_MODE);
        }
        SbcEncoder_Init(self);
    }
}

/*----------------------------------------------------------------------
|    ATX_PropertyListener interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SbcEncoder, ATX_PropertyListener)
    SbcEncoder_OnPropertyChanged
};

/*----------------------------------------------------------------------
|   SbcEncoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    SbcEncoderModule* self = ATX_SELF_EX(SbcEncoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*     registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/SBC",
        &self->sbc_type_id);
    if (BLT_FAILED(result)) return result;

    ATX_LOG_FINE_1("SbcEncoderModule::Attach (audio/SBC = %d)", self->sbc_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SbcEncoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
SbcEncoderModule_Probe(BLT_Module*              _self,
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    SbcEncoderModule* self = ATX_SELF_EX(SbcEncoderModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor =
                (BLT_MediaNodeConstructor*)parameters;

            /* the input and output protocols should be PACKET */
            if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be PCM */
            if (constructor->spec.input.media_type->id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
                return BLT_FAILURE;
            }

            /* the output type should be audio/SBC */
            if (constructor->spec.output.media_type->id != self->sbc_type_id) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "SbcEncoder")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 10;
            }

            ATX_LOG_FINE_1("SbcEncoderModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SbcEncoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcEncoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(SbcEncoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(SbcEncoderModule, SbcEncoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SbcEncoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    SbcEncoderModule_Attach,
    SbcEncoderModule_CreateInstance,
    SbcEncoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define SbcEncoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SbcEncoderModule,
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(SbcEncoderModule,
                                         "SBC Audio Encoder",
                                         "com.axiosys.encoder.sbc",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
