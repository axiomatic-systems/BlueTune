/*****************************************************************
|
|      File: BltPlay.c
|
|      BlueTune - Command-Line Player
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
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
    BLT_CString output_name;
    BLT_CString output_type;
} BLTP_Options;

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
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(BLTP)

/*----------------------------------------------------------------------
|    BLTP_PrintUsageAndExit
+---------------------------------------------------------------------*/
static void
BLTP_PrintUsageAndExit(int exit_code)
{
    BLT_Debug("usage: btplay [options] <input-spec> [<input-spec>..]\n"
              "  each <input-spec> is either an input name (file or URL), or an input type\n"
              "  (--input-type=<type>) followed by an input name\n"
              "\n"
              "options:\n"
              "  -h\n"
              "  --help\n" 
              "  --output=<name>\n"
              "  --output-type=<type>\n");
    exit(exit_code);
}

/*----------------------------------------------------------------------
|    BLTP_ParseCommandLine
+---------------------------------------------------------------------*/
static char**
BLTP_ParseCommandLine(char** args, BLTP_Options* options)
{
    char* arg;

    /* setup default values for options */
    options->output_name = BLT_DECODER_DEFAULT_OUTPUT_NAME;
    options->output_type = NULL;

    while ((arg = *args)) {
        if (ATX_StringsEqual(arg, "-h") ||
            ATX_StringsEqual(arg, "--help")) {
            BLTP_PrintUsageAndExit(0);
        } else if (ATX_StringsEqualN(arg, "--output=", 9)) {
            options->output_name = arg+9;
        } else if (ATX_StringsEqualN(arg, "--output-type=", 14)) {
            options->output_type = arg+14;
        } else {
            return args;
        }
        args++;
    }

    return args;
}

/*----------------------------------------------------------------------
|    BLTP_OnStreamPropertyChanged
+---------------------------------------------------------------------*/
BLT_VOID_METHOD
BLTP_OnStreamPropertyChanged(ATX_PropertyListenerInstance* instance,
                             ATX_CString                   name,
                             ATX_PropertyType              type,
                             const ATX_PropertyValue*      value)    
{
    BLT_COMPILER_UNUSED(instance);

    if (name == NULL) {
        BLT_Debug("BLTP::OnStreamPropertyChanged - All Properties Cleared\n");
    } else {
        if (value == NULL) {
            BLT_Debug("BLTP::OnStreamPropertyChanged - Property %s cleared\n",
                      name);
        } else {
            BLT_Debug("BLTP::OnStreamPropretyChanged - %s = ", name);
            switch (type) {
              case ATX_PROPERTY_TYPE_STRING:
                BLT_Debug("%s\n", value->string);
                break;

              case ATX_PROPERTY_TYPE_INTEGER:
                BLT_Debug("%d\n", value->integer);
                break;

              default:
                BLT_Debug("\n");
            }
        }
    }
}

/*----------------------------------------------------------------------
|    ATX_PropertyListener interface
+---------------------------------------------------------------------*/
static const ATX_PropertyListenerInterface
BLTP_ATX_PropertyListenerInterface = {
    BLTP_GetInterface,
    BLTP_OnStreamPropertyChanged,
};

