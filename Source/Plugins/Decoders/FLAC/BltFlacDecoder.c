/*****************************************************************
|
|      File: BltFlacDecoder.c
|
|      Flac Decoder Module
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
#include "BltFlacDecoder.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltByteStreamUser.h"
#include "BltStream.h"
#include "BltReplayGain.h"

#include "FLAC/stream_decoder.h"
#include "FLAC/seekable_stream_decoder.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderModule)
static const BLT_ModuleInterface FlacDecoderModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoder)
static const BLT_MediaNodeInterface FlacDecoder_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderInputPort)

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderOutputPort)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
    BLT_UInt32     flac_type_id;
} FlacDecoderModule;

typedef struct {
    BLT_Boolean     eos;
    ATX_InputStream stream;
    BLT_Size        size;
    BLT_MediaTypeId media_type_id;
} FlacDecoderInputPort;

typedef struct {
    BLT_PcmMediaType media_type;
    ATX_List*        packets;
    BLT_Cardinal     packet_count;
    ATX_Int64        sample_count;
    BLT_TimeStamp    time_stamp;
    BLT_Boolean      eos;
} FlacDecoderOutputPort;

typedef struct {
    BLT_BaseMediaNode               base;
    BLT_Stream                      context;
    FlacDecoderInputPort            input;
    FlacDecoderOutputPort           output;
    FLAC__SeekableStreamDecoder*    flac;
    FLAC__StreamMetadata_StreamInfo stream_info;
} FlacDecoder;

/*----------------------------------------------------------------------
|       FlacDecoderInputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoderInputPort_SetStream(BLT_InputStreamUserInstance* instance,
                               ATX_InputStream*             stream,
                               const BLT_MediaType*         media_type)
{
    FlacDecoder* decoder = (FlacDecoder*)instance;

    /* check the stream's media type */
    if (media_type == NULL || 
        media_type->id != decoder->input.media_type_id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(&decoder->input.stream);

    /* keep a reference to the stream */
    decoder->input.stream = *stream;
    ATX_REFERENCE_OBJECT(stream);

    /* reset counters and flags */
    decoder->input.size = 0;
    decoder->input.eos = BLT_FALSE;
    decoder->output.eos = BLT_FALSE;
    decoder->output.packet_count = 0;
    ATX_Int64_Set_Int32(decoder->output.sample_count, 0);
    BLT_TimeStamp_Set(decoder->output.time_stamp, 0, 0);

    /* get stream size */
    ATX_InputStream_GetSize(stream, &decoder->input.size);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FlacDecoderInputPort,
                                         "input",
                                         STREAM_PULL,
                                         IN)
