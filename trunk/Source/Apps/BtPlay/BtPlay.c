/*****************************************************************
|
|   BlueTune - Command-Line Player
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file 
 * Main code for BtPlay
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "Atomix.h"
#include "BlueTune.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    ATX_String    name;
    unsigned char value[16];
} BLTP_Key;

typedef struct {
    BLT_CString  output_name;
    BLT_CString  output_type;
    float        output_volume;
    unsigned int duration;
    unsigned int verbosity;
    ATX_List*    plugin_directories;
    ATX_List*    plugin_files;
    ATX_List*    keys;
} BLTP_Options;

typedef struct  {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_EventListener);
    ATX_IMPLEMENTS(ATX_PropertyListener);
    ATX_IMPLEMENTS(BLT_KeyManager);
} BLTP;

/*----------------------------------------------------------------------
|    globals
+---------------------------------------------------------------------*/
BLTP_Options Options;

/*----------------------------------------------------------------------
|    flags
+---------------------------------------------------------------------*/
#define BLTP_VERBOSITY_STREAM_TOPOLOGY      1
#define BLTP_VERBOSITY_STREAM_INFO          2
#define BLTP_VERBOSITY_MISC                 4

/*----------------------------------------------------------------------
|    macros
+---------------------------------------------------------------------*/
#define BLTP_CHECK(result)                                      \
do {                                                            \
    if (BLT_FAILED(result)) {                                   \
        fprintf(stderr, "runtime error on line %d\n", __LINE__);\
        exit(1);                                                \
    }                                                           \
} while(0)

/*----------------------------------------------------------------------
|    BLTP_PrintUsageAndExit
+---------------------------------------------------------------------*/
static void
BLTP_PrintUsageAndExit(int exit_code)
{
    ATX_ConsoleOutput(
        "--- BlueTune command line player\n"
        "--- BlueTune version " BLT_BLUETUNE_SDK_VERSION_STRING " build " BLT_SVN_VERSION_STRING "\n\n"
        "usage: btplay [options] <input-spec> [<input-spec>..]\n"
        "  each <input-spec> is either an input name (file or URL), or an input type\n"
        "  (--input-type=<type>) followed by an input name\n"
        "\n");
    ATX_ConsoleOutput(
        "options:\n"
        "  -h\n"
        "  --help\n" 
        "  --output=<name>\n"
        "  --output-type=<type>\n"
        "  --output-volume=<volume> (between 0.0 and 1.0)\n"
        "  --duration=<n> (seconds)\n"
        "  --load-plugins=<directory>[,<file-extension>]\n"
        "  --load-plugin=<plugin-filename>\n"
        "  --verbose=<name> : print messages related to <name>, where name is\n"
        "                     'stream-topology', 'stream-info', or 'all'\n"
        "                     (multiple --verbose= options can be specified)\n"
        "  --key=<name>:<value> : content decryption key for content ID <name>.\n"
        "                         The key value is in hexadecimal\n");
    exit(exit_code);
}

/*----------------------------------------------------------------------
|    BLTP_ParseHexNibble
+---------------------------------------------------------------------*/
static int
BLTP_ParseHexNibble(char nibble)
{
    switch (nibble) {
        case '0': return 0;
        case '1': return 1;
        case '2': return 2;
        case '3': return 3;
        case '4': return 4;
        case '5': return 5;
        case '6': return 6;
        case '7': return 7;
        case '8': return 8;
        case '9': return 9;
        case 'a':
        case 'A': return 0x0A;
        case 'b':
        case 'B': return 0x0B;
        case 'c':
        case 'C': return 0x0C;
        case 'd':
        case 'D': return 0x0D;
        case 'e':
        case 'E': return 0x0E;
        case 'f':
        case 'F': return 0x0F;
        default: return -1;
    }
}

/*----------------------------------------------------------------------
|    BLTP_ParseKey
+---------------------------------------------------------------------*/
static void
BLTP_ParseKey(const char* name_and_value)
{
    BLTP_Key*    key;
    unsigned int length = ATX_StringLength(name_and_value);
    
    // we need at least a ':' followed by 32 hex chars
    if (length < 33) {
        fprintf(stderr, "ERROR: invalid syntax for --key argument\n");
        return;
    }
    
    key = (BLTP_Key*)ATX_AllocateZeroMemory(sizeof(BLTP_Key));
    ATX_String_AssignN(&key->name, name_and_value, length-33);
    {
        unsigned int i;
        unsigned int x = 0;
        for (i=length-32; i<length; i+=2) {
            int nib1 = BLTP_ParseHexNibble(name_and_value[i  ]);
            int nib2 = BLTP_ParseHexNibble(name_and_value[i+1]);
            if (nib1 < 0 || nib2 < 0) {
                fprintf(stderr, "ERROR: invalid syntax for --key argument\n");
            }
            key->value[x++] = (nib1<<4) | nib2;
        }
    }
    
    ATX_List_AddData(Options.keys, key);
}