/*----------------------------------------------------------------------
|    BLTP_ShowStreamTopology
+---------------------------------------------------------------------*/
static void
BLTP_ShowStreamTopology(const ATX_Polymorphic* source)
{
    BLT_Stream         stream;
    BLT_MediaNode      node;
    BLT_StreamNodeInfo s_info;
    BLT_Result         result;

    /* cast the source object to a stream object */
    if (BLT_FAILED(ATX_CAST_OBJECT(source, &stream, BLT_Stream))) {
        return;
    }

    result = BLT_Stream_GetFirstNode(&stream, &node);
    if (BLT_FAILED(result)) return;
    while (!ATX_INSTANCE_IS_NULL(&node)) {
        const char* name;
        BLT_MediaNodeInfo n_info;
        result = BLT_Stream_GetStreamNodeInfo(&stream, &node, &s_info);
        if (BLT_FAILED(result)) break;
        result = BLT_MediaNode_GetInfo(&node, &n_info);
        if (BLT_SUCCEEDED(result)) {
            name = n_info.name ? n_info.name : "?";
        } else {
            name = "UNKNOWN";
        }

        if (s_info.input.connected) {
            BLT_Debug("-");
        } else {
            BLT_Debug(".");
        }

        switch (s_info.input.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_NONE:
            BLT_Debug("!"); break;
          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            BLT_Debug("#"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            BLT_Debug(">"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
            BLT_Debug("<"); break;
          default:
            BLT_Debug("@"); break;
        }

        BLT_Debug("[%s]", name);

        switch (s_info.output.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_NONE:
            BLT_Debug("!"); break;
          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            BLT_Debug("#"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            BLT_Debug(">"); break;
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
            BLT_Debug("<"); break;
          default:
            BLT_Debug("@"); break;
        }

        if (s_info.output.connected) {
            BLT_Debug("-");
        } else {
            BLT_Debug(".");
        }

        result = BLT_Stream_GetNextNode(&stream, &node, &node);
        if (BLT_FAILED(result)) break;
    }
    BLT_Debug("\n");
}

/*----------------------------------------------------------------------
|    BLTP_OnEvent
+---------------------------------------------------------------------*/
BLT_VOID_METHOD 
BLTP_OnEvent(BLT_EventListenerInstance* instance,
             const ATX_Object*          source,
             BLT_EventType              type,
             const BLT_Event*           event)
{
    BLT_COMPILER_UNUSED(instance);
    BLT_Debug("BLTP::OnEvent - type = %d\n", (int)type);
    if (type == BLT_EVENT_TYPE_STREAM_INFO) {
        const BLT_StreamInfoEvent* e = (BLT_StreamInfoEvent*)event;
        BLT_Debug("BLTP::OnEvent - info update=%x\n", e->update_mask);

        if (e->update_mask & BLT_STREAM_INFO_MASK_NOMINAL_BITRATE) {
            BLT_Debug("  nominal_bitrate = %ld\n", e->info.nominal_bitrate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_AVERAGE_BITRATE) {
            BLT_Debug("  average_bitrate = %ld\n", e->info.average_bitrate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_INSTANT_BITRATE) {
            BLT_Debug("  instant_bitrate = %ld\n", e->info.instant_bitrate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_SIZE) {
            BLT_Debug("  size            = %ld\n", e->info.size);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_DURATION) {
            BLT_Debug("  duration        = %ld\n", e->info.duration);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_SAMPLE_RATE) {
            BLT_Debug("  sample_rate     = %ld\n", e->info.sample_rate);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_CHANNEL_COUNT) {
            BLT_Debug("  channel_count   = %ld\n", e->info.channel_count);
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_FLAGS) {
            BLT_Debug("  flags           = %x", e->info.flags);
            if (e->info.flags & BLT_STREAM_INFO_FLAG_VBR) {
                BLT_Debug(" (VBR)\n");
            } else {
                BLT_Debug("\n");
            }
        }
        if (e->update_mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
            BLT_Debug("  data_type       = %s\n", 
                      e->info.data_type ? e->info.data_type : "");
        }
    } else if (type == BLT_EVENT_TYPE_STREAM_TOPOLOGY) {
        const BLT_StreamTopologyEvent* e = (BLT_StreamTopologyEvent*)event;
        switch (e->type) {
          case BLT_STREAM_TOPOLOGY_NODE_ADDED:
            BLT_Debug("STREAM TOPOLOGY: node added\n");  
            break;
          case BLT_STREAM_TOPOLOGY_NODE_REMOVED:
            BLT_Debug("STREAM TOPOLOGY: node removes\n");
            break;
          case BLT_STREAM_TOPOLOGY_NODE_CONNECTED:
            BLT_Debug("STREAM TOPOLOGY: node connected\n");
            break;
          default:
            break;
        }
        BLTP_ShowStreamTopology((const ATX_Polymorphic*)source);
    }
}

/*----------------------------------------------------------------------
|    BLT_EventListener interface
+---------------------------------------------------------------------*/
static const BLT_EventListenerInterface
BLTP_BLT_EventListenerInterface = {
    BLTP_GetInterface,
    BLTP_OnEvent
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(BLTP)
ATX_INTERFACE_MAP_ADD(BLTP, BLT_EventListener)
ATX_INTERFACE_MAP_ADD(BLTP, ATX_PropertyListener)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(BLTP)

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
    BLTP_Options options;
    BLT_Decoder* decoder;
    BLT_CString  input_name;
    BLT_CString  input_type = NULL;
    BLT_Result   result;

    /*mtrace();*/
    BLT_COMPILER_UNUSED(argc);

    /* parse command line */
    if (argc < 2) BLTP_PrintUsageAndExit(0);
    argv = BLTP_ParseCommandLine(argv+1, &options);

    /* create a decoder */
    result = BLT_Decoder_Create(&decoder);
    BLTP_CHECK(result);

    /* listen to stream events */
    {
        BLT_EventListener self;
        ATX_INSTANCE(&self)  = NULL;
        ATX_INTERFACE(&self) = &BLTP_BLT_EventListenerInterface;
        BLT_Decoder_SetEventListener(decoder, &self);
    }
             
    /* listen to stream properties events */
    {
        ATX_PropertyListener self;
        ATX_Properties       properties;
        ATX_INSTANCE(&self) = NULL;
        ATX_INTERFACE(&self) = &BLTP_ATX_PropertyListenerInterface;
        BLT_Decoder_GetStreamProperties(decoder, &properties);
        ATX_Properties_AddListener(&properties, NULL, &self, NULL);
    }

    /* register builtin modules */
    result = BLT_Decoder_RegisterBuiltins(decoder);
    BLTP_CHECK(result);

    /* set the output */
    result = BLT_Decoder_SetOutput(decoder,
                                   options.output_name, 
                                   options.output_type);
    if (BLT_FAILED(result)) {
        fprintf(stderr, "SetOutput failed (%d)\n", result);
        exit(1);
    }

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
            fprintf(stderr, "SetInput failed (%d)\n", result);
            input_type = NULL;
            continue;
        }

        /* pump the packets */
        do {
            result = BLT_Decoder_PumpPacket(decoder);
        } while (BLT_SUCCEEDED(result));
        BLT_Debug("BtPlay:: final result = %d\n", result);

        /* reset input type */
        input_type = NULL;
    }

    /* destroy the decoder */
    BLT_Decoder_Destroy(decoder);

    return 0;
}
