/*****************************************************************
|
|   BlueTune - Decoder (experimental video support)
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "BltDecoderX.h"
#include "BltCore.h"
#include "BltStream.h"
#include "BltBuiltins.h"
#include "BltCorePriv.h"
#include "BltByteStreamUser.h"
#include "BltByteStreamProvider.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.decoder")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
struct BLT_DecoderX {
    BLT_Core*         core;
    BLT_OutputNode*   audio_output;
    BLT_Stream*       audio_stream;
    BLT_OutputNode*   video_output;
    BLT_Stream*       video_stream;
    BLT_DecoderStatus status;
};

/*----------------------------------------------------------------------
|    BLT_DecoderX_Create
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_Create(BLT_DecoderX** decoder)
{
    BLT_Result result;

    ATX_LOG_FINE("BLT_DecoderX::Create");

    /* allocate a new decoder object */
    *decoder = (BLT_DecoderX*)ATX_AllocateZeroMemory(sizeof(BLT_DecoderX));
    if (*decoder == NULL) {
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* get the core object */
    result = BLT_Core_Create(&(*decoder)->core);
    if (BLT_FAILED(result)) goto failed;

    /* create streams */
    result = BLT_Core_CreateStream((*decoder)->core, &(*decoder)->audio_stream);
    if (BLT_FAILED(result)) goto failed;
    result = BLT_Core_CreateStream((*decoder)->core, &(*decoder)->video_stream);
    if (BLT_FAILED(result)) goto failed;

    /* done */
    return BLT_SUCCESS;

 failed:
    BLT_DecoderX_Destroy(*decoder);
    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_Destroy
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_Destroy(BLT_DecoderX* decoder)
{
    ATX_LOG_FINE("BLT_DecoderX::Destroy");
    
    ATX_RELEASE_OBJECT(decoder->audio_stream);
    ATX_RELEASE_OBJECT(decoder->video_stream);
    if (decoder->core) BLT_Core_Destroy(decoder->core);

    ATX_FreeMemory(decoder);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_RegisterBuiltins
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_RegisterBuiltins(BLT_DecoderX* decoder)
{
    return BLT_Builtins_RegisterModules(decoder->core);
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_RegisterModule
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_RegisterModule(BLT_DecoderX* decoder, BLT_Module* module)
{
    return BLT_Core_RegisterModule(decoder->core, module);
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_ClearStatus
+---------------------------------------------------------------------*/
static BLT_Result
BLT_DecoderX_ClearStatus(BLT_DecoderX* decoder) 
{
    ATX_SetMemory(&decoder->status, 0, sizeof(decoder->status));
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_UpdateStatus
+---------------------------------------------------------------------*/
static BLT_Result
BLT_DecoderX_UpdateStatus(BLT_DecoderX* decoder) 
{
    BLT_StreamStatus status;
    BLT_Result       result;

    result = BLT_Stream_GetStatus(decoder->audio_stream, &status);
    if (BLT_SUCCEEDED(result)) {
        decoder->status.time_stamp = status.time_stamp;
        decoder->status.position   = status.position;
    } else {
        decoder->status.time_stamp.seconds     = 0;
        decoder->status.time_stamp.nanoseconds = 0;
        decoder->status.position.offset        = 0;
        decoder->status.position.range         = 0;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_GetProperties(BLT_DecoderX* decoder, ATX_Properties** properties) 
{
    return BLT_Core_GetProperties(decoder->core, properties);
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetStatus
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_GetStatus(BLT_DecoderX* decoder, BLT_DecoderStatus* status) 
{
    /* update the status cache */
    BLT_DecoderX_UpdateStatus(decoder);

    /* return the cached info */
    *status = decoder->status;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetStreamProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_GetStreamProperties(BLT_DecoderX*     decoder, 
                                 ATX_Properties** properties) 
{
    return BLT_Stream_GetProperties(decoder->audio_stream, properties);
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetEventListener
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_SetEventListener(BLT_DecoderX*       decoder, 
                              BLT_EventListener* listener)
{
    /* set the listener of the stream */
    BLT_Stream_SetEventListener(decoder->audio_stream, listener);
    BLT_Stream_SetEventListener(decoder->video_stream, listener);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_CreateInputNode
+---------------------------------------------------------------------*/
static BLT_Result
BLT_DecoderX_CreateInputNode(BLT_DecoderX*    self, 
                             BLT_CString     name,
                             BLT_Stream*     stream,
                             BLT_MediaNode** mp4_parser_node)
{
    BLT_MediaType            input_media_type;
    BLT_MediaType            output_media_type;
    BLT_MediaNodeConstructor constructor;
    BLT_MediaNode*           file_input_node = NULL;
    BLT_MediaPort*           input_port = NULL;
    BLT_MediaPort*           output_port = NULL;
    BLT_InputStreamProvider* stream_provider = NULL;
    BLT_InputStreamUser*     stream_user = NULL;
    ATX_InputStream*         file_stream = NULL;
    const BLT_MediaType*     media_type = NULL;
    BLT_Result               result;

    /* default return */
    *mp4_parser_node = NULL;
    
    /* create the file input */
    constructor.spec.input.protocol  = BLT_MEDIA_PORT_PROTOCOL_NONE;
    constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_ANY;
    constructor.name                 = name;
    BLT_MediaType_Init(&input_media_type,  BLT_MEDIA_TYPE_ID_NONE);
    BLT_MediaType_Init(&output_media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
    constructor.spec.output.media_type = &output_media_type;
    constructor.spec.input.media_type  = &input_media_type;
    result = BLT_Core_CreateCompatibleMediaNode(self->core, &constructor, &file_input_node);
    if (BLT_FAILED(result)) return result;

    /* get the video/mp4 type id */
    {
        BLT_Registry* registry;
        BLT_UInt32    id;

        result = BLT_Core_GetRegistry(self->core, &registry);
        if (BLT_FAILED(result)) return result;
        
        result = BLT_Registry_GetIdForName(
           registry, 
           BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS, 
           "video/mp4", 
            &id);
        if (BLT_FAILED(result)) return result;
        BLT_MediaType_Init(&input_media_type,  id);
    }
    
    /* create the MP4 parser */
    constructor.spec.input.protocol  = BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL;
    constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_PACKET;
    constructor.name                 = NULL;
    BLT_MediaType_Init(&output_media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
    constructor.spec.output.media_type = &output_media_type;
    constructor.spec.input.media_type  = &input_media_type;
    result = BLT_Core_CreateCompatibleMediaNode(self->core, &constructor, mp4_parser_node);
    if (BLT_FAILED(result)) return result;
    
    /* setup the contexts */
    BLT_MediaNode_Activate(file_input_node, stream);
    BLT_MediaNode_Activate(*mp4_parser_node, stream);
    
    /* connect the nodes */
    BLT_MediaNode_GetPortByName(file_input_node, "output", &output_port);
    BLT_MediaNode_GetPortByName(*mp4_parser_node, "input",  &input_port);
    BLT_MediaPort_QueryMediaType(input_port, 0, &media_type);
    stream_provider = ATX_CAST(output_port, BLT_InputStreamProvider);
    stream_user = ATX_CAST(input_port, BLT_InputStreamUser);
    BLT_InputStreamProvider_GetStream(stream_provider, &file_stream);
    BLT_InputStreamUser_SetStream(stream_user, file_stream, media_type);
    ATX_RELEASE_OBJECT(file_stream);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetInput
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_SetInput(BLT_DecoderX* decoder, BLT_CString name, BLT_CString type)
{
    BLT_Result     result = BLT_SUCCESS;
    BLT_MediaNode* node = NULL;
    
    ATX_COMPILER_UNUSED(type);
    
    /* clear the status */
    BLT_DecoderX_ClearStatus(decoder);

    if (name == NULL || name[0] == '\0') {
        /* if the name is NULL or empty, it means reset */
        result = BLT_Stream_ResetInput(decoder->audio_stream);
        if (BLT_FAILED(result)) return result;
        result = BLT_Stream_ResetInput(decoder->video_stream);
        if (BLT_FAILED(result)) return result;
    } else {
        /* create the node */
        result = BLT_DecoderX_CreateInputNode(decoder, name, decoder->audio_stream, &node);
        if (BLT_FAILED(result)) return result;
        
        /* set the input of the streams */
        result = BLT_Stream_SetInputNode(decoder->audio_stream, name, "audio", node);
        if (BLT_FAILED(result)) goto end;
        
        result = BLT_Stream_SetInputNode(decoder->video_stream, name, "video", node);
        if (BLT_FAILED(result)) goto end;
    }
    
end:
    ATX_RELEASE_OBJECT(node);
    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetAudioOutput
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_SetAudioOutput(BLT_DecoderX* decoder, BLT_CString name, BLT_CString type)
{
    BLT_Result result;
    
    /* normalize the name and type */
    if (name && name[0] == '\0') name = NULL;
    if (type && type[0] == '\0') type = NULL;

    if (name == NULL) {
        /* if the name is NULL or empty, it means reset */
        decoder->audio_output = NULL;
        return BLT_Stream_ResetOutput(decoder->audio_stream);
    } else {
        if (ATX_StringsEqual(name, BLT_DECODER_DEFAULT_OUTPUT_NAME)) {
	        /* if the name is BLT_DECODER_DEFAULT_OUTPUT_NAME, use default */ 
            BLT_CString default_name;
            BLT_CString default_type;
            BLT_Builtins_GetDefaultAudioOutput(&default_name, &default_type);
            name = default_name;
            if (type == NULL) type = default_type;

  	        result =  BLT_Stream_SetOutput(decoder->audio_stream, name, type);
	    } else {
            /* set the output of the stream by name */
            result = BLT_Stream_SetOutput(decoder->audio_stream, name, type);
	    }

        if (BLT_SUCCEEDED(result)) {
            BLT_MediaNode* node = NULL;
            BLT_Stream_GetOutputNode(decoder->audio_stream, &node);
            if (node) {
                decoder->audio_output = ATX_CAST(node, BLT_OutputNode);
            }
        }
    }

    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetAudioOutput
+---------------------------------------------------------------------*/
BLT_Result BLT_DecoderX_GetAudioOutput(BLT_DecoderX*   decoder, 
                                       BLT_MediaNode** output)
{
    if (decoder->audio_output) {
        *output = ATX_CAST(decoder->audio_output, BLT_MediaNode);
    } else {
        *output = NULL;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetVideoOutput
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_SetVideoOutput(BLT_DecoderX* decoder, BLT_CString name, BLT_CString type)
{
    BLT_Result result;
    
    /* normalize the name and type */
    if (name && name[0] == '\0') name = NULL;
    if (type && type[0] == '\0') type = NULL;

    if (name == NULL) {
        /* if the name is NULL or empty, it means reset */
        decoder->video_output = NULL;
        return BLT_Stream_ResetOutput(decoder->video_stream);
    } else {
        if (ATX_StringsEqual(name, BLT_DECODER_DEFAULT_OUTPUT_NAME)) {
	        /* if the name is BLT_DECODER_DEFAULT_OUTPUT_NAME, use default */ 
            BLT_CString default_name;
            BLT_CString default_type;
            BLT_Builtins_GetDefaultVideoOutput(&default_name, &default_type);
            name = default_name;
            if (type == NULL) type = default_type;

  	        result =  BLT_Stream_SetOutput(decoder->video_stream, name, type);
	    } else {
            /* set the output of the stream by name */
            result = BLT_Stream_SetOutput(decoder->video_stream, name, type);
	    }

        if (BLT_SUCCEEDED(result)) {
            BLT_MediaNode* node = NULL;
            BLT_Stream_GetOutputNode(decoder->video_stream, &node);
            if (node) {
                decoder->video_output = ATX_CAST(node, BLT_OutputNode);
            }
        }
    }

    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetVideoOutput
+---------------------------------------------------------------------*/
BLT_Result BLT_DecoderX_GetVideoOutput(BLT_DecoderX*   decoder, 
                                       BLT_MediaNode** output)
{
    if (decoder->video_output) {
        *output = ATX_CAST(decoder->video_output, BLT_MediaNode);
    } else {
        *output = NULL;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_AddNodeByName
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_AddNodeByName(BLT_DecoderX*   decoder, 
                           BLT_MediaNode* where, 
                           BLT_CString    name)
{
    /* not implemented this in this version */
    ATX_COMPILER_UNUSED(decoder);
    ATX_COMPILER_UNUSED(where);
    ATX_COMPILER_UNUSED(name);
    
    return BLT_ERROR_NOT_IMPLEMENTED;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_PumpPacket
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_PumpPacket(BLT_DecoderX* decoder)
{
    BLT_Boolean audio_would_block = BLT_FALSE;
	BLT_Boolean audio_eos         = BLT_FALSE;
    BLT_Boolean video_would_block = BLT_FALSE;
	BLT_Boolean video_eos         = BLT_FALSE;
    BLT_Result  result;
    
    /* pump an audio packet */
    if (decoder->audio_output) {
        BLT_OutputNodeStatus status;
        BLT_OutputNode_GetStatus(decoder->audio_output, &status);
        if (status.flags & BLT_OUTPUT_NODE_STATUS_QUEUE_FULL) {
            audio_would_block = BLT_TRUE;
        }
		if (!audio_would_block) {
			result = BLT_Stream_PumpPacket(decoder->audio_stream);
			if (BLT_FAILED(result)) {
				if (result == BLT_ERROR_EOS) {
					audio_eos = BLT_TRUE;
				} else {
					return result;
				}
			}
		}
    }
    
    /* pump a video packet */
    if (decoder->video_output) {
        BLT_OutputNodeStatus status;
        BLT_OutputNode_GetStatus(decoder->video_output, &status);
        if (status.flags & BLT_OUTPUT_NODE_STATUS_QUEUE_FULL) {
            video_would_block = BLT_TRUE;
        }

		if (!video_would_block) {
			result = BLT_Stream_PumpPacket(decoder->video_stream);
			if (BLT_FAILED(result)) {
				if (result == BLT_ERROR_EOS) {
					video_eos = BLT_TRUE;
				} else {
					return result;
				}
			}
		}
    }
    
	/* check for the end of both streams */
	if (audio_eos && video_eos) return BLT_ERROR_EOS;

    /* if both would block, sleep a bit */
    if (audio_would_block && video_would_block) {
        ATX_TimeInterval sleep_duration = {0,10000000}; /* 10ms */
        ATX_System_Sleep(&sleep_duration);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_Stop
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_Stop(BLT_DecoderX* decoder)
{
    /* stop the stream */
    BLT_Stream_Stop(decoder->audio_stream);
    BLT_Stream_Stop(decoder->video_stream);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_Pause
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_Pause(BLT_DecoderX* decoder)
{
    /* pause the stream */
    BLT_Stream_Pause(decoder->audio_stream);
    BLT_Stream_Pause(decoder->video_stream);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SeekToTime
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_SeekToTime(BLT_DecoderX* decoder, BLT_UInt32 time)
{
    BLT_Stream_SeekToTime(decoder->audio_stream, time);
    BLT_Stream_SeekToTime(decoder->video_stream, time);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SeekToPosition
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_SeekToPosition(BLT_DecoderX* decoder,
                            BLT_Size     offset,
                            BLT_Size     range)
{
    BLT_Stream_SeekToPosition(decoder->audio_stream, offset, range);
    BLT_Stream_SeekToPosition(decoder->video_stream, offset, range);
    
    return BLT_SUCCESS;
}