static const BLT_MediaPortInterface
FlacDecoderInputPort_BLT_MediaPortInterface = {
    FlacDecoderInputPort_GetInterface,
    FlacDecoderInputPort_GetName,
    FlacDecoderInputPort_GetProtocol,
    FlacDecoderInputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamUserInterface
FlacDecoderInputPort_BLT_InputStreamUserInterface = {
    FlacDecoderInputPort_GetInterface,
    FlacDecoderInputPort_SetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderInputPort)
ATX_INTERFACE_MAP_ADD(FlacDecoderInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(FlacDecoderInputPort, BLT_InputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderInputPort)

/*----------------------------------------------------------------------
|    FlacDecoderOutputPort_Flush
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoderOutputPort_Flush(FlacDecoder* decoder)
{
    ATX_ListItem* item;
    while ((item = ATX_List_GetFirstItem(decoder->output.packets))) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) BLT_MediaPacket_Release(packet);
        ATX_List_RemoveItem(decoder->output.packets, item);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FlacDecoderOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoderOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                                BLT_MediaPacket**           packet)
{
    FlacDecoder*                     decoder = (FlacDecoder*)instance;
    FLAC__SeekableStreamDecoderState flac_state;
    FLAC__bool                       flac_result;
    BLT_Result                       result;

    /* check for EOS */
    if (decoder->output.eos) {
        *packet = NULL;
        return BLT_ERROR_EOS;
    }

    /* decode until we have some data available */
    do {
        ATX_ListItem* item;
        item = ATX_List_GetFirstItem(decoder->output.packets);
        if (item != NULL) {
            *packet = ATX_ListItem_GetData(item);
            ATX_List_RemoveItem(decoder->output.packets, item);
            
            /* set flags */     
            if (decoder->output.packet_count == 0) {
                /* this is the first packet */
                BLT_MediaPacket_SetFlags(*packet,
                                        BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
            }

            /* update packet count */
            decoder->output.packet_count++;

            return BLT_SUCCESS;
        }

        /* no more data available, decode some more */
        flac_state = FLAC__seekable_stream_decoder_get_state(decoder->flac);
        if (flac_state == FLAC__SEEKABLE_STREAM_DECODER_OK) {
            flac_result = 
                FLAC__seekable_stream_decoder_process_single(decoder->flac);
            flac_state = FLAC__seekable_stream_decoder_get_state(decoder->flac);
        } else {
            break;
        }
    } while (flac_result == 1);
             
    /* return an empty packet with EOS flag */
    decoder->output.eos = BLT_TRUE;
    result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                        0,
                                        (BLT_MediaType*)&decoder->output.media_type,
                                        packet);
    if (BLT_FAILED(result)) return result;
    BLT_MediaPacket_SetFlags(*packet, 
                             BLT_MEDIA_PACKET_FLAG_END_OF_STREAM);
    BLT_MediaPacket_SetTimeStamp(*packet, decoder->output.time_stamp);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FlacDecoderOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
FlacDecoderOutputPort_BLT_MediaPortInterface = {
    FlacDecoderOutputPort_GetInterface,
    FlacDecoderOutputPort_GetName,
    FlacDecoderOutputPort_GetProtocol,
    FlacDecoderOutputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
FlacDecoderOutputPort_BLT_PacketProducerInterface = {
    FlacDecoderOutputPort_GetInterface,
    FlacDecoderOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderOutputPort)
ATX_INTERFACE_MAP_ADD(FlacDecoderOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(FlacDecoderOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderOutputPort)

/*----------------------------------------------------------------------
|       FlacDecoder_ReadCallback
+---------------------------------------------------------------------*/
static FLAC__SeekableStreamDecoderReadStatus 
FlacDecoder_ReadCallback(const FLAC__SeekableStreamDecoder* flac, 
                         FLAC__byte                         buffer[], 
                         unsigned*                          bytes, 
                         void*                              client_data)
{
    FlacDecoder*  decoder = (FlacDecoder*)client_data;
    BLT_Size      bytes_read;
    BLT_Result    result;

    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /* compute how many bytes we need to read */
    bytes_read = 0;

    /* read from the input stream */
    result = ATX_InputStream_Read(&decoder->input.stream,
                                  buffer,
                                  *bytes,
                                  &bytes_read);
    if (BLT_FAILED(result)) {
        if (result == BLT_ERROR_EOS) {
            *bytes = 0;
            decoder->input.eos = BLT_TRUE;
        }
        return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_ERROR;
    }

    *bytes = bytes_read;
    return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK;
}

/*----------------------------------------------------------------------
|       FlacDecoder_SeekCallback
+---------------------------------------------------------------------*/
static FLAC__SeekableStreamDecoderSeekStatus 
FlacDecoder_SeekCallback(const FLAC__SeekableStreamDecoder *flac, 
                         FLAC__uint64                       offset, 
                         void*                              client_data)
{
    FlacDecoder* decoder = (FlacDecoder*)client_data;
    BLT_Result   result;

    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /* clear the EOS flag */
    decoder->input.eos  = BLT_FALSE;
    decoder->output.eos = BLT_FALSE;

    /* seek */
    BLT_Debug("FlacDecoder::SeekCallback - offset = %ld\n", offset);
    result = ATX_InputStream_Seek(&decoder->input.stream, (ATX_Offset)offset);
    if (BLT_FAILED(result)) {
        return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_ERROR;
    }

    return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK;
}

/*----------------------------------------------------------------------
|       FlacDecoder_TellCallback
+---------------------------------------------------------------------*/
static FLAC__SeekableStreamDecoderTellStatus 
FlacDecoder_TellCallback(const FLAC__SeekableStreamDecoder* flac, 
                         FLAC__uint64*                      offset, 
                         void*                              client_data)
{
    FlacDecoder* decoder = (FlacDecoder*)client_data;
    BLT_Offset   stream_offset;
    BLT_Result   result;

    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /* return the stream position */
    result = ATX_InputStream_Tell(&decoder->input.stream, &stream_offset);
    if (BLT_FAILED(result)) {
        *offset = 0;
        return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;
    }

    /*BLT_Debug("FlacDecoder::TellCallback - offset = %ld\n", *offset);*/
    *offset = stream_offset;

    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK;
}

/*----------------------------------------------------------------------
|       FlacDecoder_LengthCallback
+---------------------------------------------------------------------*/
static FLAC__SeekableStreamDecoderLengthStatus 
FlacDecoder_LengthCallback(const FLAC__SeekableStreamDecoder* flac, 
                           FLAC__uint64*                      stream_length, 
                           void*                              client_data)
{
    FlacDecoder* decoder = (FlacDecoder*)client_data;
    BLT_Size     size;
    BLT_Result   result;

    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /* get the stream size */
    result = ATX_InputStream_GetSize(&decoder->input.stream, &size);
    if (BLT_FAILED(result)) {
        *stream_length = 0;
    } else {
        *stream_length = size;
    }

    /*BLT_Debug("FlacDecoder::LengthCallback - length = %ld\n", *stream_length);*/

    return FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK;
}

/*----------------------------------------------------------------------
|       FlacDecoder_EofCallback
+---------------------------------------------------------------------*/
static FLAC__bool 
FlacDecoder_EofCallback(const FLAC__SeekableStreamDecoder* flac, 
                        void*                              client_data)
{
    FlacDecoder* decoder = (FlacDecoder*)client_data;
    
    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /*if (decoder->input.eos) BLT_Debug("FlacDecoder::EofCallback - EOF\n");*/
    return decoder->input.eos == BLT_TRUE ? 1:0;
}

/*----------------------------------------------------------------------
|       FlacDecoder_WriteCallback
+---------------------------------------------------------------------*/
static FLAC__StreamDecoderWriteStatus 
FlacDecoder_WriteCallback(const FLAC__SeekableStreamDecoder* flac, 
                          const FLAC__Frame*                 frame, 
                          const FLAC__int32* const           buffer[], 
                          void*                              client_data)
{
    FlacDecoder*     decoder = (FlacDecoder*)client_data;
    BLT_MediaPacket* packet;
    BLT_Size         packet_size;
    BLT_Result       result;

    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /*BLT_Debug("FlacDecoder::WriteCallback - size=%ld\n", 
      frame->header.blocksize);*/

    /* check format */
    if (frame->header.channels == 0 ||
        frame->header.channels > 2  ||
        frame->header.channels < 1) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    /* support 16, 24 and 32 bps */
    if (frame->header.bits_per_sample != 16 &&
        frame->header.bits_per_sample != 24 &&
        frame->header.bits_per_sample != 32) {
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    /* compute packet size */
    packet_size = frame->header.blocksize * frame->header.channels * frame->header.bits_per_sample/8;
    
    /* set the packet media type */
    decoder->output.media_type.sample_rate     = frame->header.sample_rate;
    decoder->output.media_type.channel_count   = frame->header.channels;
    decoder->output.media_type.bits_per_sample = frame->header.bits_per_sample;
    decoder->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* get a packet from the core */
    result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                        packet_size,
                                        (BLT_MediaType*)&decoder->output.media_type,
                                        &packet);
    if (BLT_FAILED(result)) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

    /* convert sample buffer */
    if (frame->header.channels == 2) {
        unsigned int sample;
        switch (frame->header.bits_per_sample) {
            case 16:
                {
                    short* dst = (short*)BLT_MediaPacket_GetPayloadBuffer(packet);
                    for (sample = 0; sample < frame->header.blocksize; sample++) {
                        *dst++ = buffer[0][sample];
                        *dst++ = buffer[1][sample];
                    }
                }
                break;

            case 24:
                {
                    unsigned char* dst = (unsigned char*)BLT_MediaPacket_GetPayloadBuffer(packet);
                    for (sample = 0; sample < frame->header.blocksize; sample++) {
                        unsigned char* src_0 = (unsigned char*)&(buffer[0][sample]);
                        unsigned char* src_1 = (unsigned char*)&(buffer[1][sample]);
                        *dst++ = src_0[0];
                        *dst++ = src_0[1];
                        *dst++ = src_0[2];
                        *dst++ = src_1[0];
                        *dst++ = src_1[1];
                        *dst++ = src_1[2];
                    }
                }
                break;

            case 32:
                {
                    FLAC__int32* dst = (FLAC__int32*)BLT_MediaPacket_GetPayloadBuffer(packet);
                    for (sample = 0; sample < frame->header.blocksize; sample++) {
                        *dst++ = buffer[0][sample];
                        *dst++ = buffer[1][sample];
                    }
                }
                break;
        }
    } else {
        ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(packet), 
                       buffer[0], packet_size);
    }

    /* update the size of the packet */
    BLT_MediaPacket_SetPayloadSize(packet, packet_size);
    
    /* set the packet time stamp */
    if (frame->header.sample_rate) {
        BLT_TimeStamp_FromSamples(&decoder->output.time_stamp, 
                                  decoder->output.sample_count,
                                  frame->header.sample_rate);
        BLT_MediaPacket_SetTimeStamp(packet, decoder->output.time_stamp);
    }

    /* update the sample count */
    ATX_Int64_Add_Int32(decoder->output.sample_count,  
                        frame->header.blocksize);

    /* add the packet to our output queue */
    ATX_List_AddData(decoder->output.packets, packet);

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/*----------------------------------------------------------------------
|       FlacDecoder_HandleStreamInfo
+---------------------------------------------------------------------*/
static void
FlacDecoder_HandleStreamInfo(
    FlacDecoder*                           decoder, 
    const FLAC__StreamMetadata_StreamInfo* flac_info)
{
    /* update the stream info */
    decoder->stream_info = *flac_info;
    if (!ATX_OBJECT_IS_NULL(&decoder->base.context)) {
        BLT_StreamInfo info;
        FLAC__uint64   duration;
        FLAC__uint64   bitrate;

        /* start with no info */
        info.mask = 0;

        /* sample rate */
        info.sample_rate = flac_info->sample_rate;
        info.mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;

        /* channel count */
        info.channel_count = flac_info->channels;
        info.mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;

        /* compute duration from samples and sample_rate */
        if (flac_info->sample_rate) {
            duration = (flac_info->total_samples*1000)/flac_info->sample_rate;
            info.mask |= BLT_STREAM_INFO_MASK_DURATION;
        } else {
            duration = 0;
        }
        info.duration = (BLT_Cardinal)duration;

        /* compute bitrate from input size and duration */
        if (duration) {
            bitrate = 8*1000*(FLAC__uint64)decoder->input.size/duration;
            info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;
            info.mask |= BLT_STREAM_INFO_MASK_AVERAGE_BITRATE;
            info.mask |= BLT_STREAM_INFO_MASK_INSTANT_BITRATE;
        } else {
            bitrate = 0;
        }
        info.nominal_bitrate = (BLT_Cardinal)bitrate;
        info.average_bitrate = (BLT_Cardinal)bitrate;
        info.instant_bitrate = (BLT_Cardinal)bitrate;

        /* data type */
        info.data_type = "FLAC";
        info.mask |= BLT_STREAM_INFO_MASK_DATA_TYPE;

        /* send update */
        BLT_Stream_SetInfo(&decoder->base.context, &info);
    }
}

/*----------------------------------------------------------------------
|       FlacDecoder_HandleVorbisComment
+---------------------------------------------------------------------*/
static void
FlacDecoder_HandleVorbisComment(
    FlacDecoder*                              decoder,
    const FLAC__StreamMetadata_VorbisComment* comment)
{
    unsigned int i;
    ATX_String   string = ATX_EMPTY_STRING;
    ATX_String   key    = ATX_EMPTY_STRING;
    ATX_String   value  = ATX_EMPTY_STRING;
    float        track_gain = 0.0f;
    float        album_gain = 0.0f;
    ATX_Boolean  track_gain_set = ATX_FALSE;
    ATX_Boolean  album_gain_set = ATX_FALSE;

    ATX_String_AssignN(&string,
                       comment->vendor_string.entry,
                       comment->vendor_string.length);
    BLT_Debug("VENDOR = %s\n", ATX_CSTR(string));
    for (i=0; i<comment->num_comments; i++) {
        int sep;
        ATX_String_AssignN(&string, 
                           comment->comments[i].entry,
                           comment->comments[i].length);
        sep = ATX_String_FindChar(&string, '=');
        if (sep == ATX_STRING_SEARCH_FAILED) continue;
        ATX_String_AssignN(&key, ATX_CSTR(string), sep);
        ATX_String_Assign(&value, ATX_CSTR(string)+sep+1);

        BLT_Debug("  COMMENT %d : %s = %s\n", i, ATX_CSTR(key), ATX_CSTR(value));
        ATX_String_ToUppercase(&key);
        if (ATX_String_Equals(&key, BLT_VORBIS_COMMENT_REPLAY_GAIN_TRACK_GAIN, ATX_FALSE)) {
            ATX_String_ToFloat(&value, &track_gain, ATX_TRUE);
            track_gain_set = ATX_TRUE;
        } else if (ATX_String_Equals(&key, BLT_VORBIS_COMMENT_REPLAY_GAIN_ALBUM_GAIN, ATX_FALSE)) {
            ATX_String_ToFloat(&value, &album_gain, ATX_TRUE);
            album_gain_set = ATX_TRUE;
        }
    }

    /* update the stream info */
    BLT_ReplayGain_SetStreamProperties(&decoder->base.context,
                                       track_gain, track_gain_set,
                                       album_gain, album_gain_set);

    ATX_String_Destruct(&string);
    ATX_String_Destruct(&key);
    ATX_String_Destruct(&value);
}

    
/*----------------------------------------------------------------------
|       FlacDecoder_MetaDataCallback
+---------------------------------------------------------------------*/
static void 
FlacDecoder_MetaDataCallback(const FLAC__SeekableStreamDecoder* flac, 
                             const FLAC__StreamMetadata*        metadata, 
                             void*                              client_data)
{
    FlacDecoder* decoder = (FlacDecoder*)client_data;
    
    /* unused parameters */
    BLT_COMPILER_UNUSED(flac);

    /*BLT_Debug("FlacDecoder::MetaDataCallback\n");*/

    /* get metadata block */
    switch (metadata->type) {
      case FLAC__METADATA_TYPE_STREAMINFO:
        FlacDecoder_HandleStreamInfo(decoder, &metadata->data.stream_info);
        break;
        
      case FLAC__METADATA_TYPE_VORBIS_COMMENT:
        FlacDecoder_HandleVorbisComment(decoder, &metadata->data.vorbis_comment);
        break;

      default:
        break;
    }
}

/*----------------------------------------------------------------------
|       FlacDecoder_ErrorCallback
+---------------------------------------------------------------------*/
static void 
FlacDecoder_ErrorCallback(const FLAC__SeekableStreamDecoder* flac, 
                          FLAC__StreamDecoderErrorStatus     status, 
                          void*                              client_data)
{
    /* IGNORE */
    BLT_COMPILER_UNUSED(flac);
    BLT_COMPILER_UNUSED(status);
    BLT_COMPILER_UNUSED(client_data);

    /*BLT_Debug("FlacDecoder::ErrorCallback (%d)\n", status);*/
}

/*----------------------------------------------------------------------
|    FlacDecoder_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
FlacDecoder_SetupPorts(FlacDecoder* decoder, BLT_MediaTypeId flac_type_id)
{
    ATX_Result result;

    /* setup the input port */
    decoder->input.eos = BLT_FALSE;
    ATX_CLEAR_OBJECT(&decoder->input.stream);
    decoder->input.media_type_id = flac_type_id;

    /* setup the output port */
    decoder->output.eos                    = BLT_FALSE;
    decoder->output.packet_count           = 0;
    BLT_PcmMediaType_Init(&decoder->output.media_type);
    decoder->output.media_type.sample_rate     = 0;
    decoder->output.media_type.channel_count   = 0;
    decoder->output.media_type.bits_per_sample = 0;
    decoder->output.media_type.sample_format   = 0;

    /* create a list of output packets */
    result = ATX_List_Create(&decoder->output.packets);
    if (ATX_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FlacDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
FlacDecoder_Create(BLT_Module*              module,
                   BLT_Core*                core, 
                   BLT_ModuleParametersType parameters_type,
                   BLT_CString              parameters, 
                   ATX_Object*              object)
{
    FlacDecoder* decoder;
    BLT_Result        result;

    BLT_Debug("FlacDecoder::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    decoder = ATX_AllocateZeroMemory(sizeof(FlacDecoder));
    if (decoder == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&decoder->base, module, core);

    /* create FLAC decoder */
    decoder->flac = FLAC__seekable_stream_decoder_new();

    /* setup the flac decoder */
    FLAC__seekable_stream_decoder_set_client_data(decoder->flac, decoder);
    FLAC__seekable_stream_decoder_set_read_callback(decoder->flac,
                                                    FlacDecoder_ReadCallback);
    FLAC__seekable_stream_decoder_set_seek_callback(decoder->flac,
                                                    FlacDecoder_SeekCallback);
    FLAC__seekable_stream_decoder_set_tell_callback(decoder->flac,
                                                    FlacDecoder_TellCallback);
    FLAC__seekable_stream_decoder_set_length_callback(decoder->flac,
                                                  FlacDecoder_LengthCallback);
    FLAC__seekable_stream_decoder_set_eof_callback(decoder->flac,
                                                   FlacDecoder_EofCallback);
    FLAC__seekable_stream_decoder_set_write_callback(decoder->flac,
                                                 FlacDecoder_WriteCallback);
    FLAC__seekable_stream_decoder_set_metadata_callback(decoder->flac,
                                                FlacDecoder_MetaDataCallback);
    FLAC__seekable_stream_decoder_set_error_callback(decoder->flac,
                                                 FlacDecoder_ErrorCallback);
    FLAC__seekable_stream_decoder_set_metadata_respond(decoder->flac,
        FLAC__METADATA_TYPE_VORBIS_COMMENT);
    if (FLAC__seekable_stream_decoder_init(decoder->flac) !=
        FLAC__SEEKABLE_STREAM_DECODER_OK) {
        FLAC__seekable_stream_decoder_delete(decoder->flac);
        ATX_FreeMemory((void*)decoder);
        return BLT_ERROR_INTERNAL;
    }

    /* setup the input and output ports */
    result = FlacDecoder_SetupPorts(decoder, 
                                    ((FlacDecoderModule*)ATX_INSTANCE(module))->flac_type_id);
    if (BLT_FAILED(result)) {
        ATX_FreeMemory(decoder);
        ATX_CLEAR_OBJECT(object);
        return result;
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)decoder;
    ATX_INTERFACE(object) = (ATX_Interface*)&FlacDecoder_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FlacDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FlacDecoder_Destroy(FlacDecoder* decoder)
{
    ATX_ListItem* item;
    
    BLT_Debug("FlacDecoder::Destroy\n");

    /* release the input stream */
    ATX_RELEASE_OBJECT(&decoder->input.stream);

    /* release any output packet we may hold */
    item = ATX_List_GetFirstItem(decoder->output.packets);
    while (item) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) {
            BLT_MediaPacket_Release(packet);
        }
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Destroy(decoder->output.packets);
    
    /* destroy the FLAC decoder */
    if (decoder->flac) {
        FLAC__seekable_stream_decoder_delete(decoder->flac);
    }
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&decoder->base);

    /* free the object memory */
    ATX_FreeMemory(decoder);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|       FlacDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoder_GetPortByName(BLT_MediaNodeInstance* instance,
                          BLT_CString            name,
                          BLT_MediaPort*         port)
{
    FlacDecoder* decoder = (FlacDecoder*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &FlacDecoderInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &FlacDecoderOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    FlacDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoder_Seek(BLT_MediaNodeInstance* instance,
                 BLT_SeekMode*          mode,
                 BLT_SeekPoint*         point)
{
    FlacDecoder* decoder = (FlacDecoder*)instance;

    /* flush pending packets */
    FlacDecoderOutputPort_Flush(decoder);

    /* estimate the seek point in time_stamp mode */
    if (ATX_OBJECT_IS_NULL(&decoder->base.context)) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(&decoder->base.context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_SAMPLE)) {
        return BLT_FAILURE;
    }

    /* update the output sample count */
    decoder->output.sample_count = point->sample;

    /* seek to the target sample */
    BLT_Debug("FlacDecoder::Seek - sample = %ld\n", 
              (long)point->sample);
    FLAC__seekable_stream_decoder_seek_absolute(decoder->flac, point->sample);

    /* set the mode so that the nodes down the chaine know the seek has */
    /* already been done on the stream                                  */
    *mode = BLT_SEEK_MODE_IGNORE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
FlacDecoder_BLT_MediaNodeInterface = {
    FlacDecoder_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    FlacDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    FlacDecoder_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FlacDecoder, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoder)
ATX_INTERFACE_MAP_ADD(FlacDecoder, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(FlacDecoder, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoder)

/*----------------------------------------------------------------------
|       FlacDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoderModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    FlacDecoderModule* module = (FlacDecoderModule*)instance;
    BLT_Registry      registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".flac" file extension */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".flac",
                                            "audio/x-flac");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/x-flac" type */
    result = BLT_Registry_RegisterName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-flac",
        &module->flac_type_id);
    if (BLT_FAILED(result)) return result;
    
    BLT_Debug("FlacDecoderModule::Attach (audio/x-flac type = %d)\n", 
              module->flac_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       FlacDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FlacDecoderModule_Probe(BLT_ModuleInstance*      instance, 
                        BLT_Core*                core,
                        BLT_ModuleParametersType parameters_type,
                        BLT_AnyConst             parameters,
                        BLT_Cardinal*            match)
{
    FlacDecoderModule* module = (FlacDecoderModule*)instance;
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be STREAM_PULL and the */
            /* output protocol should be PACKET                 */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be audio/x-flac */
            if (constructor->spec.input.media_type->id != 
                module->flac_type_id) {
                return BLT_FAILURE;     
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == 
                  BLT_MEDIA_TYPE_ID_UNKNOWN) &&
                !(constructor->spec.output.media_type->id == 
                  BLT_MEDIA_TYPE_ID_AUDIO_PCM)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "FlacDecoder")) {
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
            
            BLT_Debug("FlacDecoderModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FlacDecoder)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(FlacDecoder, "FLAC Decoder", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface FlacDecoderModule_BLT_ModuleInterface = {
    FlacDecoderModule_GetInterface,
    BLT_BaseModule_GetInfo,
    FlacDecoderModule_Attach,
    FlacDecoderModule_CreateInstance,
    FlacDecoderModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FlacDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FlacDecoderModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderModule) 
ATX_INTERFACE_MAP_ADD(FlacDecoderModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(FlacDecoderModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FlacDecoderModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_FlacDecoderModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return FlacDecoderModule_Create(object);
}
