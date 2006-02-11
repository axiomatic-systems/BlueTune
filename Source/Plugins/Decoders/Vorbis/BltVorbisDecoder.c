/****************************************************************
|
|      File: BltVorbisDecoder.c
|
|      Vorbis Decoder Module
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
#include "BltVorbisDecoder.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltByteStreamUser.h"
#include "BltStream.h"
#include "BltReplayGain.h"

#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderModule)
static const BLT_ModuleInterface VorbisDecoderModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoder)
static const BLT_MediaNodeInterface VorbisDecoder_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderInputPort)
static const BLT_MediaPortInterface VorbisDecoderInputPort_BLT_MediaPortInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderOutputPort)
static const BLT_MediaPortInterface VorbisDecoderOutputPort_BLT_MediaPortInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
    BLT_UInt32     ogg_type_id;
} VorbisDecoderModule;

typedef struct {
    ATX_InputStream stream;
    BLT_MediaTypeId media_type_id;
    BLT_Size        size;
    OggVorbis_File  vorbis_file;
    BLT_Boolean     eos;
} VorbisDecoderInputPort;

typedef struct {
    BLT_PcmMediaType media_type;
    BLT_Cardinal     packet_count;
    ATX_Int64        sample_count;
} VorbisDecoderOutputPort;

typedef struct {
    BLT_BaseMediaNode       base;
    VorbisDecoderInputPort  input;
    VorbisDecoderOutputPort output;
} VorbisDecoder;

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_VORBIS_DECODER_PACKET_SIZE 4096

/*----------------------------------------------------------------------
|       VorbisDecoder_ReadCallback
+---------------------------------------------------------------------*/
static size_t
VorbisDecoder_ReadCallback(void *ptr, size_t size, size_t nbelem, void *datasource)
{
    VorbisDecoder* decoder = (VorbisDecoder*)datasource;
    BLT_Size       bytes_to_read;
    BLT_Size       bytes_read;
    BLT_Result     result;

    bytes_to_read = (BLT_Size)size*nbelem;
    result = ATX_InputStream_Read(&decoder->input.stream, ptr, bytes_to_read, &bytes_read);
    if (BLT_SUCCEEDED(result)) {
        return bytes_read;
    } else if (result == BLT_ERROR_EOS) {
        return 0;
    } else {
        return -1;
    }
}

/*----------------------------------------------------------------------
|       VorbisDecoder_SeekCallback
+---------------------------------------------------------------------*/
static int
VorbisDecoder_SeekCallback(void *datasource, ogg_int64_t offset, int whence)
{
    VorbisDecoder* decoder = (VorbisDecoder *)datasource;
    BLT_Offset     where;
    BLT_Result     result;

    /* compute where to seek */
    if (whence == SEEK_CUR) {
        BLT_Offset current;
        ATX_InputStream_Tell(&decoder->input.stream, &current);
        if (current+offset <= decoder->input.size) {
            where = current+(long)offset;
        } else {
            where = decoder->input.size;
        }
    } else if (whence == SEEK_END) {
        if (offset <= decoder->input.size) {
            where = decoder->input.size - (long)offset;
        } else {
            where = 0;
        }
    } else if (whence == SEEK_SET) {
        where = (long)offset;
    } else {
        return -1;
    }

    /* clear the eos flag */
    decoder->input.eos = BLT_FALSE;

    /* perform the seek */
    result = ATX_InputStream_Seek(&decoder->input.stream, where);
    if (BLT_FAILED(result)) {
        return -1;
    } else {
        return 0;
    }
}

/*----------------------------------------------------------------------
|       VorbisDecoder_CloseCallback
+---------------------------------------------------------------------*/
static int
VorbisDecoder_CloseCallback(void *datasource)
{
    /* ignore */
    BLT_COMPILER_UNUSED(datasource);
    return 0;
}

