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
#include "BltSynchronization.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.decoder")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
struct BLT_DecoderX {
    ATX_IMPLEMENTS(BLT_TimeSource);
    
    BLT_Core*         core;
    BLT_Stream*       input_stream;
    BLT_OutputNode*   audio_output;
    BLT_Stream*       audio_stream;
    BLT_OutputNode*   video_output;
    BLT_Stream*       video_stream;
    BLT_DecoderStatus status;
};

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BLT_DecoderX, BLT_TimeSource)

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
    result = BLT_Core_CreateStream((*decoder)->core, &(*decoder)->input_stream);
    if (BLT_FAILED(result)) goto failed;
    result = BLT_Core_CreateStream((*decoder)->core, &(*decoder)->audio_stream);
    if (BLT_FAILED(result)) goto failed;
    result = BLT_Core_CreateStream((*decoder)->core, &(*decoder)->video_stream);
    if (BLT_FAILED(result)) goto failed;

    /* setup interfaces */
    ATX_SET_INTERFACE(*decoder, BLT_DecoderX, BLT_TimeSource);
    
    /* done */
    return BLT_SUCCESS;

 failed:
    BLT_DecoderX_Destroy(*decoder);
    *decoder = NULL;
    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_Destroy
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_Destroy(BLT_DecoderX* decoder)
{
    ATX_LOG_FINE("BLT_DecoderX::Destroy");
    
    ATX_RELEASE_OBJECT(decoder->audio_output);
    ATX_RELEASE_OBJECT(decoder->video_output);
    ATX_RELEASE_OBJECT(decoder->input_stream);
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
    BLT_StreamStatus input_status;
    BLT_StreamInfo   input_info;
    BLT_StreamStatus audio_status;
    BLT_StreamInfo   audio_info;
    BLT_StreamStatus video_status;
    BLT_StreamInfo   video_info;

    BLT_Stream_GetStatus(decoder->input_stream, &input_status);
    BLT_Stream_GetStatus(decoder->audio_stream, &audio_status);
    BLT_Stream_GetStatus(decoder->video_stream, &video_status);
    BLT_Stream_GetInfo(decoder->input_stream, &input_info);
    BLT_Stream_GetInfo(decoder->audio_stream, &audio_info);
    BLT_Stream_GetInfo(decoder->video_stream, &video_info);

    /* return a composite status */
    status->stream_info = input_info;
    /*status->time_stamp  = input_status.time_stamp;*/
    BLT_TimeStamp_Set(status->time_stamp, 0, 0);
    if (status->time_stamp.seconds == 0 && status->time_stamp.nanoseconds == 0) {
        status->time_stamp = audio_status.time_stamp;
    }
    if (status->time_stamp.seconds == 0 && status->time_stamp.nanoseconds == 0) {
        status->time_stamp = video_status.time_stamp;
    }
    if (input_info.duration) {
        /* estimate the position from the time stamp and duration */
        status->position.offset = BLT_TimeStamp_ToMillis(status->time_stamp);
        status->position.range  = status->stream_info.duration;
    } else {
        status->position.offset = 0;
        status->position.range  = 0;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetInputStreamProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_GetInputStreamProperties(BLT_DecoderX*     decoder, 
                                      ATX_Properties** properties) 
{
    return BLT_Stream_GetProperties(decoder->input_stream, properties);
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetAudioStreamProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_GetAudioStreamProperties(BLT_DecoderX*     decoder, 
                                      ATX_Properties** properties) 
{
    return BLT_Stream_GetProperties(decoder->audio_stream, properties);
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetVideoStreamProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_GetVideoStreamProperties(BLT_DecoderX*     decoder, 
                                      ATX_Properties** properties) 
{
    return BLT_Stream_GetProperties(decoder->video_stream, properties);
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
BLT_DecoderX_CreateInputNode(BLT_DecoderX*   self, 
                             BLT_CString     name,
                             BLT_CString     type,
                             BLT_MediaNode** input_node)
{
    BLT_Result result;
    
    /* default return value */
    *input_node = NULL;
    
    /* set the input of the input stream */
    result = BLT_Stream_SetInput(self->input_stream, name, type);
    if (BLT_FAILED(result)) return result;
    
    /* set the output of the input stream */
    result = BLT_Stream_SetOutput(self->input_stream, "com.bluetune.parsers.mp4", NULL);
    if (BLT_FAILED(result)) return result;
   
    /* the input stream's output is what we need to return here */
    return BLT_Stream_GetOutputNode(self->input_stream, input_node); 
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetInput
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_SetInput(BLT_DecoderX* decoder, BLT_CString name, BLT_CString type)
{
    BLT_Result     result = BLT_SUCCESS;
    BLT_MediaNode* node = NULL;
    
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
        result = BLT_DecoderX_CreateInputNode(decoder, name, type, &node);
        if (BLT_FAILED(result)) return result;
        
        /* activate the input stream */
        /* NOTE: this is a temporary hacky solution: we pump a few packets to ensure  */
        /* that all intermediate nodes get created and connected.                     */
        /* This works here, because we're not really pumping any packets, just making */
        /* connections from media node to media node.                                 */
        result = BLT_Stream_PumpPacket(decoder->input_stream);
        if (BLT_FAILED(result)) goto end;
        result = BLT_Stream_PumpPacket(decoder->input_stream);
        if (BLT_FAILED(result)) goto end;
        result = BLT_Stream_PumpPacket(decoder->input_stream);
        if (BLT_FAILED(result)) goto end;
        
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
|    BLT_DecoderX_AudioOutputChanged
+---------------------------------------------------------------------*/
static void
BLT_DecoderX_AudioOutputChanged(BLT_DecoderX* decoder, BLT_MediaNode* node)
{
    if (node) {
        decoder->audio_output = ATX_CAST(node, BLT_OutputNode);
    } else {
        decoder->audio_output = NULL;
    }
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
            BLT_DecoderX_AudioOutputChanged(decoder, node);
        }
    }

    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetAudioOutputNode
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_SetAudioOutputNode(BLT_DecoderX*  decoder, 
                                BLT_CString    name,
                                BLT_MediaNode* node)
{
    BLT_Result result;

    result = BLT_Stream_SetOutputNode(decoder->audio_stream, name, node);
    if (BLT_SUCCEEDED(result)) {
        BLT_DecoderX_AudioOutputChanged(decoder, node);
    }
    
    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetAudioOutputNode
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_GetAudioOutputNode(BLT_DecoderX*   decoder, 
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
|    BLT_DecoderX_VideoOutputChanged
+---------------------------------------------------------------------*/
static void
BLT_DecoderX_VideoOutputChanged(BLT_DecoderX* decoder, BLT_MediaNode* node)
{
    if (node) {
        BLT_SyncSlave* video_output_as_slave;
        decoder->video_output = ATX_CAST(node, BLT_OutputNode);
        
        /* setup the synchronization */
        video_output_as_slave = ATX_CAST(node, BLT_SyncSlave);
        if (video_output_as_slave) {
            BLT_SyncSlave_SetTimeSource(video_output_as_slave, 
                                        &ATX_BASE(decoder, BLT_TimeSource));
        }
    } else {
        decoder->video_output = NULL;
    }
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
            BLT_DecoderX_VideoOutputChanged(decoder, node);
        }
    }

    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SetVideoOutputNode
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderX_SetVideoOutputNode(BLT_DecoderX*  decoder, 
                                BLT_CString    name, 
                                BLT_MediaNode* node)
{
    BLT_Result result;

    result = BLT_Stream_SetOutputNode(decoder->video_stream, name, node);
    if (BLT_SUCCEEDED(result)) {
        BLT_DecoderX_VideoOutputChanged(decoder, node);
    }

    return result;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetVideoOutputNode
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_GetVideoOutputNode(BLT_DecoderX*   decoder, 
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
    
    /* ensure that the input is started */
    BLT_Stream_Start(decoder->input_stream);
    
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
    BLT_Stream_Stop(decoder->input_stream);
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
    BLT_Stream_Pause(decoder->input_stream);
    BLT_Stream_Pause(decoder->audio_stream);
    BLT_Stream_Pause(decoder->video_stream);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SeekToTime
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_SeekToTime(BLT_DecoderX* decoder, BLT_UInt64 time)
{
    BLT_Stream_SeekToTime(decoder->audio_stream, time);
    BLT_Stream_SeekToTime(decoder->video_stream, time);
    BLT_Stream_SeekToTime(decoder->input_stream, time);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_SeekToPosition
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderX_SeekToPosition(BLT_DecoderX* decoder,
                            BLT_LargeSize offset,
                            BLT_LargeSize range)
{
    BLT_Stream_SeekToPosition(decoder->audio_stream, offset, range);
    BLT_Stream_SeekToPosition(decoder->video_stream, offset, range);
    BLT_Stream_SeekToPosition(decoder->input_stream, offset, range);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderX_GetMediaTime
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_DecoderX_GetMediaTime(BLT_TimeSource* _self, BLT_TimeStamp* media_time)
{
    BLT_DecoderX* self = ATX_SELF(BLT_DecoderX, BLT_TimeSource);
    
    BLT_TimeStamp_Set(*media_time, 0, 0);
    if (self->audio_output) {
        BLT_OutputNodeStatus status;
        if (BLT_SUCCEEDED(BLT_OutputNode_GetStatus(self->audio_output, &status))) {
            *media_time = status.media_time;
        }
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLT_DecoderX)
    ATX_GET_INTERFACE_ACCEPT(BLT_DecoderX, BLT_TimeSource)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaTime interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLT_DecoderX, BLT_TimeSource)
    BLT_DecoderX_GetMediaTime
ATX_END_INTERFACE_MAP

