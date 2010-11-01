/****************************************************************
|
|   WMA Decoder Module
|
|   (c) 2006-2010 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltWmaDecoder.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltByteStreamUser.h"
#include "BltStream.h"

#include "wmaudio.h"
#include "pcmfmt.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.wma")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    BLT_UInt32 wma_type_id;
    BLT_UInt32 mms_type_id;
} WmaDecoderModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_InputStreamUser);
    ATX_IMPLEMENTS(BLT_PacketConsumer);

    /* members */
    ATX_InputStream* stream;
    ATX_Boolean      eos;
    ATX_Boolean      packet_mode;
    ATX_List*        packets;
    ATX_Position     head_packet_position;
    ATX_Size         wma_packet_size;
    ATX_Ordinal      wma_selected_stream;
    ATX_Boolean      wms_stream_change;
    ATX_Cardinal     packets_parsed;
    ATX_Position     position;
    BLT_LargeSize    size;
    BLT_MediaTypeId  wma_type_id;
    BLT_MediaTypeId  mms_type_id;
} WmaDecoderInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_PcmMediaType media_type;
    ATX_Boolean      samples_pending;
} WmaDecoderOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    WmaDecoderInput  input;
    WmaDecoderOutput output;
    void*            wma_handle;
    ATX_DataBuffer*  wma_buffer;
} WmaDecoder;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_WMA_DECODER_PACKET_SAMPLE_COUNT     1024
#define BLT_WMA_DECODER_DEFAULT_PCM_BUFFER_SIZE 2048
#define BLT_WMA_DECODER_MAX_DECODE_LOOP         100

