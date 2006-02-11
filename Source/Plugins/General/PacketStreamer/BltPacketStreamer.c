/*****************************************************************
|
|      File: BltPacketStreamer.c
|
|      Debug Output Module
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
#include "BltPacketStreamer.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPacketConsumer.h"
#include "BltByteStreamUser.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerModule)
static const BLT_ModuleInterface PacketStreamerModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamer)
static const BLT_MediaNodeInterface PacketStreamer_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerInputPort)
static const BLT_MediaPortInterface PacketStreamerInputPort_BLT_MediaPortInterface;
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerOutputPort)
static const BLT_MediaPortInterface PacketStreamerOutputPort_BLT_MediaPortInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} PacketStreamerModule;

typedef struct {
    BLT_BaseMediaNode base;
    ATX_List*         packets;
    ATX_OutputStream  stream;
    BLT_MediaType*    media_type;
} PacketStreamer;

/*----------------------------------------------------------------------
|    PacketStreamerInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
PacketStreamerInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                                  BLT_MediaPacket*            packet)
{
    PacketStreamer*      streamer = (PacketStreamer*)instance;
    const BLT_MediaType* media_type;
    BLT_Size             size;
    BLT_Any              payload;

    /* check the media type */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (streamer->media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN) {
        BLT_MediaType_Free(streamer->media_type);
        BLT_MediaType_Clone(media_type, &streamer->media_type);
    } else {
        if (streamer->media_type->id != media_type->id) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
    }

    /* just buffer the packets if we have no stream yet */
    if (ATX_OBJECT_IS_NULL(&streamer->stream)) {
        /* add the packet to the input list */
        BLT_Result result = ATX_List_AddData(streamer->packets, packet);
        if (ATX_SUCCEEDED(result)) {
            BLT_MediaPacket_AddReference(packet);
        }
        BLT_Debug("PacketStreamerInputPort_PutPacket - buffer\n");
        return BLT_SUCCESS;
    }

    /* flush any pending packets */
    {
        ATX_ListItem* item;
        while ((item = ATX_List_GetFirstItem(streamer->packets))) {
            BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
            if (packet) {
                size    = BLT_MediaPacket_GetPayloadSize(packet);
                payload = BLT_MediaPacket_GetPayloadBuffer(packet);
                if (size != 0 && payload != NULL) {
                    ATX_OutputStream_Write(&streamer->stream, 
                                           payload, 
                                           size, 
                                           NULL);
                }
                BLT_MediaPacket_Release(packet);
            }
            ATX_List_RemoveItem(streamer->packets, item);
        }
    }

    /* get payload and size of this packet */
    size    = BLT_MediaPacket_GetPayloadSize(packet);
    payload = BLT_MediaPacket_GetPayloadBuffer(packet);
    if (size == 0 || payload == NULL) return BLT_SUCCESS;

    /* write packet to the output stream */
    return ATX_OutputStream_Write(&streamer->stream, payload, size, NULL);
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(PacketStreamerInputPort,
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
PacketStreamerInputPort_BLT_MediaPortInterface = {
    PacketStreamerInputPort_GetInterface,
    PacketStreamerInputPort_GetName,
    PacketStreamerInputPort_GetProtocol,
    PacketStreamerInputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
PacketStreamerInputPort_BLT_PacketConsumerInterface = {
    PacketStreamerInputPort_GetInterface,
    PacketStreamerInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerInputPort)
ATX_INTERFACE_MAP_ADD(PacketStreamerInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(PacketStreamerInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerInputPort)

/*----------------------------------------------------------------------
|    PacketStreamerOutputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
PacketStreamerOutputPort_SetStream(BLT_OutputStreamUserInstance* instance,
                                   ATX_OutputStream*             stream)
{
    PacketStreamer* streamer = (PacketStreamer*)instance;

    /* keep a reference to the stream */
    streamer->stream = *stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    PacketStreamerOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
PacketStreamerOutputPort_QueryMediaType(
    BLT_MediaPortInstance* instance,
    BLT_Ordinal            index,
    const BLT_MediaType**  media_type)
{
    PacketStreamer* streamer = (PacketStreamer*)instance;
    if (index == 0) {
        *media_type = streamer->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(PacketStreamerOutputPort,
                                         "output",
                                         STREAM_PUSH,
                                         OUT)
static const BLT_MediaPortInterface
PacketStreamerOutputPort_BLT_MediaPortInterface = {
    PacketStreamerOutputPort_GetInterface,
    PacketStreamerOutputPort_GetName,
    PacketStreamerOutputPort_GetProtocol,
    PacketStreamerOutputPort_GetDirection,
    PacketStreamerOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_OutputStreamUser interface
+---------------------------------------------------------------------*/
static const BLT_OutputStreamUserInterface
PacketStreamerOutputPort_BLT_OutputStreamUserInterface = {
    PacketStreamerOutputPort_GetInterface,
    PacketStreamerOutputPort_SetStream
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerOutputPort)
ATX_INTERFACE_MAP_ADD(PacketStreamerOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(PacketStreamerOutputPort, BLT_OutputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerOutputPort)

/*----------------------------------------------------------------------
|    PacketStreamer_Create
+---------------------------------------------------------------------*/
static BLT_Result
PacketStreamer_Create(BLT_Module*              module,
                      BLT_Core*                core, 
                      BLT_ModuleParametersType parameters_type,
                      BLT_CString              parameters, 
                      ATX_Object*              object)
{
    PacketStreamer*           streamer;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    BLT_Result                result;

    BLT_Debug("PacketStreamer::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    streamer = ATX_AllocateZeroMemory(sizeof(PacketStreamer));
    if (streamer == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&streamer->base, module, core);

    /* create a list of input packets */
    result = ATX_List_Create(&streamer->packets);
    if (ATX_FAILED(result)) return result;

    /* keep the media type info */
    BLT_MediaType_Clone(constructor->spec.input.media_type,
                        &streamer->media_type);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)streamer;
    ATX_INTERFACE(object) = (ATX_Interface*)&PacketStreamer_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    PacketStreamer_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
PacketStreamer_Destroy(PacketStreamer* streamer)
{
    ATX_ListItem* item;

    BLT_Debug("PacketStreamer::Destroy\n");

    /* release the stream */
    ATX_RELEASE_OBJECT(&streamer->stream);

    /* destroy the input packet list */
    item = ATX_List_GetFirstItem(streamer->packets);
    while (item) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) {
            BLT_MediaPacket_Release(packet);
        }
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Destroy(streamer->packets);

    /* free the media type extensions */
    BLT_MediaType_Free(streamer->media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&streamer->base);

    /* free the object memory */
    ATX_FreeMemory(streamer);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       PacketStreamer_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
PacketStreamer_GetPortByName(BLT_MediaNodeInstance* instance,
                             BLT_CString            name,
                             BLT_MediaPort*         port)
{
    PacketStreamer* streamer = (PacketStreamer*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)streamer;
        ATX_INTERFACE(port) = &PacketStreamerInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port) = (BLT_MediaPortInstance*)streamer;
        ATX_INTERFACE(port)= &PacketStreamerOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
PacketStreamer_BLT_MediaNodeInterface = {
    PacketStreamer_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    PacketStreamer_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(PacketStreamer, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamer)
ATX_INTERFACE_MAP_ADD(PacketStreamer, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(PacketStreamer, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamer)

/*----------------------------------------------------------------------
|       PacketStreamerModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
PacketStreamerModule_Probe(BLT_ModuleInstance*      instance, 
                           BLT_Core*                core,
                           BLT_ModuleParametersType parameters_type,
                           BLT_AnyConst             parameters,
                           BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(instance);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* media types must match */
            if (constructor->spec.input.media_type->id !=
                BLT_MEDIA_TYPE_ID_UNKNOWN &&
                constructor->spec.output.media_type->id !=
                BLT_MEDIA_TYPE_ID_UNKNOWN &&
                constructor->spec.input.media_type->id !=
                constructor->spec.output.media_type->id) {
                return BLT_FAILURE;
            }

            /* compute match based on specified name */
            if (constructor->name == NULL) {
                *match = BLT_MODULE_PROBE_MATCH_DEFAULT;

                /* the input protocol should be PACKET */
                if (constructor->spec.input.protocol != 
                    BLT_MEDIA_PORT_PROTOCOL_PACKET) {
                    return BLT_FAILURE;
                }

                /* output protocol should be STREAM_PUSH or ANY */
                if (constructor->spec.output.protocol == 
                    BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH) {
                    *match += 10;
                } else if (constructor->spec.output.protocol !=
                           BLT_MEDIA_PORT_PROTOCOL_ANY) {
                    return BLT_FAILURE;
                }
            } else {
                /* if a name is a specified, it needs to match exactly */
                if (!ATX_StringsEqual(constructor->name, "PacketStreamer")) {
                    return BLT_FAILURE;
                } else {
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                }

                /* the input protocol should be PACKET or ANY */
                if (constructor->spec.input.protocol !=
                    BLT_MEDIA_PORT_PROTOCOL_ANY &&
                    constructor->spec.input.protocol != 
                    BLT_MEDIA_PORT_PROTOCOL_PACKET) {
                    return BLT_FAILURE;
                }

                /* output protocol should be STREAM_PUSH or ANY */
                if ((constructor->spec.output.protocol !=
                    BLT_MEDIA_PORT_PROTOCOL_ANY &&
                    constructor->spec.output.protocol != 
                    BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH)) {
                    return BLT_FAILURE;
                }
            }

            BLT_Debug("PacketStreamerModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(PacketStreamer)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface PacketStreamerModule_BLT_ModuleInterface = {
    PacketStreamerModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    PacketStreamerModule_CreateInstance,
    PacketStreamerModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define PacketStreamerModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(PacketStreamerModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerModule) 
ATX_INTERFACE_MAP_ADD(PacketStreamerModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(PacketStreamerModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PacketStreamerModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_PacketStreamerModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Packet Streamer", NULL, 0,
                                 &PacketStreamerModule_BLT_ModuleInterface,
                                 object);
}