/*----------------------------------------------------------------------
|       VorbisDecoder_TellCallback
+---------------------------------------------------------------------*/
static long
VorbisDecoder_TellCallback(void *datasource)
{
    VorbisDecoder *decoder = (VorbisDecoder *)datasource;
    BLT_Offset     offset;
    BLT_Result     result;

    result = ATX_InputStream_Tell(&decoder->input.stream, &offset);
    if (BLT_SUCCEEDED(result)) {
        return offset;
    } else {
        return 0;
    }
}

/*----------------------------------------------------------------------
|       VorbisDecoder_OpenStream
+---------------------------------------------------------------------*/
BLT_METHOD
VorbisDecoder_OpenStream(VorbisDecoder* decoder)
{
    ov_callbacks    callbacks;
    vorbis_info*    info;
    vorbis_comment* comment;
    int             result;
    
    /* check that we have a stream */
    if (ATX_OBJECT_IS_NULL(&decoder->input.stream)) {
        return BLT_FAILURE;
    }

    /* clear the eos flag */
    decoder->input.eos = BLT_FALSE;

    /* get input stream size */
    ATX_InputStream_GetSize(&decoder->input.stream, &decoder->input.size);

    /* setup callbacks */
    callbacks.read_func  = VorbisDecoder_ReadCallback;
    callbacks.seek_func  = VorbisDecoder_SeekCallback;
    callbacks.close_func = VorbisDecoder_CloseCallback;
    callbacks.tell_func  = VorbisDecoder_TellCallback;

    /* initialize the vorbis file structure */
    result = ov_open_callbacks(decoder, 
                               &decoder->input.vorbis_file, 
                               NULL, 
                               0, 
                               callbacks);
    if (result < 0) {
        decoder->input.vorbis_file.dataoffsets = NULL;
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* get info about the stream */
    info = ov_info(&decoder->input.vorbis_file, -1);
    if (info == NULL) return BLT_ERROR_INVALID_MEDIA_FORMAT;
    decoder->output.media_type.sample_rate     = info->rate;
    decoder->output.media_type.channel_count   = info->channels;
    decoder->output.media_type.bits_per_sample = 16;
    decoder->output.media_type.sample_format   = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;

    /* update the stream info */
    if (!ATX_OBJECT_IS_NULL(&decoder->base.context)) {
        BLT_StreamInfo stream_info;

        /* start with no info */
        stream_info.mask = 0;

        /* sample rate */
        stream_info.sample_rate = info->rate;
        stream_info.mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;

        /* channel count */
        stream_info.channel_count = info->channels;
        stream_info.mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;

        /* data type */
        stream_info.data_type = "Vorbis";
        stream_info.mask |= BLT_STREAM_INFO_MASK_DATA_TYPE;

        /* nominal bitrate */
        stream_info.nominal_bitrate = info->bitrate_nominal;
        stream_info.mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;

        /* average bitrate */
        {
            long bitrate = ov_bitrate(&decoder->input.vorbis_file, -1);
            if (bitrate > 0) {
                stream_info.average_bitrate = bitrate;
            } else {
                stream_info.average_bitrate = info->bitrate_nominal;
            }
            stream_info.mask |= BLT_STREAM_INFO_MASK_AVERAGE_BITRATE;
        }
        
        /* instant bitrate (not computed for now) */
        stream_info.instant_bitrate = 0;

        /* duration */
        if (info->rate) {
            stream_info.duration = 
                (long)(1000.0f*
                       (float)ov_pcm_total(&decoder->input.vorbis_file,-1)/
                       (float)info->rate);
            stream_info.mask |= BLT_STREAM_INFO_MASK_DURATION;
        } else {
            stream_info.duration = 0;
        }   

        BLT_Stream_SetInfo(&decoder->base.context, &stream_info);
    }

    /* process the comments */
    comment = ov_comment(&decoder->input.vorbis_file, -1);
    if (comment) {
        int i;
        ATX_String   string = ATX_EMPTY_STRING;
        ATX_String   key    = ATX_EMPTY_STRING;
        ATX_String   value  = ATX_EMPTY_STRING;
        float        track_gain = 0.0f;
        float        album_gain = 0.0f;
        ATX_Boolean  track_gain_set = ATX_FALSE;
        ATX_Boolean  album_gain_set = ATX_FALSE;

        for (i=0; i<comment->comments; i++) {
            int sep;
            ATX_String_AssignN(&string, 
                               comment->user_comments[i],
                               comment->comment_lengths[i]);
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

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       VorbisDecoderInputPort_SetStream
+---------------------------------------------------------------------*/
static BLT_Result
VorbisDecoderInputPort_SetStream(BLT_InputStreamUserInstance* instance, 
                                 ATX_InputStream*             stream,
                                 const BLT_MediaType*         media_type)
{
    VorbisDecoder* decoder = (VorbisDecoder*)instance;
    BLT_Result     result;

    /* check the stream's media type */
    if (media_type == NULL || 
        media_type->id != decoder->input.media_type_id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(&decoder->input.stream);

    /* reset counters and flags */
    decoder->input.size = 0;
    decoder->input.eos  = BLT_FALSE;
    decoder->output.packet_count = 0;
    ATX_Int64_Set_Int32(decoder->output.sample_count, 0);

    /* open the stream */
    decoder->input.stream = *stream;
    result = VorbisDecoder_OpenStream(decoder);
    if (BLT_FAILED(result)) {
        ATX_CLEAR_OBJECT(&decoder->input.stream);
        BLT_Debug("VorbisDecoderInputPort::SetStream - failed\n");
        return result;
    }

    /* keep a reference to the stream */
    ATX_REFERENCE_OBJECT(stream);

    /* get stream size */
    ATX_InputStream_GetSize(stream, &decoder->input.size);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(VorbisDecoderInputPort,
                                         "input",
                                         STREAM_PULL,
                                         IN)
static const BLT_MediaPortInterface
VorbisDecoderInputPort_BLT_MediaPortInterface = {
    VorbisDecoderInputPort_GetInterface,
    VorbisDecoderInputPort_GetName,
    VorbisDecoderInputPort_GetProtocol,
    VorbisDecoderInputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamUserInterface
VorbisDecoderInputPort_BLT_InputStreamUserInterface = {
    VorbisDecoderInputPort_GetInterface,
    VorbisDecoderInputPort_SetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderInputPort)
ATX_INTERFACE_MAP_ADD(VorbisDecoderInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(VorbisDecoderInputPort, BLT_InputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderInputPort)

/*----------------------------------------------------------------------
|    VorbisDecoderOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
VorbisDecoderOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                                  BLT_MediaPacket**           packet)
{
    VorbisDecoder* decoder = (VorbisDecoder*)instance;
    BLT_Any     buffer;
    int         current_section;
    long        bytes_read;
    BLT_Result  result;
    
    /* check for EOS */
    if (decoder->input.eos) {
        *packet = NULL;
        return BLT_ERROR_EOS;
    }

    /* get a packet from the core */
    result = BLT_Core_CreateMediaPacket(&decoder->base.core,
                                        BLT_VORBIS_DECODER_PACKET_SIZE,
                                        (BLT_MediaType*)&decoder->output.media_type,
                                        packet);
    if (BLT_FAILED(result)) return result;

    /* get the addr of the buffer */
    buffer = BLT_MediaPacket_GetPayloadBuffer(*packet);

    /* decode some audio samples */
    do {
        bytes_read = ov_read(&decoder->input.vorbis_file,
                             buffer,
                             BLT_VORBIS_DECODER_PACKET_SIZE,
                             0, 2, 1, &current_section);
    } while (bytes_read == OV_HOLE);
    if (bytes_read == 0) {
        decoder->input.eos = BLT_TRUE;
        BLT_MediaPacket_SetFlags(*packet, 
                                 BLT_MEDIA_PACKET_FLAG_END_OF_STREAM);    
    } else if (bytes_read < 0) {
        *packet = NULL;
        BLT_MediaPacket_Release(*packet);
        return BLT_FAILURE;
    }   
    
    /* update the size of the packet */
    BLT_MediaPacket_SetPayloadSize(*packet, bytes_read);

    /* set flags */     
    if (decoder->output.packet_count == 0) {
        /* this is the first packet */
        BLT_MediaPacket_SetFlags(*packet,
                                 BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
    }

    /* update the sample count and timestamp */
    if (decoder->output.media_type.channel_count   != 0 && 
        decoder->output.media_type.bits_per_sample != 0 &&
        decoder->output.media_type.sample_rate     != 0) {
        BLT_UInt32 sample_count;

            /* compute time stamp */
        BLT_TimeStamp time_stamp;
        BLT_TimeStamp_FromSamples(&time_stamp, 
                                  decoder->output.sample_count,
                                  decoder->output.media_type.sample_rate);
        BLT_MediaPacket_SetTimeStamp(*packet, time_stamp);

        /* update sample count */
        sample_count = bytes_read/(decoder->output.media_type.channel_count*
                                   decoder->output.media_type.bits_per_sample/8);
        ATX_Int64_Add_Int32(decoder->output.sample_count, sample_count);
    } 

    /* update the packet count */
    decoder->output.packet_count++;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(VorbisDecoderOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
VorbisDecoderOutputPort_BLT_MediaPortInterface = {
    VorbisDecoderOutputPort_GetInterface,
    VorbisDecoderOutputPort_GetName,
    VorbisDecoderOutputPort_GetProtocol,
    VorbisDecoderOutputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
VorbisDecoderOutputPort_BLT_PacketProducerInterface = {
    VorbisDecoderOutputPort_GetInterface,
    VorbisDecoderOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderOutputPort)
ATX_INTERFACE_MAP_ADD(VorbisDecoderOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(VorbisDecoderOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderOutputPort)

/*----------------------------------------------------------------------
|    VorbisDecoder_Create
+---------------------------------------------------------------------*/
static BLT_Result
VorbisDecoder_Create(BLT_Module*              module,
                     BLT_Core*                core, 
                     BLT_ModuleParametersType parameters_type,
                     BLT_CString              parameters, 
                     ATX_Object*              object)
{
    VorbisDecoder* decoder;

    BLT_Debug("VorbisDecoder::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    decoder = ATX_AllocateZeroMemory(sizeof(VorbisDecoder));
    if (decoder == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&decoder->base, module, core);

    /* construct the object */
    decoder->input.media_type_id = 
        ((VorbisDecoderModule*)ATX_INSTANCE(module))->ogg_type_id;
    BLT_PcmMediaType_Init(&decoder->output.media_type);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)decoder;
    ATX_INTERFACE(object) = (ATX_Interface*)&VorbisDecoder_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    VorbisDecoder_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
VorbisDecoder_Destroy(VorbisDecoder* decoder)
{
    BLT_Debug("VorbisDecoder::Destroy\n");

    /* free the vorbis decoder */
    if (!ATX_OBJECT_IS_NULL(&decoder->input.stream)) {
        ov_clear(&decoder->input.vorbis_file);
    }

    /* release the input stream */
    ATX_RELEASE_OBJECT(&decoder->input.stream);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&decoder->base);

    /* free the object memory */
    ATX_FreeMemory(decoder);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|       VorbisDecoder_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
VorbisDecoder_GetPortByName(BLT_MediaNodeInstance* instance,
                            BLT_CString            name,
                            BLT_MediaPort*         port)
{
    VorbisDecoder* decoder = (VorbisDecoder*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &VorbisDecoderInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)decoder;
        ATX_INTERFACE(port) = &VorbisDecoderOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    VorbisDecoder_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
VorbisDecoder_Seek(BLT_MediaNodeInstance* instance,
                   BLT_SeekMode*          mode,
                   BLT_SeekPoint*         point)
{
    VorbisDecoder* decoder = (VorbisDecoder*)instance;
    double         time;
    int            ov_result;

    /* estimate the seek point in time_stamp mode */
    if (ATX_OBJECT_IS_NULL(&decoder->base.context)) return BLT_FAILURE;
    BLT_Stream_EstimateSeekPoint(&decoder->base.context, *mode, point);
    if (!(point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) ||
        !(point->mask & BLT_SEEK_POINT_MASK_SAMPLE)) {
        return BLT_FAILURE;
    }

    /* update the output sample count */
    decoder->output.sample_count = point->sample;

    /* seek to the target time */
    time = 
        (double)point->time_stamp.seconds +
        (double)point->time_stamp.nanoseconds/1000000000.0f;
    BLT_Debug("VorbisDecoder::Seek - sample = %f\n", time);
    ov_result = ov_time_seek(&decoder->input.vorbis_file, time);
    if (ov_result != 0) return BLT_FAILURE;

    /* set the mode so that the nodes down the chaine know the seek has */
    /* already been done on the stream                                  */
    *mode = BLT_SEEK_MODE_IGNORE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
VorbisDecoder_BLT_MediaNodeInterface = {
    VorbisDecoder_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    VorbisDecoder_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    VorbisDecoder_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(VorbisDecoder, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoder)
ATX_INTERFACE_MAP_ADD(VorbisDecoder, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(VorbisDecoder, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoder)

/*----------------------------------------------------------------------
|       VorbisDecoderModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
VorbisDecoderModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    VorbisDecoderModule* module = (VorbisDecoderModule*)instance;
    BLT_Registry      registry;
    BLT_Result        result;

    /* get the registry */
    result = BLT_Core_GetRegistry(core, &registry);
    if (BLT_FAILED(result)) return result;

    /* register the ".ogg" file extension */
    result = BLT_Registry_RegisterExtension(&registry, 
                                            ".ogg",
                                            "application/x-ogg");
    if (BLT_FAILED(result)) return result;

    /* register the "application/x-ogg" type */
    result = BLT_Registry_RegisterName(
        &registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "application/x-ogg",
        &module->ogg_type_id);
    if (BLT_FAILED(result)) return result;
    
    BLT_Debug("VorbisDecoderModule::Attach (application/ogg type = %d)\n", 
              module->ogg_type_id);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       VorbisDecoderModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
VorbisDecoderModule_Probe(BLT_ModuleInstance*      instance, 
                          BLT_Core*                core,
                          BLT_ModuleParametersType parameters_type,
                          BLT_AnyConst             parameters,
                          BLT_Cardinal*            match)
{
    VorbisDecoderModule* module = (VorbisDecoderModule*)instance;
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

            /* the input type should be audio/x-ogg */
            if (constructor->spec.input.media_type->id != 
                module->ogg_type_id) {
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
                if (ATX_StringsEqual(constructor->name, "VorbisDecoder")) {
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

            BLT_Debug("VorbisDecoderModule::Probe - Ok [%d]\n", *match); 
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(VorbisDecoder)
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(VorbisDecoder, "Vorbis Decoder", 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface VorbisDecoderModule_BLT_ModuleInterface = {
    VorbisDecoderModule_GetInterface,
    BLT_BaseModule_GetInfo,
    VorbisDecoderModule_Attach,
    VorbisDecoderModule_CreateInstance,
    VorbisDecoderModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define VorbisDecoderModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(VorbisDecoderModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderModule) 
ATX_INTERFACE_MAP_ADD(VorbisDecoderModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(VorbisDecoderModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(VorbisDecoderModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_VorbisDecoderModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return VorbisDecoderModule_Create(object);
}