#define BLT_WMA_DECODER_MMS_PACKET_ID_HEADER        0x48 /* 'H' */
#define BLT_WMA_DECODER_MMS_PACKET_ID_DATA          0x44 /* 'D' */
#define BLT_WMA_DECODER_MMS_PACKET_ID_STREAM_CHANGE 0x43 /* 'C' */

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(WmaDecoderModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(WmaDecoder, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(WmaDecoder, ATX_Referenceable)

/*----------------------------------------------------------------------
|   WMADebugMessage
+---------------------------------------------------------------------*/
void 
WMADebugMessage(const char* format, ...)
{
    ATX_COMPILER_UNUSED(format);
}

/*----------------------------------------------------------------------
|   WMAFileCBGetData
+---------------------------------------------------------------------*/
tWMA_U32 
WMAFileCBGetData(void*           state,
                 tWMA_U64        position,
                 tWMA_U32        bytes_to_read,
                 unsigned char** data)
{
    tWMAFileHdrState* header = (tWMAFileHdrState*)state;
    WmaDecoder*       self;
    ATX_Result        result;

    ATX_ASSERT(handle != NULL);

    /* default values */
    *data = NULL;

    /* retrieve our self-pointer in the handle */
    self = (WmaDecoder*)header->callbackContext;

    /* check which mode we're in */
    if (self->input.packet_mode) {
        /* packet mode, look for the first packet that has the data we want */
        unsigned char* payload = NULL;
        ATX_Size       payload_size = 0;
        ATX_Size       payload_offset = 0;
        ATX_ListItem*  item;
        if (self->input.packets == NULL) return 0;
        item = ATX_List_GetFirstItem(self->input.packets);

        /* check if we're before the start of the first packet */
        if (position < self->input.head_packet_position) {
            ATX_LOG_WARNING("attempt to rewind in packet mode");
            return 0;
        }
        
        /* iterate over input packets */
        while (item) {
            ATX_ListItem* next = ATX_ListItem_GetNext(item);
            BLT_MediaPacket* packet = (BLT_MediaPacket*)ATX_ListItem_GetData(item);
            payload      = BLT_MediaPacket_GetPayloadBuffer(packet);
            payload_size = BLT_MediaPacket_GetPayloadSize(packet);
            if (position >= self->input.head_packet_position+payload_size) {
                /* we're done with this packet */
                BLT_MediaPacket_Release(packet);
                ATX_List_RemoveItem(self->input.packets, item);
                if (self->input.packets_parsed == 0 || self->input.wma_packet_size == 0) {
                    self->input.head_packet_position += payload_size;
                } else {
                    self->input.head_packet_position += self->input.wma_packet_size;
                }
                self->input.packets_parsed++;
                payload = NULL;
            } else {
                break;
            }
            item = next;
        }
        if (payload == NULL) return 0;
        payload_offset = (ATX_Size)(position-self->input.head_packet_position);
        if (payload_offset+bytes_to_read > payload_size) {
            bytes_to_read = payload_size-payload_offset;
        }
        *data = payload+payload_offset;
        self->input.position += bytes_to_read;
        return bytes_to_read;
    } else {
        /* we are in stream mode */
        ATX_Size       total_read = 0;
        unsigned char* buffer = NULL;
        
        /* seek if needed */
        if (position != self->input.position) {
            result = ATX_InputStream_Seek(self->input.stream, (ATX_Position)position);
            if (ATX_FAILED(result)) return 0;
            self->input.position = (ATX_Position)position;
        }

        /* reserve some space in our internal buffer */
        ATX_DataBuffer_Reserve(self->wma_buffer, bytes_to_read);
        *data = ATX_DataBuffer_UseData(self->wma_buffer);
        buffer = *data;
        
        /* read from the input stream until we've read everything or reached the end */
        while (bytes_to_read) {
            ATX_Size bytes_read;
            result = ATX_InputStream_Read(self->input.stream, buffer, bytes_to_read, &bytes_read);
            if (ATX_FAILED(result) || bytes_read == 0) {
                self->input.eos = ATX_TRUE;
                break;
            }
            bytes_to_read        -= bytes_read;
            total_read           += bytes_read;
            buffer               += bytes_read;
            self->input.position += bytes_read;
        }

        /* return the number of bytes read */
        return total_read;
    }
}

/*----------------------------------------------------------------------
|   WmaDecoder_OpenStream
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_OpenStream(WmaDecoder* self)
{
    tWMAFileStatus status;
    tWMAFileHeader header;
    PCMFormat      pcm_format;

    /* check that we have a stream unless in packet mode */
    if (self->input.stream == NULL && !self->input.packet_mode) {
        return BLT_FAILURE;
    }

    /* reset the position */
    self->input.packets_parsed = 0;
    self->input.position       = 0;
    self->input.eos            = ATX_FALSE;
    
    /* release any existing decoder */
    if (self->wma_handle) {
        WMAFileDecodeClose(&self->wma_handle);
    }

    /* create a new decoder */
    status = WMAFileDecodeCreate(&self->wma_handle);
    if (status != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoder_OpenStream - WMAFileDecodeCreate failed (%d)", status);
        return BLT_FAILURE;
    }

    /* setup the callback info */
    ((tWMAFileHdrState*)self->wma_handle)->callbackContext = self;

    /* initialize the decoder */
    status = WMAFileDecodeInitEx(self->wma_handle, 
                                 0, /* nDecoderFlags  */
                                 0, /* nDRCSetting    */
                                 0, /* bDropPacket    */
                                 0, /* nDstChannelMask */
                                 0, /* nInterpResampRate */
                                 &pcm_format, /* pPCMFormat */
                                 self->input.wma_selected_stream);
    if (status != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoder_OpenStream - WMAFileDecodeInitEx failed (%d)", status);
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    
    /* get the file info */
    ATX_SetMemory(&header, 0, sizeof(header));
    status = WMAFileDecodeInfo(self->wma_handle, &header);
    if (status != cWMA_NoErr) {
        ATX_LOG_WARNING_1("WmaDecoder_OpenStream - WMAFileDecodeInfo failed (%d)", status);
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* check that we support this */
    if (header.has_DRM) {
        ATX_LOG_WARNING("WmaDecoder_OpenStream - stream is DRM protected");
        return BLT_ERROR_UNSUPPORTED_FORMAT;
    }

    /* remember the packet size */
    self->input.wma_packet_size = header.packet_size;
    
    /* update the output media type */
    self->output.media_type.sample_rate     = header.sample_rate;
    self->output.media_type.channel_count   = header.num_channels;
    self->output.media_type.channel_mask    = header.channel_mask;
    self->output.media_type.bits_per_sample = (BLT_UInt8)header.bits_per_sample;
    self->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* update the stream info */
    if (ATX_BASE(self, BLT_BaseMediaNode).context) {
        BLT_StreamInfo stream_info;
        const tWMAFileContDesc* content_desc = NULL;
        
        /* start with no info */
        stream_info.mask = 0;

        /* sample rate */
        stream_info.sample_rate = header.sample_rate;
        stream_info.mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;

        /* channel count */
        stream_info.channel_count = header.num_channels;
        stream_info.mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;

        /* data type */
        if (WMAFileIsLosslessWMA(self->wma_handle)) {
            if (header.version > cWMA_V2) {
                stream_info.data_type = "WMA Pro";
            } else {
                stream_info.data_type = "WMA";
            }
        } else {
            stream_info.data_type = "WMA Lossless";
        }
        stream_info.mask |= BLT_STREAM_INFO_MASK_DATA_TYPE;

        /* nominal bitrate */
        stream_info.nominal_bitrate = header.bitrate;
        stream_info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;

        /* duration */
        stream_info.duration = header.duration;
        stream_info.mask |= BLT_STREAM_INFO_MASK_DURATION;

        BLT_Stream_SetInfo(ATX_BASE(self, BLT_BaseMediaNode).context, &stream_info);
        
        /* metadata */
        status = WMAFileContentDesc(self->wma_handle, &content_desc);
        if (status == cWMA_NoErr && content_desc) {
            ATX_Properties* properties = NULL;
            BLT_Result result = BLT_Stream_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).context, &properties);
            if (BLT_SUCCEEDED(result)) {
#if 0
                ATX_PropertyValue property;
                
                if (content_desc->
                property.data.string = buffer;
                property_value.type = ATX_PROPERTY_VALUE_TYPE_STRING;
                ATX_Properties_SetProperty(properties,
                                           "Tags/Title",
                                           &property);
#endif
            }
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   WmaDecoderInput_SetStream
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoderInput_SetStream(BLT_InputStreamUser* _self, 
                          ATX_InputStream*     stream,
                          const BLT_MediaType* media_type)
{
    WmaDecoder* self = ATX_SELF_M(input, WmaDecoder, BLT_InputStreamUser);
    BLT_Result  result;

    /* check the stream's media type */
    if (media_type == NULL || 
        media_type->id != self->input.wma_type_id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(self->input.stream);

    /* reset counters and flags */
    self->input.size     = 0;
    self->input.position = 0;
    self->input.eos      = ATX_FALSE;

    /* get stream size */
    ATX_InputStream_GetSize(stream, &self->input.size);

    /* open the stream */
    self->input.stream = stream;
    result = WmaDecoder_OpenStream(self);
    if (BLT_FAILED(result)) {
        self->input.stream = NULL;
        ATX_LOG_WARNING("OpenStream - failed");
        return result;
    }

    /* keep a reference to the stream */
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WmaDecoderInput_Flush
+---------------------------------------------------------------------*/
static void
WmaDecoderInput_Flush(WmaDecoderInput* self) 
{
    if (self->packets) {
        ATX_ListItem* packet_item = ATX_List_GetFirstItem(self->packets);
        while (packet_item) {
            BLT_MediaPacket_Release((BLT_MediaPacket*)ATX_ListItem_GetData(packet_item));
            packet_item = ATX_ListItem_GetNext(packet_item);
        }
        ATX_List_Clear(self->packets);
    }
}

/*----------------------------------------------------------------------
|   WmaDecoderInput_AddPacket
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoderInput_AddPacket(WmaDecoderInput* self, BLT_MediaPacket* packet)
{
    /* keep the packet */
    if (self->packets == NULL) {
        ATX_List_Create(&self->packets);
    }
    if (ATX_List_GetItemCount(self->packets) == 0) {
        self->head_packet_position = self->position;
    }
    ATX_List_AddData(self->packets, packet);
    BLT_MediaPacket_AddReference(packet);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   WmaDecoderInput_PutPacket
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoderInput_PutPacket(BLT_PacketConsumer* _self, BLT_MediaPacket* packet)
{
    WmaDecoder*          self = ATX_SELF_M(input, WmaDecoder, BLT_PacketConsumer);
    const BLT_MediaType* media_type = NULL;
    const unsigned char* payload;
    BLT_Size             payload_size;
    unsigned int         mms_packet_length;
    unsigned int         mms_packet_id;
    BLT_Result           result;

    /* check the packet's media type */
    if (packet == NULL) return BLT_ERROR_INVALID_PARAMETERS;
    result = BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (BLT_FAILED(result)) return result;
    if (media_type->id != self->input.mms_type_id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    /* inspect the packet header */
    payload = (const unsigned char*)BLT_MediaPacket_GetPayloadBuffer(packet);
    payload_size = BLT_MediaPacket_GetPayloadSize(packet);
    if ((payload[0]&0x7F) != 0x24) {
        ATX_LOG_WARNING("invalid framing byte");
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    mms_packet_id = payload[1];
    mms_packet_length = ATX_BytesToInt16Le(&payload[2]);
    if (mms_packet_length+2 > payload_size) {
        ATX_LOG_WARNING("invalid payload size");
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    if (mms_packet_id == BLT_WMA_DECODER_MMS_PACKET_ID_HEADER) {
        /* flush any pending packets */
        self->input.position = 0;
        WmaDecoderInput_Flush(&self->input);
        
        /* keep this packet */
        WmaDecoderInput_AddPacket(&self->input, packet);
        
        /* get the selected stream (stored in the the 'incarnation' field of the header) */
        self->input.wma_selected_stream = payload[8];
        if (self->input.wma_selected_stream == 0 || self->input.wma_selected_stream > 127) {
            self->input.wma_selected_stream = 1;
        }
        
        /* skip the packet header */
        BLT_MediaPacket_SetPayloadOffset(packet, BLT_MediaPacket_GetPayloadOffset(packet)+12);
        ATX_LOG_FINE_1("new header packet - size=%d", mms_packet_length);
        self->input.wms_stream_change = ATX_FALSE;
        result = WmaDecoder_OpenStream(self);
        if (BLT_FAILED(result)) {
            ATX_LOG_WARNING("OpenStream - failed");
            WmaDecoderInput_Flush(&self->input);
            return result;
        }
    } else if (mms_packet_id == BLT_WMA_DECODER_MMS_PACKET_ID_DATA) {
        /* skip the packet header */
        BLT_MediaPacket_SetPayloadOffset(packet, BLT_MediaPacket_GetPayloadOffset(packet)+12);
        ATX_LOG_FINE_1("new data packet - size=%d", mms_packet_length);

        /* keep this packet */
        WmaDecoderInput_AddPacket(&self->input, packet);
    } else if (mms_packet_id == BLT_WMA_DECODER_MMS_PACKET_ID_STREAM_CHANGE) {
        /* stream change */
        ATX_LOG_FINE("stream change");
        self->input.wms_stream_change = ATX_TRUE;
        BLT_MediaPacket_Release(packet);
    } else {
        /* discard the packet */
        ATX_LOG_FINE_2("discarding unknown packet type=%x - size=%d", mms_packet_id, mms_packet_length);
        BLT_MediaPacket_Release(packet);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoderInput)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderInput, BLT_InputStreamUser)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(WmaDecoderInput, BLT_InputStreamUser)
    WmaDecoderInput_SetStream
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(WmaDecoderInput, BLT_PacketConsumer)
    WmaDecoderInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    WmaDecoderInput_GetName
+---------------------------------------------------------------------*/
BLT_METHOD 
WmaDecoderInput_GetName(BLT_MediaPort* self, BLT_CString* name)
{
    BLT_COMPILER_UNUSED(self);
    *name = "input";
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WmaDecoderInput_GetProtocol
+---------------------------------------------------------------------*/
BLT_METHOD 
WmaDecoderInput_GetProtocol(BLT_MediaPort*         _self,
                            BLT_MediaPortProtocol* protocol)
{
    WmaDecoderInput* self = ATX_SELF(WmaDecoderInput, BLT_MediaPort);    
    if (self->packet_mode) {
        *protocol = BLT_MEDIA_PORT_PROTOCOL_PACKET;
    } else {
        *protocol = BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WmaDecoderInput_GetDirection
+---------------------------------------------------------------------*/
BLT_METHOD 
WmaDecoderInput_GetDirection(BLT_MediaPort*          self,
                             BLT_MediaPortDirection* direction)
{
    BLT_COMPILER_UNUSED(self);
    *direction = BLT_MEDIA_PORT_DIRECTION_IN;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(WmaDecoderInput, BLT_MediaPort)
    WmaDecoderInput_GetName,
    WmaDecoderInput_GetProtocol,
    WmaDecoderInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    WmaDecoderOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoderOutput_GetPacket(BLT_PacketProducer* _self,
                           BLT_MediaPacket**   packet)
{
    WmaDecoder*    self = ATX_SELF_M(output, WmaDecoder, BLT_PacketProducer);
    tWMA_U32       decoded_samples;
    tWMA_U32       pcm_samples;
    tWMA_I64       timestamp;
    BLT_UInt32     buffer_size;
    BLT_Result     result;
    tWMAFileStatus rc;
    unsigned int   watchdog;
    
    /* default value */
    *packet = NULL;

    /* check if we've got more data to decode */
    if (self->input.eos) {
        return BLT_ERROR_EOS;
    }
    
    /* decode one frame */
    if (self->output.samples_pending) {
        /* get a packet from the core */
        buffer_size = BLT_WMA_DECODER_DEFAULT_PCM_BUFFER_SIZE*
                      (self->output.media_type.bits_per_sample/8)*
                       self->output.media_type.channel_count;
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                            buffer_size,
                                            (BLT_MediaType*)&self->output.media_type,
                                            packet);
        if (BLT_FAILED(result)) return result;

        /* first try to see if there are buffered samples available */
        pcm_samples = WMAFileGetPCM(self->wma_handle, 
                                    BLT_MediaPacket_GetPayloadBuffer(*packet), NULL, 
                                    buffer_size, decoded_samples, &timestamp);
        if (pcm_samples) {
            buffer_size = pcm_samples*
                          (self->output.media_type.bits_per_sample/8)*
                           self->output.media_type.channel_count;
            BLT_MediaPacket_SetPayloadSize(*packet, buffer_size);
            return BLT_SUCCESS;
        }

        /* if we reach this point, we know we've consumed all the pending samples */
        self->output.samples_pending = ATX_FALSE;
    }        
    
    /* don't go any further if we've reached the end */
    if (self->input.eos) return BLT_ERROR_EOS;

    /* we need to decode some more data */
    watchdog = 0;
    result = BLT_SUCCESS;
    do {
        if (++watchdog > BLT_WMA_DECODER_MAX_DECODE_LOOP) {
            ATX_LOG_WARNING("decode loop watchdog bit us");
            result = BLT_ERROR_EOS;
            break;
        }
        if (self->input.eos) {
            ATX_LOG_INFO("eos");
            result = BLT_ERROR_EOS;
            break;
        }
        
        /* in packet mode, we want at least some packets in the list */
        if (self->input.packet_mode) {
            unsigned int min_pending = 2;
            if (self->input.wms_stream_change) {
                /* we need to consume all pending packets */
                min_pending = 1;
            }
            if (self->input.packets == NULL || ATX_List_GetItemCount(self->input.packets) < min_pending) {
                result = BLT_ERROR_PORT_HAS_NO_DATA;
                break;
            }
        }
        
        /* try to decode some data */
        rc = WMAFileDecodeData(self->wma_handle, &decoded_samples);
        if (rc == cWMA_NoMoreFrames) {
            ATX_LOG_FINE("no more frames, end of stream");
            result = BLT_ERROR_EOS;
            break;
        } else if (rc != cWMA_NoErr) {
            ATX_LOG_WARNING_1("WMAFileDecodeData failed (%d)", rc);
            result = BLT_FAILURE;
            break;
        }
    } while (decoded_samples == 0);

    /* check if we decoded something */
    if (BLT_FAILED(result)) { 
        if (*packet) {
            BLT_MediaPacket_Release(*packet);
            *packet = NULL;
        }
        return result;
    }
    
    /* mark that we may have pending samples */
    self->output.samples_pending = ATX_TRUE;
    
    /* allocate a new packet if we have not done so already, or reuse one */
    buffer_size = decoded_samples*
                  (self->output.media_type.bits_per_sample/8)*
                   self->output.media_type.channel_count;
    if (*packet == NULL) {
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core,
                                            buffer_size,
                                            (BLT_MediaType*)&self->output.media_type,
                                            packet);
        if (BLT_FAILED(result)) return result;
    } else {
        /* resize the packet we already have */
        BLT_MediaPacket_SetPayloadSize(*packet, buffer_size);
    }
    
    /* convert to PCM */
    pcm_samples = WMAFileGetPCM(self->wma_handle, 
                                BLT_MediaPacket_GetPayloadBuffer(*packet), NULL, 
                                buffer_size, decoded_samples, &timestamp);
    if (pcm_samples) {
        buffer_size = pcm_samples*
                      (self->output.media_type.bits_per_sample/8)*
                       self->output.media_type.channel_count;
        BLT_MediaPacket_SetPayloadSize(*packet, buffer_size);
        return BLT_SUCCESS;
    } else {
        self->output.samples_pending = ATX_FALSE;
        BLT_MediaPacket_Release(*packet);
        *packet = NULL;
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoderOutput)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(WmaDecoderOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(WmaDecoderOutput,
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(WmaDecoderOutput, BLT_MediaPort)
    WmaDecoderOutput_GetName,
    WmaDecoderOutput_GetProtocol,
    WmaDecoderOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(WmaDecoderOutput, BLT_PacketProducer)
    WmaDecoderOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    WmaDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoder_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  const void*              parameters, 
                  BLT_MediaNode**          object)
{
    WmaDecoder*                     decoder;
    const BLT_MediaNodeConstructor* constructor = (const BLT_MediaNodeConstructor*)parameters;

    ATX_LOG_FINE("WmaDecoder::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    decoder = ATX_AllocateZeroMemory(sizeof(WmaDecoder));
    if (decoder == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(decoder, BLT_BaseMediaNode), module, core);

    /* construct the object */
    decoder->input.wma_type_id = ATX_SELF_EX_O(module, WmaDecoderModule, BLT_BaseModule, BLT_Module)->wma_type_id;
    decoder->input.mms_type_id = ATX_SELF_EX_O(module, WmaDecoderModule, BLT_BaseModule, BLT_Module)->mms_type_id;
    BLT_PcmMediaType_Init(&decoder->output.media_type);
    decoder->wma_handle = NULL;
    ATX_DataBuffer_Create(4096, &decoder->wma_buffer);
    decoder->input.wma_selected_stream = 1;
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(decoder, WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(decoder, WmaDecoder, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&decoder->input,  WmaDecoderInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&decoder->output, WmaDecoderOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&decoder->output, WmaDecoderOutput, BLT_PacketProducer);
    if (constructor->spec.input.media_type->id == decoder->input.mms_type_id ) {
        decoder->input.packet_mode = ATX_TRUE;
        ATX_SET_INTERFACE(&decoder->input, WmaDecoderInput,  BLT_PacketConsumer);
    } else {
        decoder->input.packet_mode = ATX_FALSE;
        ATX_SET_INTERFACE(&decoder->input, WmaDecoderInput,  BLT_InputStreamUser);
    }
    *object = &ATX_BASE_EX(decoder, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    WmaDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
WmaDecoder_Destroy(WmaDecoder* self)
{
    ATX_LOG_FINE("enter");

    /* free the WMA decoder */
    if (self->wma_handle) {
        WMAFileDecodeClose(&self->wma_handle);
    }
    
    /* free the WMA buffer */
    ATX_DataBuffer_Destroy(self->wma_buffer);

    /* release the input stream and/or packet*/
    ATX_RELEASE_OBJECT(self->input.stream);
    WmaDecoderInput_Flush(&self->input);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory((void*)self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|    WmaDecoder_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_Deactivate(BLT_MediaNode* _self)
{
    WmaDecoder* self = ATX_SELF_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_FINER("WmaDecoder::Deactivate");

    /* call the base class method */
    BLT_BaseMediaNode_Deactivate(_self);

    /* release the input stream */
    ATX_RELEASE_OBJECT(self->input.stream);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   WmaDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    WmaDecoder* self = ATX_SELF_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);

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
|    WmaDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoder_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    WmaDecoder* self = ATX_SELF_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode);
    tWMA_U32 ts_ms;
    tWMA_U32 ts_ms_actual = 0;

    if (self->input.packet_mode) {
        WmaDecoderInput_Flush(&self->input);
        self->input.eos = ATX_FALSE;
        self->output.samples_pending = ATX_FALSE;
        return BLT_SUCCESS;
    }
    
    /* estimate the seek point in time_stamp mode */
    if (ATX_BASE(self, BLT_BaseMediaNode).context == NULL) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(ATX_BASE(self, BLT_BaseMediaNode).context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP)) {
        return BLT_FAILURE;
    }

    /* seek to the target time */
    ts_ms = point->time_stamp.seconds*1000+point->time_stamp.nanoseconds/1000000;
    ATX_LOG_FINER_1("seek to %d", ts_ms);

    WMAFileSeek(self->wma_handle, ts_ms, &ts_ms_actual);

    ATX_LOG_FINER_1("actual timestamp = %d", ts_ms_actual);

    /* set the mode so that the nodes down the chain know the seek has */
    /* already been done on the stream                                 */
    *mode = BLT_SEEK_MODE_IGNORE;

    /* clear eos flag if set */
    self->input.eos = ATX_FALSE;
    
    /* clear output state */
    self->output.samples_pending = ATX_FALSE;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoder)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoder, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(WmaDecoder, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    WmaDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    WmaDecoder_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    WmaDecoder_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(WmaDecoder, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   WmaDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoderModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    WmaDecoderModule* self = ATX_SELF_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*     registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".wma" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".wma",
                                            "audio/x-ms-wma");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/x-ms-wma" type */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-ms-wma",
        &self->wma_type_id);
    if (BLT_FAILED(result)) return result;
    
    /* register the "application/x-mms-framed" type */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "application/x-mms-framed",
        &self->mms_type_id);
    if (BLT_FAILED(result)) return result;

    ATX_LOG_FINE_1("(audio/x-ms-wma type = %d)", self->wma_type_id);
    ATX_LOG_FINE_1("(application/x-mms-framed type = %d)", self->mms_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   WmaDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
WmaDecoderModule_Probe(BLT_Module*              _self, 
                       BLT_Core*                core,
                       BLT_ModuleParametersType parameters_type,
                       BLT_AnyConst             parameters,
                       BLT_Cardinal*            match)
{
    WmaDecoderModule* self = ATX_SELF_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* output protocol should be PACKET */
            if ((constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be audio/x-ms-wma or application/x-mms-framed */
            if (constructor->spec.input.media_type->id == self->wma_type_id) {
                if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                     constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL)) {
                    return BLT_FAILURE;
                }
            } else if (constructor->spec.input.media_type->id == self->mms_type_id) {
                if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                     constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                    return BLT_FAILURE;
                }
            } else {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN) &&
                !(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "WmaDecoder")) {
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

            ATX_LOG_FINE_1("WmaDecoderModule::Probe - Ok [%d]", *match); 
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(WmaDecoderModule)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(WmaDecoderModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(WmaDecoderModule, WmaDecoder)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(WmaDecoderModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    WmaDecoderModule_Attach,
    WmaDecoderModule_CreateInstance,
    WmaDecoderModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define WmaDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(WmaDecoderModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(WmaDecoderModule,
                                         "WMA Audio Decoder",
                                         "com.axiosys.decoder.wma",
                                         "1.3.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)