/*----------------------------------------------------------------------
|    BLTP_ParseCommandLine
+---------------------------------------------------------------------*/
static char**
BLTP_ParseCommandLine(char** args)
{
    char* arg;

    /* setup default values for options */
    Options.output_name   = BLT_DECODER_DEFAULT_OUTPUT_NAME;
    Options.output_type   = NULL;
    Options.output_volume = -1.0f;
    Options.duration      = 0;
    Options.verbosity     = 0;
    ATX_List_Create(&Options.keys);
    ATX_List_Create(&Options.plugin_directories);
    ATX_List_Create(&Options.plugin_files);
    
    while ((arg = *args)) {
        if (ATX_StringsEqual(arg, "-h") ||
            ATX_StringsEqual(arg, "--help")) {
            BLTP_PrintUsageAndExit(0);
        } else if (ATX_StringsEqualN(arg, "--output=", 9)) {
            Options.output_name = arg+9;
        } else if (ATX_StringsEqualN(arg, "--output-type=", 14)) {
            Options.output_type = arg+14;
        } else if (ATX_StringsEqualN(arg, "--output-volume=", 16)) {
            float volume;
            if (ATX_SUCCEEDED(ATX_ParseFloat(arg+16, &volume, ATX_TRUE))) {
                if (volume >= 0.0f && volume <= 1.0f) {
                    Options.output_volume = volume;
                } else {
                    fprintf(stderr, "ERROR: output volume value out of range\n");
                }
            } else {
                fprintf(stderr, "ERROR: invalid output volume value\n");
            }
        } else if (ATX_StringsEqualN(arg, "--duration=", 11)) {
            int duration = 0;
            ATX_ParseInteger(arg+11, &duration, ATX_FALSE);
            Options.duration = duration;
        } else if (ATX_StringsEqualN(arg, "--load-plugins=", 15)) {
            ATX_String* directory = (ATX_String*)ATX_AllocateMemory(sizeof(ATX_String));
            *directory = ATX_String_Create(arg+15);
            ATX_List_AddData(Options.plugin_directories, directory);
        } else if (ATX_StringsEqualN(arg, "--load-plugin=", 14)) {
            ATX_String* plugin = (ATX_String*)ATX_AllocateMemory(sizeof(ATX_String));
            *plugin = ATX_String_Create(arg+14);
            ATX_List_AddData(Options.plugin_files, plugin);
        } else if (ATX_StringsEqualN(arg, "--verbose=", 10)) {
            if (ATX_StringsEqual(arg+10, "stream-topology")) {
                Options.verbosity |= BLTP_VERBOSITY_STREAM_TOPOLOGY;
            } else if (ATX_StringsEqual(arg+10, "stream-info")) {
                Options.verbosity |= BLTP_VERBOSITY_STREAM_INFO;
            } else if (ATX_StringsEqual(arg+10, "all")) {
                Options.verbosity = 0xFFFFFFFF;
            }
        } else if (ATX_StringsEqualN(arg, "--key=", 6)) {
            BLTP_ParseKey(arg+6);
        } else {
            return args;
        }
        ++args;
    }

    return args;
}

/*----------------------------------------------------------------------
|    BLTP_SetupKeyManager
+---------------------------------------------------------------------*/
static void
BLTP_SetupKeyManager(BLTP* player, BLT_Decoder* decoder)
{
    ATX_Properties* properties;
    BLT_Decoder_GetProperties(decoder, &properties);

    {
        ATX_PropertyValue value;
        value.type         = ATX_PROPERTY_VALUE_TYPE_POINTER;
        value.data.pointer = &ATX_BASE(player, BLT_KeyManager);
        ATX_Properties_SetProperty(properties, BLT_KEY_MANAGER_PROPERTY, &value);
    }
}

