/*****************************************************************
|
|   BlueTune - OSX AudioFileStream Parser Module
|
|   (c) 2002-2010 Gilles Boccon-Gibod
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
#include "BltByteStreamUser.h"
#include "BltStream.h"
#include "BltCommonMediaTypes.h"

#include <AudioToolbox/AudioToolbox.h>

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.parsers.audio-file-stream")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_MediaType               base;
    AudioStreamBasicDescription asbd;
    unsigned int                magic_cookie_size;
    unsigned char               magic_cookie[1];
    // followed by zero or more magic_cookie bytes
} AsbdMediaType;

typedef struct {
    BLT_MediaTypeId asbd;
    BLT_MediaTypeId audio_mp4;
    BLT_MediaTypeId audio_mp3;
} AudioFileStreamParserMediaTypeIds;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);

    /* members */
    AudioFileStreamParserMediaTypeIds media_type_ids;
} AudioFileStreamParserModule;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_InputStreamUser);

    /* members */
    ATX_InputStream* stream;
    BLT_Boolean      eos;
} AudioFileStreamParserInput;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    AsbdMediaType* media_type;
    ATX_List*      packets;
} AudioFileStreamParserOutput;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* members */
    AudioFileStreamParserInput        input;
    AudioFileStreamParserOutput       output;
    AudioFileStreamID                 stream_parser;
    AudioFileStreamParserMediaTypeIds media_type_ids;
    UInt32*                           supported_formats;
    unsigned int                      supported_format_count;
} AudioFileStreamParser;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(AudioFileStreamParserModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(AudioFileStreamParser,       BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(AudioFileStreamParser,       ATX_Referenceable)

/*----------------------------------------------------------------------
|    AudioFileStreamParser_FormatIsSupported
+---------------------------------------------------------------------*/
static BLT_Boolean
AudioFileStreamParser_FormatIsSupported(AudioFileStreamParser* self, 
                                        UInt32                 format)
{
    unsigned int i;
    if (self->supported_formats == NULL) return BLT_TRUE; /* assume */
    for (i=0; i<self->supported_format_count; i++) {
        if (self->supported_formats[i] == format) return BLT_TRUE;
    }
    return BLT_FALSE;
}

/*----------------------------------------------------------------------
|    AudioFileStreamParser_OnProperty
+---------------------------------------------------------------------*/
static void 
AudioFileStreamParser_OnProperty(void*                     _self, 
                                 AudioFileStreamID         stream,
                                 AudioFileStreamPropertyID property_id,
                                 UInt32*                   flags)
{
    AudioFileStreamParser* self = (AudioFileStreamParser*)_self;
    UInt32                 property_size = 0;
    OSStatus               result;
    ATX_COMPILER_UNUSED(flags);
    
    switch (property_id) {
        case kAudioFileStreamProperty_FileFormat: {
            UInt32 property_value = 0;
            property_size = sizeof(property_value);
            if (AudioFileStreamGetProperty(stream, property_id, &property_size, &property_value) == noErr) {
                ATX_LOG_FINE_1("kAudioFileStreamProperty_FileFormat = %x", property_value);
            }
            break;
        }

        case kAudioFileStreamProperty_DataFormat: 
        case kAudioFileStreamProperty_FormatList:
        case kAudioFileStreamProperty_MagicCookieData:
            /* ask the parser to cache the property */
            *flags |= kAudioFileStreamPropertyFlag_CacheProperty;
            break;
            
        case kAudioFileStreamProperty_ReadyToProducePackets:
        {
            unsigned int media_type_size = sizeof(AsbdMediaType);
            
            /* get the magic cookie size */
            result = AudioFileStreamGetPropertyInfo(stream, kAudioFileStreamProperty_MagicCookieData, &property_size, NULL);
            if (result == noErr) {
                ATX_LOG_FINE_1("magic cookie size=%d", property_size);
            } else {
                ATX_LOG_FINE("no magic cookie");
                property_size = 0;
            }
            
            /* allocate the media type */
            if (property_size > 1) {
                media_type_size += property_size-1;
            }
            self->output.media_type = (AsbdMediaType*)ATX_AllocateZeroMemory(media_type_size);
            BLT_MediaType_InitEx(&self->output.media_type->base, self->media_type_ids.asbd, media_type_size);
            self->output.media_type->magic_cookie_size = property_size;
            
            /* copy the magic cookie if there is one */
            if (property_size) {
                result = AudioFileStreamGetProperty(stream, kAudioFileStreamProperty_MagicCookieData, &property_size, self->output.media_type->magic_cookie);
                if (result != noErr) {
                    ATX_LOG_WARNING_1("AudioFileStreamGetProperty failed (%d)", result);
                    ATX_FreeMemory(self->output.media_type);
                    self->output.media_type = NULL;
                    return;
                }
            }
            
            /* iterate the format list if one is available */
            result = AudioFileStreamGetPropertyInfo(stream, kAudioFileStreamProperty_FormatList, &property_size, NULL);
            if (result == noErr && property_size) {
                AudioFormatListItem* items = (AudioFormatListItem*)malloc(property_size);
                result = AudioFileStreamGetProperty(stream, kAudioFileStreamProperty_FormatList, &property_size, items);
                int item_count = property_size/sizeof(items[0]);
                int i;
                for (i=0; i<item_count; i++) {
                    ATX_LOG_FINE_7("format %d: %c%c%c%c, %d %d", 
                                   i,
                                   (items[i].mASBD.mFormatID>>24)&0xFF,
                                   (items[i].mASBD.mFormatID>>16)&0xFF,
                                   (items[i].mASBD.mFormatID>> 8)&0xFF,
                                   (items[i].mASBD.mFormatID    )&0xFF,
                                   (int)items[i].mASBD.mSampleRate,
                                   items[i].mASBD.mChannelsPerFrame);
                    if (items[i].mASBD.mFormatID == kAudioFormatMPEG4AAC_HE && 
                        AudioFileStreamParser_FormatIsSupported(self, items[i].mASBD.mFormatID)) {
                        ATX_LOG_FINE("selecting kAudioFormatMPEG4AAC_HE");
                        self->output.media_type->asbd = items[i].mASBD;
                        break;
                    }
                    if (items[i].mASBD.mFormatID == kAudioFormatMPEG4AAC_HE_V2 &&
                        AudioFileStreamParser_FormatIsSupported(self, items[i].mASBD.mFormatID)) {
                        ATX_LOG_FINE("selecting kAudioFormatMPEG4AAC_HE_V2");
                        self->output.media_type->asbd = items[i].mASBD;
                        break;
                    }
                }
                ATX_FreeMemory(items);
            }
            
            /* get the audio description if none was selected from the list previously */
            if (self->output.media_type->asbd.mFormatID == 0) {
                property_size = sizeof(AudioStreamBasicDescription);
                result = AudioFileStreamGetProperty(stream, kAudioFileStreamProperty_DataFormat, &property_size, &self->output.media_type->asbd);
                if (result != noErr) {
                    ATX_LOG_WARNING_1("AudioFileStreamGetProperty failed (%d)", result);
                    ATX_FreeMemory(self->output.media_type);
                    self->output.media_type = NULL;
                    return;
                }
                ATX_LOG_FINE_6("kAudioFileStreamProperty_DataFormat: %c%c%c%c, %d %d", 
                               (self->output.media_type->asbd.mFormatID>>24)&0xFF,
                               (self->output.media_type->asbd.mFormatID>>16)&0xFF,
                               (self->output.media_type->asbd.mFormatID>> 8)&0xFF,
                               (self->output.media_type->asbd.mFormatID    )&0xFF,
                               (int)self->output.media_type->asbd.mSampleRate,
                               self->output.media_type->asbd.mChannelsPerFrame);
            }
            
            break;
        }
        
        default:
            ATX_LOG_FINER_1("property %x", property_id);
    }
}

/*----------------------------------------------------------------------
|    AudioFileStreamParser_OnPacket
+---------------------------------------------------------------------*/
static void 
AudioFileStreamParser_OnPacket(void*                         _self,
                               UInt32                        number_of_bytes,
                               UInt32                        number_of_packets,
                               const void*                   data,
                               AudioStreamPacketDescription* packet_descriptions)
{
    AudioFileStreamParser* self = (AudioFileStreamParser*)_self;
    
    ATX_LOG_FINER_2("new packet data, size=%d, count=%d", number_of_bytes, number_of_packets);
    if (self->output.media_type == NULL) return; 
    unsigned int i;
    for (i=0; i<number_of_packets; i++) {
        // create a media packet
        BLT_MediaPacket*     packet = NULL;
        const unsigned char* packet_data = data;
        BLT_Result           result;
        result = BLT_Core_CreateMediaPacket(ATX_BASE(self, BLT_BaseMediaNode).core, 
                                            packet_descriptions[i].mDataByteSize,
                                            &self->output.media_type->base,
                                            &packet);
        if (BLT_FAILED(result)) return;
        ATX_CopyMemory(BLT_MediaPacket_GetPayloadBuffer(packet), 
                       packet_data+packet_descriptions[i].mStartOffset,
                       packet_descriptions[i].mDataByteSize);
        BLT_MediaPacket_SetPayloadSize(packet, packet_descriptions[i].mDataByteSize);
        ATX_List_AddData(self->output.packets, packet);
    }
}

/*----------------------------------------------------------------------
|   AudioFileStreamParserInput_Setup
+---------------------------------------------------------------------*/
static BLT_Result
AudioFileStreamParserInput_Setup(AudioFileStreamParser* self, 
                                 BLT_MediaTypeId        media_type_id)
{
    OSStatus status;
    
    /* decide what the media format is */
    UInt32 type_hint = 0;
    if (media_type_id == self->media_type_ids.audio_mp4) {
        ATX_LOG_FINE("packet type is audio/mp4");
        type_hint = kAudioFileM4AType;
    } else if (media_type_id == self->media_type_ids.audio_mp3) {
        ATX_LOG_FINE("packet type is audio/mp3");
        type_hint = kAudioFileMP3Type;
    } else {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    /* create the stream parser */
    status = AudioFileStreamOpen(self,
                                 AudioFileStreamParser_OnProperty,
                                 AudioFileStreamParser_OnPacket,
                                 type_hint,
                                 &self->stream_parser);
    if (status != noErr) {
        ATX_LOG_WARNING_1("AudioFileStreamOpen failed (%d)", status);
        return BLT_FAILURE;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AudioFileStreamParserInput_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
AudioFileStreamParserInput_SetStream(BLT_InputStreamUser* _self,
                                     ATX_InputStream*     stream,
                                     const BLT_MediaType* media_type)
{
    AudioFileStreamParser* self = ATX_SELF_M(input, AudioFileStreamParser, BLT_InputStreamUser);

    /* check the media type */
    if (media_type == NULL || 
        (media_type->id != self->media_type_ids.audio_mp4 &&
         media_type->id != self->media_type_ids.audio_mp3)) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    
    /* keep a reference to the stream */
    if (self->input.stream) ATX_RELEASE_OBJECT(self->input.stream);
    self->input.stream = stream;
    ATX_REFERENCE_OBJECT(stream);
    
    /* clear the current media type if we have one */
    if (self->output.media_type) {
        ATX_FreeMemory(self->output.media_type);
        self->output.media_type = NULL;
    }
    
    /* create the stream parser */
    return AudioFileStreamParserInput_Setup(self, media_type->id);
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AudioFileStreamParserInput)
    ATX_GET_INTERFACE_ACCEPT(AudioFileStreamParserInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AudioFileStreamParserInput, BLT_InputStreamUser)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AudioFileStreamParserInput, BLT_InputStreamUser)
    AudioFileStreamParserInput_SetStream
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AudioFileStreamParserInput, 
                                         "input",
                                         STREAM_PULL,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(AudioFileStreamParserInput, BLT_MediaPort)
    AudioFileStreamParserInput_GetName,
    AudioFileStreamParserInput_GetProtocol,
    AudioFileStreamParserInput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   AudioFileStreamParserOutput_Flush
+---------------------------------------------------------------------*/
static BLT_Result
AudioFileStreamParserOutput_Flush(AudioFileStreamParser* self)
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
|   AudioFileStreamParserOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
AudioFileStreamParserOutput_GetPacket(BLT_PacketProducer* _self,
                                      BLT_MediaPacket**   packet)
{
    AudioFileStreamParser* self = ATX_SELF_M(output, AudioFileStreamParser, BLT_PacketProducer);
    ATX_ListItem*          packet_item;
    OSStatus               status;

    /* default return */
    *packet = NULL;

    /* check that we have a stream */
    if (self->input.stream == NULL) return BLT_ERROR_INTERNAL;
        
    /* pump data from the source until packets start showing up */
    while ((packet_item = ATX_List_GetFirstItem(self->output.packets)) == NULL) {
        unsigned char buffer[4096];
        ATX_Size      bytes_read = 0;
        BLT_Result result = ATX_InputStream_Read(self->input.stream, buffer, sizeof(buffer), &bytes_read);
        if (BLT_FAILED(result)) return result;
        
        status = AudioFileStreamParseBytes(self->stream_parser,
                                           bytes_read,
                                           buffer,
                                           0);
        if (status != noErr) {
            ATX_LOG_WARNING_1("AudioFileStreamParseBytes failed (%x)", status);
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }            
    }

    *packet = (BLT_MediaPacket*)ATX_ListItem_GetData(packet_item);
    ATX_List_RemoveItem(self->output.packets, packet_item);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AudioFileStreamParserOutput)
    ATX_GET_INTERFACE_ACCEPT(AudioFileStreamParserOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(AudioFileStreamParserOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(AudioFileStreamParserOutput, 
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(AudioFileStreamParserOutput, BLT_MediaPort)
    AudioFileStreamParserOutput_GetName,
    AudioFileStreamParserOutput_GetProtocol,
    AudioFileStreamParserOutput_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(AudioFileStreamParserOutput, BLT_PacketProducer)
    AudioFileStreamParserOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    AudioFileStreamParser_Create
+---------------------------------------------------------------------*/
static BLT_Result
AudioFileStreamParser_Create(BLT_Module*              module,
                             BLT_Core*                core, 
                             BLT_ModuleParametersType parameters_type,
                             BLT_CString              parameters, 
                             BLT_MediaNode**          object)
{
    AudioFileStreamParser*       self;
    AudioFileStreamParserModule* parser_module = (AudioFileStreamParserModule*)module;
    BLT_Result                   result;
    
    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(AudioFileStreamParser));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* make a copy of the type ids registered by the module */
    self->media_type_ids = parser_module->media_type_ids;
    
    /* setup the input and output ports */
    self->input.eos = BLT_FALSE;

    /* create a list of input packets */
    result = ATX_List_Create(&self->output.packets);
    if (ATX_FAILED(result)) {
        ATX_FreeMemory(self);
        return result;
    }
    
    /* get a list of supported formats */
    {
        UInt32 property_size = 0;
        OSErr  status = AudioFormatGetPropertyInfo(kAudioFormatProperty_DecodeFormatIDs, 0, NULL, &property_size);
        if (status == noErr && property_size && (property_size%sizeof(UInt32)) == 0) {
            unsigned int i;
            self->supported_formats = (UInt32*)malloc(property_size);
            self->supported_format_count = property_size/sizeof(UInt32);
            status = AudioFormatGetProperty(kAudioFormatProperty_DecodeFormatIDs, 
                                            0, 
                                            NULL,
                                            &property_size, 
                                            self->supported_formats);
            if (status != noErr) {
                ATX_FreeMemory(self->supported_formats);
                self->supported_formats = NULL;
            }
            for (i=0; i<self->supported_format_count; i++) {
                ATX_LOG_FINE_5("supported format %d: %c%c%c%c", 
                               i, 
                               (self->supported_formats[i]>>24)&0xFF,
                               (self->supported_formats[i]>>16)&0xFF,
                               (self->supported_formats[i]>> 8)&0xFF,
                               (self->supported_formats[i]    )&0xFF);
            }
        }
    }
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, AudioFileStreamParser, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, AudioFileStreamParser, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(&self->input,  AudioFileStreamParserInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  AudioFileStreamParserInput,  BLT_InputStreamUser);
    ATX_SET_INTERFACE(&self->output, AudioFileStreamParserOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, AudioFileStreamParserOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    AudioFileStreamParser_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
AudioFileStreamParser_Destroy(AudioFileStreamParser* self)
{ 
    /* release any packet we may hold */
    AudioFileStreamParserOutput_Flush(self);
    ATX_List_Destroy(self->output.packets);
    BLT_MediaType_Free((BLT_MediaType*)self->output.media_type);
    
    /* release the input stream if we have one */
    if (self->input.stream) ATX_RELEASE_OBJECT(self->input.stream);
    
    /* close the stream parser */
    if (self->stream_parser) AudioFileStreamClose(self->stream_parser);

    /* free allocated memory */
    if (self->supported_formats) ATX_FreeMemory(self->supported_formats);
        
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   AudioFileStreamParser_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
AudioFileStreamParser_GetPortByName(BLT_MediaNode*  _self,
                                    BLT_CString     name,
                                    BLT_MediaPort** port)
{
    AudioFileStreamParser* self = ATX_SELF_EX(AudioFileStreamParser, BLT_BaseMediaNode, BLT_MediaNode);

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
|    AudioFileStreamParser_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
AudioFileStreamParser_Seek(BLT_MediaNode* _self,
                           BLT_SeekMode*  mode,
                           BLT_SeekPoint* point)
{
    AudioFileStreamParser* self = ATX_SELF_EX(AudioFileStreamParser, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);
    
    /* clear the eos flag */
    self->input.eos = BLT_FALSE;

    /* remove any packets in the output list */
    AudioFileStreamParserOutput_Flush(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AudioFileStreamParser)
    ATX_GET_INTERFACE_ACCEPT_EX(AudioFileStreamParser, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(AudioFileStreamParser, BLT_BaseMediaNode, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AudioFileStreamParser, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    AudioFileStreamParser_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    AudioFileStreamParser_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AudioFileStreamParser, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   AudioFileStreamParserModule_Attach
+---------------------------------------------------------------------*/
BLT_METHOD
AudioFileStreamParserModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    AudioFileStreamParserModule* self = ATX_SELF_EX(AudioFileStreamParserModule, BLT_BaseModule, BLT_Module);
    BLT_Registry*                registry;
    BLT_Result                   result;

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

    /* register the "mp3" file extension */
    result = BLT_Registry_RegisterExtension(registry, 
                                            ".mp3",
                                            "audio/mpeg");
    if (BLT_FAILED(result)) return result;

    /* register the "audio/mpeg" type */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mpeg",
        &self->media_type_ids.audio_mp3);
    if (BLT_FAILED(result)) return result;
    
    /* register mime type aliases */
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/mp3", self->media_type_ids.audio_mp3);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mp3", self->media_type_ids.audio_mp3);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/mpg", self->media_type_ids.audio_mp3);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mpg", self->media_type_ids.audio_mp3);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mpeg", self->media_type_ids.audio_mp3);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/mpeg3", self->media_type_ids.audio_mp3);
    BLT_Registry_RegisterNameForId(registry, 
                                   BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
                                   "audio/x-mpeg3", self->media_type_ids.audio_mp3);

    /* get the type id for "audio/mp4" */
    result = BLT_Registry_GetIdForName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mp4",
        &self->media_type_ids.audio_mp4);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1("audio/mp4 type = %d", self->media_type_ids.audio_mp4);

    /* get the type id for "audio/mp3" */
    result = BLT_Registry_GetIdForName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/mp3",
        &self->media_type_ids.audio_mp3);
    if (BLT_FAILED(result)) return result;
    ATX_LOG_FINE_1("audio/mp3 type = %d", self->media_type_ids.audio_mp3);

    /* register the audio/x-apple-asbd type id */
    result = BLT_Registry_RegisterName(
        registry,
        BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
        "audio/x-apple-asbd",
        &self->media_type_ids.asbd);
    if (BLT_FAILED(result)) return result;
    
    ATX_LOG_FINE_1("audio/x-apple-asbd type = %d", self->media_type_ids.asbd);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   AudioFileStreamParserModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
AudioFileStreamParserModule_Probe(BLT_Module*              _self, 
                                  BLT_Core*                core,
                                  BLT_ModuleParametersType parameters_type,
                                  BLT_AnyConst             parameters,
                                  BLT_Cardinal*            match)
{
    AudioFileStreamParserModule* self = ATX_SELF_EX(AudioFileStreamParserModule, BLT_BaseModule, BLT_Module);
    BLT_COMPILER_UNUSED(core);
    
    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* the input must be PACKET or STREAM_PULL and the output must be PACKET */
            if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be one of the supported types */
            if (constructor->spec.input.media_type->id != self->media_type_ids.audio_mp4 &&
                constructor->spec.input.media_type->id != self->media_type_ids.audio_mp3) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/x-apple-asbd */
            if (!(constructor->spec.output.media_type->id == self->media_type_ids.asbd) &&
                !(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "AudioFileStreamParser")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_MAX - 9;
            }

            ATX_LOG_FINE_1("Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(AudioFileStreamParserModule)
    ATX_GET_INTERFACE_ACCEPT_EX(AudioFileStreamParserModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(AudioFileStreamParserModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(AudioFileStreamParserModule, AudioFileStreamParser)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(AudioFileStreamParserModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    AudioFileStreamParserModule_Attach,
    AudioFileStreamParserModule_CreateInstance,
    AudioFileStreamParserModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define AudioFileStreamParserModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(AudioFileStreamParserModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   node constructor
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(AudioFileStreamParserModule, "OSX AudioFileStream Parser", 0)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_OsxAudioFileStreamParserModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return AudioFileStreamParserModule_Create(object);
}
