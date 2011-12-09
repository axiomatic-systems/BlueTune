/*****************************************************************
|
|   ADTS Parser Module
|
|   (c) 2002-2007 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltAdtsParser.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.parsers.adts")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_ADTS_PARSER_MAX_FRAME_SIZE           8192
#define BLT_AAC_DECODER_OBJECT_TYPE_MPEG2_AAC_LC 0x67
#define BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO  0x40

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef enum {
    BLT_ADTS_PARSER_STATE_NEED_SYNC,
    BLT_ADTS_PARSER_STATE_NEED_BODY,
    BLT_ADTS_PARSER_STATE_NEED_READAHEAD
} AdtsParserState;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 adts_type_id;
    BLT_UInt32 mp4es_type_id;
} AdtsParserModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    BLT_MediaType    media_type;
    BLT_Boolean      eos;
    BLT_Boolean      packet_is_new;
    BLT_MediaPacket* packet;
} AdtsParserInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_Mp4AudioMediaType* media_type;
} AdtsParserOutput;

typedef struct {
    /* fixed part */
    unsigned char id;
    unsigned char layer;
    unsigned char protection_absent;
    unsigned char profile_object_type;
    unsigned char sampling_frequency_index;
    unsigned char private_bit;
    unsigned char channel_configuration;
    unsigned char original;
    unsigned char home;
    
    /* variable part */
    unsigned char copyright_identification_bit;
    unsigned char copyright_identification_start;
    unsigned int  aac_frame_length;
    unsigned int  adts_buffer_fullness;
    unsigned char number_of_raw_data_blocks;
} AdtsHeader;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    AdtsParserInput  input;
    AdtsParserOutput output;
    unsigned char    buffer[BLT_ADTS_PARSER_MAX_FRAME_SIZE+7];
    unsigned int     buffer_fullness;
    BLT_TimeStamp    time_stamp;
    BLT_Boolean      last_timestamp_was_zero;
    AdtsHeader       frame_header;
    AdtsParserState  state;
} AdtsParser;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(AdtsParserModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(AdtsParser, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(AdtsParser, ATX_Referenceable)

/*----------------------------------------------------------------------
|   AdtsParserInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParserInput_PutPacket(BLT_PacketConsumer* _self, BLT_MediaPacket* packet)
{
    AdtsParserInput*     self = ATX_SELF(AdtsParserInput, BLT_PacketConsumer);
    const BLT_MediaType* media_type = NULL;
    
    /* check media type */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (media_type == NULL || media_type->id != self->media_type.id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    /* release the previous packet */
    if (self->packet) BLT_MediaPacket_Release(self->packet);
    self->packet = NULL;
    
    /* keep a reference to this packet */
    if (packet) {
        if (BLT_MediaPacket_GetFlags(packet) & BLT_MEDIA_PACKET_FLAG_END_OF_STREAM) {
            self->eos = BLT_TRUE;
        }
        if (BLT_MediaPacket_GetPayloadSize(packet)) {
            self->packet = packet;
            BLT_MediaPacket_AddReference(packet);
            self->packet_is_new = BLT_TRUE;
        }
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AdtsParserInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParserInput_QueryMediaType(BLT_MediaPort*        _self,
                               BLT_Ordinal           index,
                               const BLT_MediaType** media_type)
{
    AdtsParserInput* self = ATX_SELF(AdtsParserInput, BLT_MediaPort);
    
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AdtsParserInput)
    ATX_GET_INTERFACE_ACCEPT(AdtsParserInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AdtsParserInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AdtsParserInput, BLT_PacketConsumer)
    AdtsParserInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AdtsParserInput, 
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(AdtsParserInput, BLT_MediaPort)
    AdtsParserInput_GetName,
    AdtsParserInput_GetProtocol,
    AdtsParserInput_GetDirection,
    AdtsParserInput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   Adts_SamplingFrequencyTable
+---------------------------------------------------------------------*/
static const unsigned int Adts_SamplingFrequencyTable[16] =
{
    96000,
    88200,
    64000,
    48000,
    44100,
    32000,
    24000,
    22050,
    16000,
    12000,
    11025,
    8000,
    7350,
    0,      /* Reserved */
    0,      /* Reserved */
    0       /* Escape code */
};

/*----------------------------------------------------------------------
|   AdtsParser_ParseHeader
+---------------------------------------------------------------------*/
static BLT_Result
AdtsHeader_Parse(AdtsHeader* h, const unsigned char* buffer)
{
    h->id                             =  (buffer[1]>>4)&0x01;
    h->layer                          =  (buffer[1]>>1)&0x03;
    h->protection_absent              =  (buffer[1]   )&0x01;
    h->profile_object_type            =  (buffer[2]>>6)&0x03;
    h->sampling_frequency_index       =  (buffer[2]>>2)&0x0F;
    h->private_bit                    =  (buffer[2]>>1)&0x01;
    h->channel_configuration          = ((buffer[2]<<1)&0x04) | 
                                        ((buffer[3]>>6)&0x03);
    h->original                       =  (buffer[3]>>5)&0x01;
    h->home                           =  (buffer[3]>>4)&0x01;
    h->copyright_identification_bit   =  (buffer[3]>>3)&0x01;
    h->copyright_identification_start =  (buffer[3]>>2)&0x01;
    h->aac_frame_length               = (((unsigned int)buffer[3]<<11)&0x1FFF) |
                                        (((unsigned int)buffer[4]<< 3)       ) |
                                        (((unsigned int)buffer[5]>> 5)&0x07  );
    h->adts_buffer_fullness           = (((unsigned int)buffer[5]<< 6)&0x3FF ) |
                                        (((unsigned int)buffer[6]>> 2)&0x03  );
    h->number_of_raw_data_blocks      = (buffer[6]    )&0x03;

    /* check the validity of the header */
    if (h->layer != 0) return BLT_FAILURE;
    if (h->id == 1 && h->profile_object_type == 3) return BLT_FAILURE;
    if (h->sampling_frequency_index > 12) return BLT_FAILURE;
    if (h->aac_frame_length < 7) return BLT_FAILURE;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AdtsHeader_Match
+---------------------------------------------------------------------*/
static BLT_Boolean
AdtsHeader_Match(const AdtsHeader* h1, const AdtsHeader* h2)
{
    return h1->id                       == h2->id && 
           h1->layer                    == h2->layer &&
           h1->protection_absent        == h2->protection_absent &&
           h1->profile_object_type      == h2->profile_object_type &&
           h1->sampling_frequency_index == h2->sampling_frequency_index &&
           h1->private_bit              == h2->private_bit &&
           h1->channel_configuration    == h2->channel_configuration &&
           h1->original                 == h2->original &&
           h1->home                     == h2->home;
}

/*----------------------------------------------------------------------
|   AdtsParser_FillBuffer
+---------------------------------------------------------------------*/
static BLT_Result
AdtsParser_FillBuffer(AdtsParser*  self, 
                      unsigned int buffer_fullness)
{
    unsigned int needed;
    unsigned int available;
    unsigned int chunk;
    
    if (self->buffer_fullness >= buffer_fullness) return BLT_SUCCESS;
    if (self->input.packet == NULL) {
        if (self->input.eos) {
            return BLT_ERROR_EOS;
        } else {
            return BLT_ERROR_PORT_HAS_NO_DATA;
        }
    }
    
    needed = buffer_fullness-self->buffer_fullness;
    available = BLT_MediaPacket_GetPayloadSize(self->input.packet);
    if (available >= needed) {
        chunk = needed;
    } else {
        chunk = available;
    }
    ATX_CopyMemory(&self->buffer[self->buffer_fullness], 
                   BLT_MediaPacket_GetPayloadBuffer(self->input.packet),
                   chunk);
    BLT_MediaPacket_SetPayloadOffset(self->input.packet, 
                                     BLT_MediaPacket_GetPayloadOffset(self->input.packet)+chunk);
    self->buffer_fullness += chunk;
    if (available == chunk) {
        /* we read everything */
        BLT_MediaPacket_Release(self->input.packet);
        self->input.packet = NULL;
    }
    if (chunk == needed) {
        return BLT_SUCCESS;
    } else {
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
}

/*----------------------------------------------------------------------
|   AdtsParser_FindHeader
+---------------------------------------------------------------------*/
static BLT_Result
AdtsParser_FindHeader(AdtsParser* self, AdtsHeader* header)
{
    BLT_Result   result;
    unsigned int i;
    
    for (;;) {
        /* refill the buffer to have at least 7 bytes */
        result = AdtsParser_FillBuffer(self, 7);
        if (BLT_FAILED(result)) return result;
        
        /* look for a sync pattern */
        for (i=0; i<self->buffer_fullness-1; i++) {
            if (self->buffer[i] == 0xFF && (self->buffer[i+1]&0xF0) == 0xF0) {
                /* sync pattern found, left-align the data and refill if needed */
                if (i != 0) {
                    /* refill the header to a full 7 bytes */
                    unsigned int j;
                    for (j=i; j<self->buffer_fullness; j++) {
                        self->buffer[j-i] = self->buffer[j];
                    }
                    self->buffer_fullness -= i;
                    result = AdtsParser_FillBuffer(self, 7);
                    if (BLT_FAILED(result)) return result;
                }
                
                result = AdtsHeader_Parse(header, self->buffer);
                if (BLT_FAILED(result)) {
                    /* it looked like a header, but wasn't one */
                    /* skip two bytes and try again            */
                    unsigned int j;
                    for (j=2; j<self->buffer_fullness; j++) {
                        self->buffer[j-2] = self->buffer[j];
                    }
                    self->buffer_fullness -= 2;
                    i = 0;
                    continue;
                }

                /* found a valid header */
                return BLT_SUCCESS;
            }
        }
    
        /* sync pattern not found, keep the last byte for the next time around */
        self->buffer[0] = self->buffer[self->buffer_fullness-1];
        self->buffer_fullness = 1;
    }
    
    return BLT_ERROR_PORT_HAS_NO_DATA;
}

/*----------------------------------------------------------------------
|   AdtsParser_ReadNextHeader
+---------------------------------------------------------------------*/
static BLT_Result
AdtsParser_ReadNextHeader(AdtsParser* self, AdtsHeader* header)
{
    BLT_Result           result;
    const unsigned char* buffer;
    
    /* fill the buffer to have 7 bytes past the frame body */
    result = AdtsParser_FillBuffer(self, self->frame_header.aac_frame_length+7);
    if (BLT_FAILED(result)) return result;
    
    /* look for a sync pattern */
    buffer = &self->buffer[self->frame_header.aac_frame_length];
    if (buffer[0] != 0xFF || (buffer[1]&0xF0) != 0xF0) {
        /* not a sync pattern */
        return BLT_FAILURE;
    }
    
    /* parse the buffer into a header struct */
    result = AdtsHeader_Parse(header, buffer);
    if (BLT_FAILED(result)) return result;
        
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AdtsWriteBits
+---------------------------------------------------------------------*/
void 
AdtsWriteBits(unsigned char* data, unsigned int* total_bits, unsigned int bits, unsigned int bit_count)
{
    data += *total_bits/8;
    unsigned int space = 8-(*total_bits%8);
    while (bit_count) {
        unsigned int mask = bit_count==32 ? 0xFFFFFFFF : ((1<<bit_count)-1);
        if (bit_count <= space) {
            *data |= ((bits&mask) << (space-bit_count));
            *total_bits += bit_count;
            return;
        } else {
            *data |= ((bits&mask) >> (bit_count-space));
            ++data;
            *total_bits += space;
            bit_count  -= space;
            space       = 8;
        }
    }
}

/*----------------------------------------------------------------------
|   AdtsParser_UpdateMediaType
+---------------------------------------------------------------------*/
static void
AdtsParser_UpdateMediaType(AdtsParser* self)
{
    /* default DSI */
    unsigned char dsi[7] = {0,0,0,0,0,0,0};
    unsigned int  dsi_bits = 0;
    
    self->output.media_type->channel_count = self->frame_header.channel_configuration;
    self->output.media_type->sample_rate   = Adts_SamplingFrequencyTable[self->frame_header.sampling_frequency_index&0x0F];
    self->output.media_type->base.format_or_object_type_id = 
        self->frame_header.id == 0 ?
        BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO :
        BLT_AAC_DECODER_OBJECT_TYPE_MPEG2_AAC_LC;
        
    AdtsWriteBits(dsi, &dsi_bits, self->frame_header.profile_object_type+1, 5);
    AdtsWriteBits(dsi, &dsi_bits, self->frame_header.sampling_frequency_index, 4);
    AdtsWriteBits(dsi, &dsi_bits, self->frame_header.channel_configuration, 4);
    
    /* if the sampling rate is less than 24kHz, assume this is He-AAC */
    if (self->frame_header.sampling_frequency_index >= 6) {
        self->output.media_type->base.format_or_object_type_id = BLT_AAC_DECODER_OBJECT_TYPE_MPEG4_AUDIO;
        AdtsWriteBits(dsi, &dsi_bits, 0, 3); /* extension bits */
        AdtsWriteBits(dsi, &dsi_bits, 0x2b7, 11); /* extension tag */
        AdtsWriteBits(dsi, &dsi_bits, 5, 5); 
        AdtsWriteBits(dsi, &dsi_bits, 1, 1); /* sbr present */
        AdtsWriteBits(dsi, &dsi_bits, self->frame_header.sampling_frequency_index-3, 4); 
        AdtsWriteBits(dsi, &dsi_bits, 0x548, 11); /* extension tag */
        AdtsWriteBits(dsi, &dsi_bits, 1, 1); /* ps present */
    }
    
    self->output.media_type->decoder_info_length = (dsi_bits+7)/8;
    ATX_CopyMemory(&self->output.media_type->decoder_info[0], dsi, self->output.media_type->decoder_info_length);
}

/*----------------------------------------------------------------------
|   AdtsParser_UpdateTimeStamp
+---------------------------------------------------------------------*/
static void
AdtsParser_UpdateTimeStamp(AdtsParser* self)
{
    if (self->input.packet_is_new) {
        if (self->input.packet) {
            BLT_TimeStamp new_timestamp = BLT_MediaPacket_GetTimeStamp(self->input.packet);
            if (new_timestamp.seconds || new_timestamp.nanoseconds) {
                self->time_stamp = new_timestamp;
                self->last_timestamp_was_zero = BLT_FALSE;
            } else {
                if (self->last_timestamp_was_zero) {
                    BLT_TimeStamp frame_duration = BLT_TimeStamp_FromSamples(1024, Adts_SamplingFrequencyTable[self->frame_header.sampling_frequency_index&0x0F]);
                    self->time_stamp = BLT_TimeStamp_Add(self->time_stamp, frame_duration);
                } else {
                    self->time_stamp.seconds     = 0;
                    self->time_stamp.nanoseconds = 0;
                    self->last_timestamp_was_zero = BLT_TRUE;
                }
            }
        }
        self->input.packet_is_new = BLT_FALSE;
    } else {
        BLT_TimeStamp frame_duration = BLT_TimeStamp_FromSamples(1024, Adts_SamplingFrequencyTable[self->frame_header.sampling_frequency_index&0x0F]);
        self->time_stamp = BLT_TimeStamp_Add(self->time_stamp, frame_duration);
    }
}

/*----------------------------------------------------------------------
|   AdtsParserOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParserOutput_QueryMediaType(BLT_MediaPort*        _self,
                                BLT_Ordinal           index,
                                const BLT_MediaType** media_type)
{
    AdtsParserOutput* self = ATX_SELF(AdtsParserOutput, BLT_MediaPort);
    
    if (index == 0) {
        *media_type = (BLT_MediaType*)self->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   AdtsParserOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParserOutput_GetPacket(BLT_PacketProducer* _self,
                           BLT_MediaPacket**   packet)
{
    AdtsParser*  self   = ATX_SELF_M(output, AdtsParser, BLT_PacketProducer);
    AdtsHeader   next_header;
    BLT_Result   result = BLT_SUCCESS;
    
    /* default value */
    *packet = NULL;
    ATX_SetMemory(&next_header, 0, sizeof(next_header));
    
    /* loop until we get a packet or run out of data */
    for (;;) {
        BLT_Boolean have_next_header = BLT_FALSE;

        /* if we're not processing a frame, get the next header */
        if (self->state == BLT_ADTS_PARSER_STATE_NEED_SYNC) {
            result = AdtsParser_FindHeader(self, &self->frame_header);
            if (BLT_FAILED(result)) return result;
            
            AdtsParser_UpdateMediaType(self);
            AdtsParser_UpdateTimeStamp(self);
            self->state = BLT_ADTS_PARSER_STATE_NEED_BODY;
        }
                            
        /* get the frame body */
        if (self->state == BLT_ADTS_PARSER_STATE_NEED_BODY) {
            result = AdtsParser_FillBuffer(self, self->frame_header.aac_frame_length);
            if (BLT_FAILED(result)) return result;
            self->state = BLT_ADTS_PARSER_STATE_NEED_READAHEAD;
        }

        /* the frame is complete, look for the next header */
        if (self->state == BLT_ADTS_PARSER_STATE_NEED_READAHEAD) {
            result = AdtsParser_ReadNextHeader(self, &next_header);
            if (BLT_FAILED(result)) {
                if (result == BLT_ERROR_PORT_HAS_NO_DATA) return result;
                
                /* at the end of the stream, it is ok not to have */
                /* a next header.                                 */
                if (result != BLT_ERROR_EOS) {
                    /* invalidate the current frame, we'll try again */
                    self->buffer[0] = 0;
                    self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
                    continue;
                }
            } else {
                have_next_header = BLT_TRUE;
    
                /* compare with the current header */
                if (!AdtsHeader_Match(&next_header, &self->frame_header)) {
                    /* not the same header, look for a new frame */
                    self->buffer[0] = 0;
                    self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
                    continue;
                }
            }
        }
        
        /* this frame looks good, create a media packet for it */
        {
            unsigned int payload_offset = self->frame_header.protection_absent?7:9;
            unsigned int payload_size   = self->frame_header.aac_frame_length-payload_offset;
            
            if (self->frame_header.aac_frame_length < payload_offset) {
                /* something is terribly wrong here */
                self->buffer_fullness = 0;
                self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
                continue;                        
            }
            
            result = BLT_Core_CreateMediaPacket(
                ATX_BASE(self, BLT_BaseMediaNode).core,
                payload_size,
                (BLT_MediaType*)self->output.media_type,
                packet);
            if (BLT_FAILED(result)) {
                self->buffer_fullness = 0;
                self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
                return result;
            }
            ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(*packet),
                           &self->buffer[payload_offset],
                           payload_size);
            BLT_MediaPacket_SetPayloadSize(*packet, payload_size);
            BLT_MediaPacket_SetTimeStamp(*packet, self->time_stamp);
            ATX_LOG_FINE_3("ADTS packet: size=%d, ts=%d.%09d", payload_size, self->time_stamp.seconds, self->time_stamp.nanoseconds);
        }

        /* update the header for next time */
        if (have_next_header) {
            unsigned int i;
            for (i=0; i<7; i++) {
                self->buffer[i] = self->buffer[i+self->frame_header.aac_frame_length];
            }
            AdtsParser_UpdateTimeStamp(self);
            self->frame_header = next_header;
            self->buffer_fullness = 7;
            self->state = BLT_ADTS_PARSER_STATE_NEED_BODY;
        } else {
            self->buffer_fullness = 0;
            self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
        }
        return BLT_SUCCESS;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AdtsParserOutput)
    ATX_GET_INTERFACE_ACCEPT(AdtsParserOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AdtsParserOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AdtsParserOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(AdtsParserOutput, BLT_MediaPort)
    AdtsParserOutput_GetName,
    AdtsParserOutput_GetProtocol,
    AdtsParserOutput_GetDirection,
    AdtsParserOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AdtsParserOutput, BLT_PacketProducer)
    AdtsParserOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   AdtsParser_Create
+---------------------------------------------------------------------*/
static BLT_Result
AdtsParser_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  BLT_MediaNode**          object)
{
    AdtsParser* self;

    ATX_LOG_FINE("AdtsParser::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(AdtsParser));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */
    BLT_MediaType_Init(&self->input.media_type,
                       ((AdtsParserModule*)module)->adts_type_id);
    self->output.media_type = (BLT_Mp4AudioMediaType*)ATX_AllocateZeroMemory(sizeof(BLT_Mp4AudioMediaType)+6); /* up to 7 bytes decoder_config */
    BLT_MediaType_InitEx(&self->output.media_type->base.base, ((AdtsParserModule*)module)->mp4es_type_id, sizeof(BLT_Mp4AudioMediaType)+6);
    self->output.media_type->base.stream_type              = BLT_MP4_STREAM_TYPE_AUDIO;
    self->output.media_type->base.format_or_object_type_id = 0; 
    self->output.media_type->decoder_info_length           = 2;

    self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
    self->buffer_fullness = 0;
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, AdtsParser, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, AdtsParser, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  AdtsParserInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  AdtsParserInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, AdtsParserOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, AdtsParserOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AdtsParser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AdtsParser_Destroy(AdtsParser* self)
{
    ATX_LOG_FINE("AdtsParser::Destroy");

    /* release any packet we have */
    if (self->input.packet) {
        BLT_MediaPacket_Release(self->input.packet);
    }

    /* free the media type extensions */
    BLT_MediaType_Free((BLT_MediaType*)self->output.media_type);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AdtsParser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParser_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    AdtsParser* self = ATX_SELF_EX(AdtsParser, BLT_BaseMediaNode, BLT_MediaNode);

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
|   AdtsParser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParser_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    AdtsParser* self = ATX_SELF_EX(AdtsParser, BLT_BaseMediaNode, BLT_MediaNode);
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);
    
    /* we need to reset the state machine */
    self->state = BLT_ADTS_PARSER_STATE_NEED_SYNC;
    self->buffer_fullness = 0;
    self->input.eos = BLT_FALSE;
	
	return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AdtsParser)
    ATX_GET_INTERFACE_ACCEPT_EX(AdtsParser, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(AdtsParser, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AdtsParser, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    AdtsParser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    AdtsParser_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AdtsParser, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   AdtsParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParserModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    AdtsParserModule* self = ATX_SELF_EX(AdtsParserModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*     registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".aac" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".aac",
                                            "audio/aac");
    if (BLT_FAILED(result)) return result;

    /* get the type id for "audio/aac" */
    result = BLT_Registry_GetIdForName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/aac",
        &self->adts_type_id);
    if (BLT_FAILED(result)) return result;
 
    /* register an alias for the same mime type */
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/aacp", self->adts_type_id);

    /* register the type id for BLT_MP4_ES_MIME_TYPE */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        BLT_MP4_AUDIO_ES_MIME_TYPE,
        &self->mp4es_type_id);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("ADTS Parser Module::Attach (" BLT_MP4_AUDIO_ES_MIME_TYPE " type = %d)", self->mp4es_type_id);
    ATX_LOG_FINE_1("ADTS Parser Module::Attach (audio/aac type = %d)", self->adts_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AdtsParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AdtsParserModule_Probe(BLT_Module*              _self, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    AdtsParserModule* self = ATX_SELF_EX(AdtsParserModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* we need the input protocol to be PACKET and the output */
            /* protocol to be PACKET                                  */
             if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* we need the input media type to be 'audio/aac' */
            if (constructor->spec.input.media_type->id != self->adts_type_id) {
                return BLT_FAILURE;
            }

            /* the output type should be unknown or an AAC elementary stream at this point */
            if (constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN &&
                constructor->spec.output.media_type->id != self->mp4es_type_id) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "AdtsParser")) {
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

            ATX_LOG_FINE_1("AdtsParserModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AdtsParserModule)
    ATX_GET_INTERFACE_ACCEPT_EX(AdtsParserModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(AdtsParserModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AdtsParserModule, AdtsParser)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AdtsParserModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    AdtsParserModule_Attach,
    AdtsParserModule_CreateInstance,
    AdtsParserModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AdtsParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AdtsParserModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(AdtsParserModule,
                                         "ADTS Parser",
                                         "com.axiosys.parser.adts",
                                         "1.2.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