/*----------------------------------------------------------------------
|    BLTP_OnStreamPropertyChanged
+---------------------------------------------------------------------*/
BLT_VOID_METHOD
BLTP_OnStreamPropertyChanged(ATX_PropertyListener*    self,
                             ATX_CString              name,
                             const ATX_PropertyValue* value)    
{
    BLT_COMPILER_UNUSED(self);

    if (!(Options.verbosity & BLTP_VERBOSITY_STREAM_INFO)) return;
    
    if (name == NULL) {
        ATX_ConsoleOutput("BLTP::OnStreamPropertyChanged - All Properties Cleared\n");
    } else {
        if (value == NULL) {
            ATX_ConsoleOutputF("BLTP::OnStreamPropertyChanged - Property %s cleared\n", name);
        } else {
            ATX_ConsoleOutputF("BLTP::OnStreamPropretyChanged - %s = ", name);
            switch (value->type) {
              case ATX_PROPERTY_VALUE_TYPE_STRING:
                ATX_ConsoleOutputF("%s\n", value->data.string);
                break;

              case ATX_PROPERTY_VALUE_TYPE_INTEGER:
                ATX_ConsoleOutputF("%d\n", value->data.integer);
                break;

              default:
                ATX_ConsoleOutput("\n");
            }
        }
    }
}

/*----------------------------------------------------------------------
|    BLTP_GetKeyByName
+---------------------------------------------------------------------*/
BLT_METHOD
BLTP_GetKeyByName(BLT_KeyManager* self,
                  const char*     name,
                  unsigned char*  key, 
                  unsigned int*   key_size)
{
    ATX_ListItem* item = ATX_List_GetFirstItem(Options.keys);
    
    /* check the key size */
    if (*key_size < 16) {
        *key_size = 16;
        return BLT_ERROR_BUFFER_TOO_SMALL;
    }
    
    for (; item; item = ATX_ListItem_GetNext(item)) {
        BLTP_Key* key_info = (BLTP_Key*)ATX_ListItem_GetData(item);
        if (ATX_String_Equals(&key_info->name, name, ATX_FALSE)) {
            /* found a match */
            ATX_CopyMemory(key, key_info->value, 16);
            *key_size = 16;
            return BLT_SUCCESS;
        }
    }
    
    return ATX_ERROR_NO_SUCH_ITEM;
}

