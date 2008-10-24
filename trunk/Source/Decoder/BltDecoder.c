/*****************************************************************
|
|   BlueTune - Sync Layer
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "BltDecoder.h"
#include "BltCore.h"
#include "BltStream.h"
#include "BltBuiltins.h"
#include "BltCorePriv.h"
#include "BltDynamicPlugins.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.decoder")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
struct BLT_Decoder {
    BLT_Core*         core;
    BLT_Stream*       stream;
    BLT_DecoderStatus status;
};

/*----------------------------------------------------------------------
|    BLT_Decoder_Create
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Decoder_Create(BLT_Decoder** decoder)
{
    BLT_Result result;

    ATX_LOG_FINE("BLT_Decoder::Create");

    /* allocate a new decoder object */
    *decoder = (BLT_Decoder*)ATX_AllocateZeroMemory(sizeof(BLT_Decoder));
    if (*decoder == NULL) {
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* get the core object */
    result = BLT_Core_Create(&(*decoder)->core);
    if (BLT_FAILED(result)) goto failed;

    /* create a stream */
    result = BLT_Core_CreateStream((*decoder)->core, &(*decoder)->stream);
    if (BLT_FAILED(result)) goto failed;

    /* done */
    return BLT_SUCCESS;

 failed:
    BLT_Decoder_Destroy(*decoder);
    return result;
}

/*----------------------------------------------------------------------
|    BLT_Decoder_Destroy
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_Destroy(BLT_Decoder* decoder)
{
    ATX_LOG_FINE("BLT_Decoder::Destroy");
    
    ATX_RELEASE_OBJECT(decoder->stream);
    if (decoder->core) BLT_Core_Destroy(decoder->core);

    ATX_FreeMemory(decoder);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Decoder_RegisterBuiltins
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_RegisterBuiltins(BLT_Decoder* decoder)
{
    return BLT_Builtins_RegisterModules(decoder->core);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_RegisterModule
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Decoder_RegisterModule(BLT_Decoder* decoder, BLT_Module* module)
{
    return BLT_Core_RegisterModule(decoder->core, module);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_LoadPlugin
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Decoder_LoadPlugin(BLT_Decoder* decoder, 
                       const char*  name, 
                       BLT_Flags    search_flags)
{
    return BLT_Plugins_LoadModulesFromFile(decoder->core, 
                                           name, 
                                           search_flags);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_LoadPlugins
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Decoder_LoadPlugins(BLT_Decoder* decoder, 
                        const char*  directory,
                        const char*  file_extension)
{
    return BLT_Plugins_LoadModulesFromDirectory(decoder->core, 
                                                directory, 
                                                file_extension);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_ClearStatus
+---------------------------------------------------------------------*/
static BLT_Result
BLT_Decoder_ClearStatus(BLT_Decoder* decoder) 
{
    ATX_SetMemory(&decoder->status, 0, sizeof(decoder->status));
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Decoder_UpdateStatus
+---------------------------------------------------------------------*/
static BLT_Result
BLT_Decoder_UpdateStatus(BLT_Decoder* decoder) 
{
    BLT_StreamStatus status;
    BLT_Result       result;

    result = BLT_Stream_GetStatus(decoder->stream, &status);
    if (BLT_SUCCEEDED(result)) {
        decoder->status.time_stamp = status.time_stamp;
        decoder->status.position   = status.position;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Decoder_GetProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_GetProperties(BLT_Decoder* decoder, ATX_Properties** properties) 
{
    return BLT_Core_GetProperties(decoder->core, properties);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_GetStatus
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_GetStatus(BLT_Decoder* decoder, BLT_DecoderStatus* status) 
{
    /* update the status cache */
    BLT_Decoder_UpdateStatus(decoder);

    /* return the cached info */
    *status = decoder->status;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Decoder_GetStreamProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_GetStreamProperties(BLT_Decoder*     decoder, 
                                ATX_Properties** properties) 
{
    return BLT_Stream_GetProperties(decoder->stream, properties);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_SetEventListener
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_SetEventListener(BLT_Decoder*       decoder, 
                             BLT_EventListener* listener)
{
    /* set the listener of the stream */
    return BLT_Stream_SetEventListener(decoder->stream, listener);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_SetInput
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_SetInput(BLT_Decoder* decoder, BLT_CString name, BLT_CString type)
{
    /* clear the status */
    BLT_Decoder_ClearStatus(decoder);

    if (name == NULL || name[0] == '\0') {
        /* if the name is NULL or empty, it means reset */
        return BLT_Stream_ResetInput(decoder->stream);
    } else {
        /* set the input of the stream by name */
        return BLT_Stream_SetInput(decoder->stream, name, type);
    }
}

/*----------------------------------------------------------------------
|    BLT_Decoder_SetOutput
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_SetOutput(BLT_Decoder* decoder, BLT_CString name, BLT_CString type)
{
    /* normalize the name and type */
    if (name && name[0] == '\0') name = NULL;
    if (type && type[0] == '\0') type = NULL;

    if (name == NULL) {
        /* if the name is NULL or empty, it means reset */
        return BLT_Stream_ResetOutput(decoder->stream);
    } else {
        if (ATX_StringsEqual(name, BLT_DECODER_DEFAULT_OUTPUT_NAME)) {
	        /* if the name is BLT_DECODER_DEFAULT_OUTPUT_NAME, use default */ 
            BLT_CString default_name;
            BLT_CString default_type;
            BLT_Builtins_GetDefaultAudioOutput(&default_name, &default_type);
            name = default_name;
            if (type == NULL) type = default_type;

  	        return BLT_Stream_SetOutput(decoder->stream, name, type);
	    } else {
            /* set the output of the stream by name */
            return BLT_Stream_SetOutput(decoder->stream, name, type);
	    }
    }
}

/*----------------------------------------------------------------------
|    BLT_Decoder_AddNodeByName
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_AddNodeByName(BLT_Decoder*   decoder, 
                          BLT_MediaNode* where, 
                          BLT_CString    name)
{
    return BLT_Stream_AddNodeByName(decoder->stream, where, name);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_PumpPacket
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_PumpPacket(BLT_Decoder* decoder)
{
    BLT_Result result;

    /* pump a packet */
    result = BLT_Stream_PumpPacket(decoder->stream);
    if (BLT_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Decoder_Stop
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_Stop(BLT_Decoder* decoder)
{
    /* stop the stream */
    return BLT_Stream_Stop(decoder->stream);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_Pause
+---------------------------------------------------------------------*/
BLT_Result
BLT_Decoder_Pause(BLT_Decoder* decoder)
{
    /* pause the stream */
    return BLT_Stream_Pause(decoder->stream);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_SeekToTime
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Decoder_SeekToTime(BLT_Decoder* decoder, BLT_UInt64 time)
{
    return BLT_Stream_SeekToTime(decoder->stream, time);
}

/*----------------------------------------------------------------------
|    BLT_Decoder_SeekToPosition
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Decoder_SeekToPosition(BLT_Decoder* decoder,
                           BLT_UInt64   offset,
                           BLT_UInt64   range)
{
    return BLT_Stream_SeekToPosition(decoder->stream, offset, range);
}