/*----------------------------------------------------------------------
|    BLTP_ShowStreamTopology
+---------------------------------------------------------------------*/
static void
BLTP_ShowStreamTopology(ATX_Object* source)
{
    BLT_Stream*        stream;
    BLT_MediaNode*     node;
    BLT_StreamNodeInfo s_info;
    BLT_Result         result;

    /* cast the source object to a stream object */
    stream = ATX_CAST(source, BLT_Stream);
    if (stream == NULL) return;

    result = BLT_Stream_GetFirstNode(stream, &node);
    if (BLT_FAILED(result)) return;
    while (node) {
        const char* name;
        BLT_MediaNodeInfo n_info;
        result = BLT_Stream_GetStreamNodeInfo(stream, node, &s_info);
        if (BLT_FAILED(result)) break;
        result = BLT_MediaNode_GetInfo(node, &n_info);
        if (BLT_SUCCEEDED(result)) {
            name = n_info.name ? n_info.name : "?";
        } else {
            name = "UNKNOWN";
        }

        if (s_info.input.connected) {
            ATX_ConsoleOutput("-");
        } else {
            ATX_ConsoleOutput(".");
        }

        switch (s_info.input.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_NONE:
            ATX_ConsoleOutput("!"); break;
          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            ATX_ConsoleOutput("#"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            ATX_ConsoleOutput(">"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
            ATX_ConsoleOutput("<"); break;
          default:
            ATX_ConsoleOutput("@"); break;
        }

        ATX_ConsoleOutputF("[%s]", name);

        switch (s_info.output.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_NONE:
            ATX_ConsoleOutput("!"); break;
          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            ATX_ConsoleOutput("#"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            ATX_ConsoleOutput(">"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
            ATX_ConsoleOutput("<"); break;
          default:
            ATX_ConsoleOutput("@"); break;
        }

        if (s_info.output.connected) {
            ATX_ConsoleOutput("-");
        } else {
            ATX_ConsoleOutput(".");
        }

        result = BLT_Stream_GetNextNode(stream, node, &node);
        if (BLT_FAILED(result)) break;
    }
    ATX_ConsoleOutput("\n");
}

/*----------------------------------------------------------------------
|    BLTP_OnEvent
+---------------------------------------------------------------------*/
BLT_VOID_METHOD 
BLTP_OnEvent(BLT_EventListener* self,
             ATX_Object*        source,
             BLT_EventType      type,
             const BLT_Event*   event)
{
    BLT_COMPILER_UNUSED(self);
    if (type == BLT_EVENT_TYPE_STREAM_INFO && 
        Options.verbosity & BLTP_VERBOSITY_STREAM_INFO) {
        const BLT_StreamInfoEvent* e = (BLT_StreamInfoEvent*)event;
        ATX_ConsoleOutputF("BLTP::OnEvent - info update=%x\n", e->update_mask);

        if (e->update_mask & BLT_STREAM_INFO_MASK_NOMINAL_BITRATE) {
            ATX_ConsoleOutputF("  nominal_bitrate = %ld\n", e->info.nominal_bitrate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_AVERAGE_BITRATE) {
            ATX_ConsoleOutputF("  average_bitrate = %ld\n", e->info.average_bitrate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_INSTANT_BITRATE) {
            ATX_ConsoleOutputF("  instant_bitrate = %ld\n", e->info.instant_bitrate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_SIZE) {
            ATX_ConsoleOutputF("  size            = %" ATX_INT64_PRINTF_FORMAT "d\n", e->info.size);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_DURATION) {
            ATX_ConsoleOutputF("  duration        = %" ATX_INT64_PRINTF_FORMAT "d\n", e->info.duration);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_SAMPLE_RATE) {
            ATX_ConsoleOutputF("  sample_rate     = %ld\n", e->info.sample_rate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_CHANNEL_COUNT) {
            ATX_ConsoleOutputF("  channel_count   = %ld\n", e->info.channel_count);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_FLAGS) {
            ATX_ConsoleOutputF("  flags           = %x", e->info.flags);
            if (e->info.flags & BLT_STREAM_INFO_FLAG_VBR) {
                ATX_ConsoleOutputF(" (VBR)\n");
            } else {
                ATX_ConsoleOutput("\n");
            }
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
            ATX_ConsoleOutputF("  data_type       = %s\n", 
                               e->info.data_type ? e->info.data_type : "");
        }
    } else if (type == BLT_EVENT_TYPE_STREAM_TOPOLOGY &&
               Options.verbosity & BLTP_VERBOSITY_STREAM_TOPOLOGY) {
        const BLT_StreamTopologyEvent* e = (BLT_StreamTopologyEvent*)event;
        switch (e->type) {
          case BLT_STREAM_TOPOLOGY_NODE_ADDED:
            ATX_ConsoleOutput("STREAM TOPOLOGY: node added\n");  
            break;
          case BLT_STREAM_TOPOLOGY_NODE_REMOVED:
            ATX_ConsoleOutput("STREAM TOPOLOGY: node removed\n");
            break;
          case BLT_STREAM_TOPOLOGY_NODE_CONNECTED:
            ATX_ConsoleOutput("STREAM TOPOLOGY: node connected\n");
            break;
          default:
            break;
        }
        BLTP_ShowStreamTopology(source);
    }
}

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLTP)
    ATX_GET_INTERFACE_ACCEPT(BLTP, BLT_EventListener)
    ATX_GET_INTERFACE_ACCEPT(BLTP, ATX_PropertyListener)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    ATX_PropertyListener interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLTP, ATX_PropertyListener)
    BLTP_OnStreamPropertyChanged
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_EventListener interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLTP, BLT_EventListener)
    BLTP_OnEvent
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_KeyManager interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLTP, BLT_KeyManager)
    BLTP_GetKeyByName
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLTP_CheckElapsedTime
+---------------------------------------------------------------------*/
static BLT_Result
BLTP_CheckElapsedTime(BLT_Decoder* decoder, unsigned int duration)
{
    BLT_DecoderStatus status;
    
    if (duration == 0) return BLT_SUCCESS;
    BLT_Decoder_GetStatus(decoder, &status);
    if (status.time_stamp.seconds > (int)duration) {
        ATX_ConsoleOutput("END of specified duration\n");
        return BLT_FAILURE;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
    BLT_Decoder* decoder;
    BLT_CString  input_name;
    BLT_CString  input_type = NULL;
    BLTP         player;
    BLT_Result   result;

    /*mtrace();*/
    BLT_COMPILER_UNUSED(argc);

    /* parse command line */
    if (argc < 2) BLTP_PrintUsageAndExit(0);
    argv = BLTP_ParseCommandLine(argv+1);

    /* create a decoder */
    result = BLT_Decoder_Create(&decoder);
    BLTP_CHECK(result);

    /* setup our interfaces */
    ATX_SET_INTERFACE(&player, BLTP, BLT_EventListener);
    ATX_SET_INTERFACE(&player, BLTP, ATX_PropertyListener);
    ATX_SET_INTERFACE(&player, BLTP, BLT_KeyManager);

    /* listen to stream events */
    BLT_Decoder_SetEventListener(decoder, &ATX_BASE(&player, BLT_EventListener));
             
    /* listen to stream properties events */
    {
        ATX_Properties* properties;
        BLT_Decoder_GetStreamProperties(decoder, &properties);
        ATX_Properties_AddListener(properties, NULL, &ATX_BASE(&player, ATX_PropertyListener), NULL);
    }

    /* setup a key manager for encrypted files */
    BLTP_SetupKeyManager(&player, decoder);
    
    /* register builtin modules */
    result = BLT_Decoder_RegisterBuiltins(decoder);
    BLTP_CHECK(result);

    /* load and register loadable plugins */
    {
        ATX_ListItem* item;
        for (item = ATX_List_GetFirstItem(Options.plugin_files); item; item = ATX_ListItem_GetNext(item)) {
            ATX_String* plugin = (ATX_String*)ATX_ListItem_GetData(item);
            BLT_Decoder_LoadPlugin(decoder, ATX_String_GetChars(plugin), BLT_PLUGIN_LOADER_FLAGS_SEARCH_ALL);
        }
        for (item = ATX_List_GetFirstItem(Options.plugin_directories); item; item = ATX_ListItem_GetNext(item)) {
            ATX_String* directory = (ATX_String*)ATX_ListItem_GetData(item);
            BLT_Decoder_LoadPlugins(decoder, ATX_String_GetChars(directory), "plugin");
        }
    }
    
    /* set the output */
    result = BLT_Decoder_SetOutput(decoder,
                                   Options.output_name, 
                                   Options.output_type);
    if (BLT_FAILED(result)) {
        fprintf(stderr, "SetOutput failed: %d (%s)\n", result, BLT_ResultText(result));
        exit(1);
    }

    /* set the output volume */
    if (Options.output_volume >= 0.0f) {
        result = BLT_Decoder_SetVolume(decoder, Options.output_volume);
        if (BLT_FAILED(result)) {
            fprintf(stderr, "SetVolume failed: %d (%s)\n", result, BLT_ResultText(result));
        }
    }
    
    /* enable the gain control filter */
    BLT_Decoder_AddNodeByName(decoder, NULL, "GainControlFilter");

    /* by default, add a filter host module */
    BLT_Decoder_AddNodeByName(decoder, NULL, "FilterHost");

    /* process each input in turn */
    while ((input_name = *argv++)) {
        if (ATX_StringsEqualN(input_name, "--input-type=", 13)) {
            input_type = input_name+13;
            continue;
        }

        /* set the input name */
        result = BLT_Decoder_SetInput(decoder, input_name, input_type);
        if (BLT_FAILED(result)) {
            ATX_ConsoleOutputF("SetInput failed: %d (%s)\n", result, BLT_ResultText(result));
            input_type = NULL;
            continue;
        }

        /* pump the packets */
        do {
            /* process one packet */
            result = BLT_Decoder_PumpPacket(decoder);
            
            /* if a duration is specified, check if we have exceeded it */
            if (BLT_SUCCEEDED(result)) result = BLTP_CheckElapsedTime(decoder, Options.duration);
        } while (BLT_SUCCEEDED(result));
        if (Options.verbosity & BLTP_VERBOSITY_MISC) {
            ATX_ConsoleOutputF("final result = %d (%s)\n", result, BLT_ResultText(result));
        }

        /* reset input type */
        input_type = NULL;
    }

    /* destroy the decoder */
    BLT_Decoder_Destroy(decoder);

    /* cleanup */
    {
        ATX_ListItem* item;
        for (item = ATX_List_GetFirstItem(Options.plugin_files); item; item = ATX_ListItem_GetNext(item)) {
            ATX_String* s = (ATX_String*)ATX_ListItem_GetData(item);
            ATX_String_Destruct(s);
            ATX_FreeMemory(s);
        }
        ATX_List_Destroy(Options.plugin_files);
        for (item = ATX_List_GetFirstItem(Options.plugin_directories); item; item = ATX_ListItem_GetNext(item)) {
            ATX_String* s = (ATX_String*)ATX_ListItem_GetData(item);
            ATX_String_Destruct(s);
            ATX_FreeMemory(s);
        }
        ATX_List_Destroy(Options.plugin_directories);
    }
    
    return 0;
}
