/*****************************************************************
|
|      File: BltStream.c
|
|      BlueTune - Stream Objects
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Stream Objects
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltDebug.h"
#include "BltErrors.h"
#include "BltCore.h"
#include "BltStream.h"
#include "BltStreamPriv.h"
#include "BltMediaNode.h"
#include "BltByteStreamProvider.h"
#include "BltByteStreamUser.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltBuiltins.h"
#include "BltEvent.h"
#include "BltEventListener.h"
#include "BltOutputNode.h"

/*----------------------------------------------------------------------
|    macros
+---------------------------------------------------------------------*/
#define STREAM_LOG(l, m) BLT_LOG(BLT_LOG_CHANNEL_STREAM, l, m)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct StreamNode {
    BLT_Stream         stream;
    BLT_MediaNode      media_node;
    BLT_MediaNodeState state;
    BLT_Flags          flags;
    struct {
        BLT_MediaPortProtocol protocol;
        BLT_MediaPort         port;
        BLT_Boolean           connected;
        union {
            BLT_InputStreamUser      stream_user;
            BLT_OutputStreamProvider stream_provider;
            BLT_PacketConsumer       packet_consumer;
        }                     iface;
    }                  input;
    struct {
        BLT_MediaPortProtocol protocol;
        BLT_MediaPort         port;
        BLT_Boolean           connected;
        union {
            BLT_OutputStreamUser    stream_user;
            BLT_InputStreamProvider stream_provider;
            BLT_PacketProducer      packet_producer;
        }                     iface;
    }                  output;
    struct StreamNode* next;
    struct StreamNode* prev;
} StreamNode;

typedef struct {
    BLT_Cardinal reference_count;
    BLT_Core     core;
    BLT_Stream   me;
    struct {
        StreamNode* head;
        StreamNode* tail;
    }            nodes;
    struct {
        BLT_CString name;
        StreamNode* node;
    }            input;
    struct {
        BLT_CString    name;
        StreamNode*    node;
        BLT_OutputNode output_node;
        BLT_TimeStamp  last_time_stamp;
    }                 output;
    ATX_Properties    properties;
    BLT_StreamInfo    info;
    BLT_EventListener event_listener;
} Stream;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Stream)
static const BLT_StreamInterface Stream_BLT_StreamInterface;
static BLT_Result Stream_ResetInfo(Stream* stream);
static BLT_Result Stream_RemoveNode(Stream* stream, StreamNode* stream_node);
static BLT_Result StreamNode_Activate(StreamNode* stream_node);
static BLT_Result StreamNode_Deactivate(StreamNode* stream_node);
static BLT_Result StreamNode_Start(StreamNode* stream_node);
static BLT_Result StreamNode_Stop(StreamNode* stream_node);

/*----------------------------------------------------------------------
|    StreamNode_Create
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Create(BLT_Stream*          stream,
                  const BLT_MediaNode* media_node, 
                  StreamNode**         stream_node)
{
    BLT_Result  result;
    StreamNode* node;

    /* activate the node */
    result = BLT_MediaNode_Activate(media_node, stream);
    if (BLT_FAILED(result)) return result;

    /* allocate memory for the node */
    node = (StreamNode*)ATX_AllocateZeroMemory(sizeof(StreamNode));
    if (node == NULL) return BLT_ERROR_OUT_OF_MEMORY;

    /* construct the node */
    node->stream = *stream;
    if (media_node) {
        node->media_node = *media_node;
    } else {
        ATX_CLEAR_OBJECT(&node->media_node);
    }
    node->state = BLT_MEDIA_NODE_STATE_IDLE;
    node->next  = NULL;
    node->prev  = NULL;
    node->flags = 0;

    /* get the input port of the node */
    ATX_CLEAR_OBJECT(&node->input.port);
    node->input.connected = BLT_FALSE;
    node->input.protocol  = BLT_MEDIA_PORT_PROTOCOL_NONE;
    result = BLT_MediaNode_GetPortByName(media_node, 
                                         "input",
                                         &node->input.port);
    if (BLT_SUCCEEDED(result)) {
        /* this is a valid input */
        BLT_MediaPort_GetProtocol(&node->input.port, &node->input.protocol);
        
        switch (node->input.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            result = ATX_CAST_OBJECT(&node->input.port,
                                     &node->input.iface.packet_consumer,
                                     BLT_PacketConsumer);
            break;

          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            result = ATX_CAST_OBJECT(&node->input.port, 
                                     &node->input.iface.stream_provider,
                                     BLT_OutputStreamProvider);
            break;

          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
            result = ATX_CAST_OBJECT(&node->input.port, 
                                     &node->input.iface.stream_user,
                                     BLT_InputStreamUser);
            break;

          default:
            result = BLT_FAILURE;
            break;
        }
        if (BLT_FAILED(result)) {
            ATX_FreeMemory((void*)node);
            return result;
        }
    }

    /* get the output port of the node */
    ATX_CLEAR_OBJECT(&node->output.port);
    node->output.connected = BLT_FALSE;
    node->output.protocol  = BLT_MEDIA_PORT_PROTOCOL_NONE;
    result = BLT_MediaNode_GetPortByName(media_node, 
                                         "output",
                                         &node->output.port);
    if (BLT_SUCCEEDED(result)) {
        /* this is a valid output */
        BLT_MediaPort_GetProtocol(&node->output.port, &node->output.protocol);
        
        switch (node->output.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            result = ATX_CAST_OBJECT(&node->output.port,
                                     &node->output.iface.packet_producer,
                                     BLT_PacketProducer);
            break;

          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            result = ATX_CAST_OBJECT(&node->output.port, 
                                     &node->output.iface.stream_user,
                                     BLT_OutputStreamUser);
            break;

          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
            result = ATX_CAST_OBJECT(&node->output.port, 
                                     &node->output.iface.stream_provider,
                                     BLT_InputStreamProvider);
            break;

          default:
            result = BLT_FAILURE;
            break;
        }
        if (BLT_FAILED(result)) {
            ATX_FreeMemory((void*)node);
            return result;
        }
    }

    /* keep a reference to the media node */
    ATX_REFERENCE_OBJECT(media_node);

    /* return the node */
    *stream_node = node;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamNode_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Destroy(StreamNode* stream_node)
{
    /* check parameters */
    if (stream_node == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    /* deactivate the node */
    StreamNode_Deactivate(stream_node);

    /* release the reference to the media node */
    ATX_RELEASE_OBJECT(&stream_node->media_node);

    /* free the memory */
    ATX_FreeMemory(stream_node);

    return BLT_SUCCESS;
}

#if defined(BLT_DEBUG)
/*----------------------------------------------------------------------
|    Stream_GetProtocolName
+---------------------------------------------------------------------*/
static const char*
Stream_GetProtocolName(BLT_MediaPortProtocol protocol)
{
    switch (protocol) {
      case BLT_MEDIA_PORT_PROTOCOL_NONE:
        return "NONE";
      case BLT_MEDIA_PORT_PROTOCOL_ANY:
        return "ANY";
      case BLT_MEDIA_PORT_PROTOCOL_PACKET:
        return "PACKET";
      case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
        return "STREAM_PULL";
      case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
        return "STREAM_PUSH";
      default:
        return "UNKNOWN";
    }
}

/*----------------------------------------------------------------------
|    Stream_GetTypeName
+---------------------------------------------------------------------*/
static const char*
Stream_GetTypeName(Stream* stream, const BLT_MediaType* type)
{
    switch (type->id) {
      case BLT_MEDIA_TYPE_ID_NONE:
        return "none";
      case BLT_MEDIA_TYPE_ID_UNKNOWN:
        return "unknown";
      case BLT_MEDIA_TYPE_ID_AUDIO_PCM:
        return "audio/pcm";
      default: {
          BLT_Result   result;
          BLT_Registry registry;
          BLT_CString  name;
          result = BLT_Core_GetRegistry(&stream->core, &registry);
          if (BLT_FAILED(result)) return "no-reg";
          result = BLT_Registry_GetNameForId(
              &registry, 
              BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS,
              type->id,
              &name);
          if (BLT_FAILED(result)) return "not-found";
          return name;
      }
    }
}
#endif /* defined(BLT_DEBUG) */

/*----------------------------------------------------------------------
|    Stream_TopologyChanged
+---------------------------------------------------------------------*/
static void
Stream_TopologyChanged(Stream*                     stream, 
                       BLT_StreamTopologyEventType type,
                       StreamNode*                 node)
{
    BLT_StreamTopologyEvent event;

    /* quick check */
    if (ATX_OBJECT_IS_NULL(&stream->event_listener)) return;

    /* send an event to the listener */
    event.type = type;
    if (node) {
        event.node = &node->media_node;
    } else {
        event.node = NULL;
    }
    BLT_EventListener_OnEvent(&stream->event_listener,
                              (ATX_Object*)&stream->me, 
                              BLT_EVENT_TYPE_STREAM_TOPOLOGY,
                              (const BLT_Event*)&event);
}

/*----------------------------------------------------------------------
|    StreamNode_Activate
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Activate(StreamNode* stream_node)
{
    BLT_Result result;

    switch (stream_node->state) {
      case BLT_MEDIA_NODE_STATE_RESET:
        /* activate the node */
        result = BLT_MediaNode_Activate(&stream_node->media_node,
                                        &stream_node->stream);
        if (BLT_FAILED(result)) return result;
        break;

      case BLT_MEDIA_NODE_STATE_IDLE:
      case BLT_MEDIA_NODE_STATE_RUNNING:
      case BLT_MEDIA_NODE_STATE_PAUSED:
        /* already activated */
        break;
    }

    /* the node is now activated */
    stream_node->state = BLT_MEDIA_NODE_STATE_IDLE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamNode_Deactivate
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Deactivate(StreamNode* stream_node)
{
    BLT_Result result;

    switch (stream_node->state) {
      case BLT_MEDIA_NODE_STATE_RESET:
        /* already deactivated */
        break;

      case BLT_MEDIA_NODE_STATE_RUNNING:
      case BLT_MEDIA_NODE_STATE_PAUSED:
        /* first, stop the node */
        result = StreamNode_Stop(stream_node);
        if (BLT_FAILED(result)) return result;

        /* then deactivate the node (FALLTHROUGH) */

      case BLT_MEDIA_NODE_STATE_IDLE:
        /* deactivate the node */
        result = BLT_MediaNode_Deactivate(&stream_node->media_node);
        if (BLT_FAILED(result)) return result;
        break;
    }

    /* the node is now deactivated */
    stream_node->state = BLT_MEDIA_NODE_STATE_RESET;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamNode_Start
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Start(StreamNode* stream_node)
{
    BLT_Result result;

    switch (stream_node->state) {
      case BLT_MEDIA_NODE_STATE_RESET:
        /* first, activate the node */
        result = StreamNode_Activate(stream_node);
        if (BLT_FAILED(result)) return result;

        /* then start the node (FALLTHROUGH) */

      case BLT_MEDIA_NODE_STATE_IDLE:
        /* start the node */
        result = BLT_MediaNode_Start(&stream_node->media_node);
        if (BLT_FAILED(result)) return result;
        break;

      case BLT_MEDIA_NODE_STATE_RUNNING:
        /* the node is already running, do nothing */
        return BLT_SUCCESS;

      case BLT_MEDIA_NODE_STATE_PAUSED:
        /* resume the node */
        result = BLT_MediaNode_Resume(&stream_node->media_node);
        if (BLT_FAILED(result)) return result;
        break;
    }

    /* the node is now running */
    stream_node->state = BLT_MEDIA_NODE_STATE_RUNNING;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamNode_Stop
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Stop(StreamNode* stream_node)
{
    BLT_Result result;

    switch (stream_node->state) {
      case BLT_MEDIA_NODE_STATE_RESET:
      case BLT_MEDIA_NODE_STATE_IDLE:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_MEDIA_NODE_STATE_RUNNING:
      case BLT_MEDIA_NODE_STATE_PAUSED:
        /* stop the node */
        result = BLT_MediaNode_Stop(&stream_node->media_node);
        if (BLT_FAILED(result)) return result;
        break;
    }

    /* the node is now idle */
    stream_node->state = BLT_MEDIA_NODE_STATE_IDLE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamNode_Pause
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Pause(StreamNode* stream_node)
{
    BLT_Result result;

    switch (stream_node->state) {
      case BLT_MEDIA_NODE_STATE_RESET:
      case BLT_MEDIA_NODE_STATE_IDLE:
        /* ignore */
        return BLT_SUCCESS;

      case BLT_MEDIA_NODE_STATE_RUNNING:
        /* pause the node */
        result = BLT_MediaNode_Pause(&stream_node->media_node);
        if (BLT_FAILED(result)) return result;
        break;

      case BLT_MEDIA_NODE_STATE_PAUSED:
        /* the node is already paused, do nothing */
        break;
    }

    /* the node is now paused */
    stream_node->state = BLT_MEDIA_NODE_STATE_PAUSED;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamNode_Seek
+---------------------------------------------------------------------*/
static BLT_Result
StreamNode_Seek(StreamNode*    stream_node,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    BLT_Result result;

    switch (stream_node->state) {
      case BLT_MEDIA_NODE_STATE_RESET:
        /* first, activate the node */
        result = StreamNode_Activate(stream_node);
        if (BLT_FAILED(result)) return result;

        /* then seek (FALLTHROUGH) */

      case BLT_MEDIA_NODE_STATE_IDLE:
      case BLT_MEDIA_NODE_STATE_RUNNING:
      case BLT_MEDIA_NODE_STATE_PAUSED:
        /* seek */
        return BLT_MediaNode_Seek(&stream_node->media_node, mode, point);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_Create
+---------------------------------------------------------------------*/
BLT_Result
Stream_Create(BLT_Core* core, BLT_Stream* object)
{
    Stream* stream;

    /* allocate memory for the object */
    stream = ATX_AllocateZeroMemory(sizeof(Stream));
    if (stream == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the object */
    stream->reference_count = 1;
    stream->core            = *core;
    ATX_Properties_Create(&stream->properties);

    /* construct reference */
    ATX_INSTANCE(object) = (BLT_StreamInstance*)stream;
    ATX_INTERFACE(object) = &Stream_BLT_StreamInterface;
    
    /* keep this object reference as a data member so that we can pass */
    /* it down as a reference to our nodes when they are created       */
    stream->me = *object;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Stream_Destroy(Stream* stream)
{
    /* destroy the nodes */
    {
        StreamNode* node = stream->nodes.head;
        while (node) {
            StreamNode* next = node->next;
            StreamNode_Destroy(node);
            node = next;
        }
    }

    /* free input name */
    if (stream->input.name) {
        ATX_FreeMemory((void*)stream->input.name);
    }

    /* free output name */
    if (stream->output.name) {
        ATX_FreeMemory((void*)stream->output.name);
    }

    /* free the stream info data */
    Stream_ResetInfo(stream);

    /* destroy the properties object */
    ATX_DESTROY_OBJECT(&stream->properties);

    /* free the object memory */
    ATX_FreeMemory(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_InsertChain
+---------------------------------------------------------------------*/
static BLT_Result 
Stream_InsertChain(Stream*     stream, 
                   StreamNode* where, 
                   StreamNode* chain)
{
    StreamNode* tail = chain;

    /* find the tail of the chain */
    while (tail->next) {
        tail = tail->next;
    }
    
    /* insert 'chain' after the 'where' node */
    if (where == NULL) {
        /* this node chain becomes the head */
        tail->next = stream->nodes.head;
        if (tail->next) {
            tail->next->prev = tail;
        }
        stream->nodes.head = chain;
    } else {
        chain->prev = where;
        tail->next  = where->next;
        if (tail->next) {
            tail->next->prev = chain;
        }
        where->next = chain;
    }
    if (where == stream->nodes.tail) {
        /* this is the new tail */
        stream->nodes.tail = tail;
    }

    /* notify that a node was added */
    Stream_TopologyChanged(stream, 
                           BLT_STREAM_TOPOLOGY_NODE_ADDED,
                           chain);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_CleanupChain
+---------------------------------------------------------------------*/
static BLT_Result 
Stream_CleanupChain(Stream* stream) 
{
    /* remove all transient nodes */
    StreamNode* node = stream->nodes.head;
    while (node) {
        StreamNode* next = node->next;
        if (node->flags & BLT_STREAM_NODE_FLAG_TRANSIENT) {
            Stream_RemoveNode(stream, node);
        }
        node = next;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_RemoveNode
+---------------------------------------------------------------------*/
static BLT_Result 
Stream_RemoveNode(Stream* stream, StreamNode* stream_node)
{
    /* quick check */
    if (stream_node == NULL) {
        return BLT_SUCCESS;
    }

    /* relink the chain */
    if (stream_node->prev) {
        stream_node->prev->next = stream_node->next;
        stream_node->prev->output.connected = BLT_FALSE;
    } 
    if (stream_node->next) {
        stream_node->next->prev = stream_node->prev;
        stream_node->next->input.connected = BLT_FALSE;
    }
    if (stream->nodes.head == stream_node) {
        stream->nodes.head =  stream_node->next;
    }
    if (stream->nodes.tail == stream_node) {
        stream->nodes.tail =  stream_node->prev;
    }

    /* notify that the topology has changed */
    stream_node->next = stream_node->prev = NULL;
    Stream_TopologyChanged(stream, 
                           BLT_STREAM_TOPOLOGY_NODE_REMOVED, 
                           stream_node);

    /* destroy the removed node */
    StreamNode_Destroy(stream_node);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_ResetInfo
+---------------------------------------------------------------------*/
static BLT_Result
Stream_ResetInfo(Stream* stream)
{
    /* clear the stream info */
    if (stream->info.data_type) {
        ATX_FreeMemory((void*)stream->info.data_type);
	stream->info.data_type = NULL;
    }
    ATX_SetMemory(&stream->info, 0, sizeof(stream->info));

    /* reset the time stamp */
    BLT_TimeStamp_Set(stream->output.last_time_stamp, 0, 0);

    return BLT_SUCCESS;
} 

/*----------------------------------------------------------------------
|    Stream_ResetProperties
+---------------------------------------------------------------------*/
static BLT_Result
Stream_ResetProperties(Stream* stream)
{
    /* reset the properies object */
    return ATX_Properties_Clear(&stream->properties);
} 

/*----------------------------------------------------------------------
|    Stream_SetEventListener
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_SetEventListener(BLT_StreamInstance*      instance, 
                        const BLT_EventListener* listener)
{
    Stream* stream = (Stream*)instance;

    /* keep the object reference */
    stream->event_listener = *listener;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_ResetInputNode
+---------------------------------------------------------------------*/
static BLT_Result
Stream_ResetInputNode(Stream* stream)
{
    /* reset the stream info */
    Stream_ResetInfo(stream);

    /* reset the properties */
    Stream_ResetProperties(stream);

    /* clear the name */
    if (stream->input.name) {
        ATX_FreeMemory((void*)stream->input.name);
        stream->input.name = NULL;
    }

    /* remove the current input */
    if (stream->input.node) {
        Stream_RemoveNode(stream, stream->input.node);
        stream->input.node = NULL;

        /* cleanup the chain */
        Stream_CleanupChain(stream);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_ResetInput
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_ResetInput(BLT_StreamInstance* instance)
{
    Stream* stream = (Stream*)instance;

    /* reset the input node */
    Stream_ResetInputNode(stream);

    /* notify of this new info */
    if (!ATX_OBJECT_IS_NULL(&stream->event_listener)) {
        BLT_StreamInfoEvent event;

        event.update_mask = BLT_STREAM_INFO_MASK_ALL;
        event.info        = stream->info;
        
        BLT_EventListener_OnEvent(&stream->event_listener, 
                                  (ATX_Object*)&stream->me, 
                                  BLT_EVENT_TYPE_STREAM_INFO,
                                  (const BLT_Event*)&event);
    }   

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_SetInputNode
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_SetInputNode(BLT_StreamInstance*  instance, 
		            BLT_CString          name,
                    const BLT_MediaNode* node)

{
    Stream*     stream = (Stream*)instance;
    StreamNode* stream_node;
    BLT_Result  result;

    /* check parameters */
    if (instance == NULL || node == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* reset the current stream */
    Stream_ResetInputNode(stream);

    /* create a stream node to represent the media node */
    result = StreamNode_Create(&stream->me, node, &stream_node);
    if (BLT_FAILED(result)) return result;

    /* copy the name */
    stream->input.name = ATX_DuplicateString(name);

    /* install the new input */
    stream->input.node = stream_node;
    Stream_InsertChain(stream, NULL, stream_node);
        
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_SetInput
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_SetInput(BLT_StreamInstance* instance, 
                BLT_CString         name, 
                BLT_CString         type)
{
    Stream*                  stream = (Stream*)instance;
    BLT_MediaNode            media_node;
    BLT_MediaType            input_media_type;
    BLT_MediaType            output_media_type;
    BLT_MediaNodeConstructor constructor;
    BLT_Result               result;

    /* check parameters */
    if (instance == NULL || name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* normalize type */
    if (type && type[0] == '\0') type = NULL;

    /* ask the core to create the corresponding input node */
    constructor.spec.input.protocol  = BLT_MEDIA_PORT_PROTOCOL_NONE;
    constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_ANY;
    constructor.name                 = name;
    BLT_MediaType_Init(&input_media_type,  BLT_MEDIA_TYPE_ID_NONE);
    BLT_MediaType_Init(&output_media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
    constructor.spec.output.media_type = &output_media_type;
    constructor.spec.input.media_type  = &input_media_type;
    if (type != NULL) {
        BLT_Registry registry;
        BLT_UInt32   id;

        result = BLT_Core_GetRegistry(&stream->core, &registry);
        if (BLT_FAILED(result)) return result;
        
        result = BLT_Registry_GetIdForName(
           &registry, 
           BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS, 
           type, 
            &id);
        if (BLT_FAILED(result)) return result;
        output_media_type.id = id;
    }

    /* create the input media node */
    result = BLT_Core_CreateCompatibleMediaNode(&stream->core, 
                                                &constructor, 
                                                &media_node);
    if (BLT_FAILED(result)) return result;

    /* set the media node as the new input */
    result = Stream_SetInputNode(instance, name, &media_node);
    ATX_RELEASE_OBJECT(&media_node);
    if (BLT_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetInputNode
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_GetInputNode(BLT_StreamInstance* instance, BLT_MediaNode* node)
{
    Stream* stream = (Stream*)instance;

    /* check parameters */
    if (instance == NULL || node == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* return the input media node */
    if (stream->input.node != NULL) {
        *node = stream->input.node->media_node;
    } else {
        ATX_CLEAR_OBJECT(node);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_ResetOutput
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_ResetOutput(BLT_StreamInstance* instance)
{
    Stream* stream = (Stream*)instance;

    /* clear the name */
    if (stream->output.name) {
        ATX_FreeMemory((void*)stream->output.name);
        stream->output.name = NULL;
    }

    /* remove the previous output */
    if (stream->output.node) {
        Stream_RemoveNode(stream, stream->output.node);
        stream->output.node = NULL;

        /* cleanup the chain */
        Stream_CleanupChain(stream);
    }

    /* reset the time stamp */
    BLT_TimeStamp_Set(stream->output.last_time_stamp, 0, 0);

    return BLT_SUCCESS;
} 

/*----------------------------------------------------------------------
|    Stream_SetOutputNode
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_SetOutputNode(BLT_StreamInstance*  instance, 
		             BLT_CString          name,
                     const BLT_MediaNode* node)
{
    Stream*     stream = (Stream*)instance;
    StreamNode* stream_node;
    BLT_Result  result;

    /* check parameters */
    if (instance == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* reset the current output */
    Stream_ResetOutput(instance);

    /* create a stream node to represent the media node */
    result = StreamNode_Create(&stream->me, node, &stream_node);
    if (BLT_FAILED(result)) return result;

    /* get the BLT_OutputNode interface */
    if (BLT_FAILED(ATX_CAST_OBJECT(&stream_node->media_node, 
                                   &stream->output.output_node,
                                   BLT_OutputNode))) {
        ATX_CLEAR_OBJECT(&stream->output.output_node);
    }

    /* copy the name */
    stream->output.name = ATX_DuplicateString(name);

    /* install the new output */
    stream->output.node = stream_node;
    Stream_InsertChain(stream, stream->nodes.tail, stream_node);
        
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_SetOutput
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_SetOutput(BLT_StreamInstance* instance, 
                 BLT_CString         name,
                 BLT_CString         type)
{
    Stream*                  stream = (Stream*)instance;
    BLT_MediaNode            media_node;
    BLT_MediaType            input_media_type;
    BLT_MediaType            output_media_type;
    BLT_MediaNodeConstructor constructor;
    BLT_Result               result;

    /* check parameters */
    if (instance == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* normalize type */
    if (type && type[0] == '\0') type = NULL;

    /* default output */
    if (name == NULL) {
        BLT_CString default_name;
        BLT_Builtins_GetDefaultOutput(&default_name, NULL);
        name = default_name;

        /* default output */
        if (type == NULL) {
            BLT_CString default_type;
            BLT_Builtins_GetDefaultOutput(NULL, &default_type);
            type = default_type;
        }
    }

    /* ask the core to create the corresponding output node */
    constructor.spec.input.protocol  = BLT_MEDIA_PORT_PROTOCOL_ANY;
    constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_NONE;
    constructor.name                 = name;
    BLT_MediaType_Init(&input_media_type,  BLT_MEDIA_TYPE_ID_UNKNOWN);
    BLT_MediaType_Init(&output_media_type, BLT_MEDIA_TYPE_ID_NONE);
    constructor.spec.output.media_type = &output_media_type;
    constructor.spec.input.media_type  = &input_media_type;
    if (type != NULL) {
        BLT_Registry registry;
        BLT_UInt32   id;

        result = BLT_Core_GetRegistry(&stream->core, &registry);
        if (BLT_FAILED(result)) return result;
        
        result = BLT_Registry_GetIdForName(
           &registry, 
           BLT_REGISTRY_NAME_CATEGORY_MEDIA_TYPE_IDS, 
           type, 
            &id);
        if (BLT_FAILED(result)) return result;
        input_media_type.id = id;
    }
    result = BLT_Core_CreateCompatibleMediaNode(&stream->core, 
                                                &constructor, 
                                                &media_node);
    if (BLT_FAILED(result)) return result;

    /* set the media node as the new output node */
    result = Stream_SetOutputNode(instance, name, &media_node);
    ATX_RELEASE_OBJECT(&media_node);
    if (BLT_FAILED(result)) return result;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetOutputNode
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_GetOutputNode(BLT_StreamInstance* instance, BLT_MediaNode* node)
{
    Stream* stream = (Stream*)instance;

    /* check parameters */
    if (instance == NULL || node == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* return the output media node */
    if (stream->output.node != NULL) {
        *node = stream->output.node->media_node;
    } else {
        ATX_CLEAR_OBJECT(node);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_FindNode
+---------------------------------------------------------------------*/
static StreamNode*
Stream_FindNode(Stream* stream, const BLT_MediaNode* media_node)
{
    StreamNode* node = stream->nodes.head;
    while (node) {
        if (ATX_INSTANCE(&node->media_node) == ATX_INSTANCE(media_node)) {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

/*----------------------------------------------------------------------
|    Stream_AddNode
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_AddNode(BLT_StreamInstance*  instance, 
               const BLT_MediaNode* where,
               const BLT_MediaNode* node)
{
    Stream*     stream = (Stream*)instance;
    StreamNode* stream_node;
    StreamNode* insert_point;
    BLT_Result  result;

    /* check parameters */
    if (instance == NULL || node == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* find a place to insert the node */
    if (where) {
        insert_point = Stream_FindNode(stream, where);
    } else {
        /* a NULL insert point means just before the output */
        if (stream->output.node) {
            insert_point = stream->output.node->prev;
        } else {
            insert_point = NULL;
        }
    }

    /* create a stream node to represent the media node */
    result = StreamNode_Create(&stream->me, node, &stream_node);
    if (BLT_FAILED(result)) return result;

    /* insert the node in the chain */
    return Stream_InsertChain(stream, insert_point, stream_node);
}

/*----------------------------------------------------------------------
|    Stream_AddNodeByName
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_AddNodeByName(BLT_StreamInstance*  instance, 
                     const BLT_MediaNode* where,
                     BLT_CString          name)
{
    Stream*                  stream = (Stream*)instance;
    BLT_MediaNode            media_node;
    BLT_MediaType            input_media_type;
    BLT_MediaType            output_media_type;
    BLT_MediaNodeConstructor constructor;
    BLT_Result               result;

    /* check parameters */
    if (instance == NULL || name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* ask the core to create the media node */
    constructor.spec.input.protocol  = BLT_MEDIA_PORT_PROTOCOL_ANY;
    constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_ANY;
    constructor.name                 = name;
    BLT_MediaType_Init(&input_media_type,  BLT_MEDIA_TYPE_ID_UNKNOWN);
    BLT_MediaType_Init(&output_media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
    constructor.spec.output.media_type = &output_media_type;
    constructor.spec.input.media_type  = &input_media_type;
    result = BLT_Core_CreateCompatibleMediaNode(&stream->core, 
                                                &constructor, 
                                                &media_node);
    if (BLT_FAILED(result)) return result;

    /* add the node to the stream */
    result = Stream_AddNode(instance, where, &media_node);
    ATX_RELEASE_OBJECT(&media_node);
    if (BLT_FAILED(result)) {
        return result;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetStreamNodeInfo
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_GetStreamNodeInfo(BLT_StreamInstance*  instance,
                         const BLT_MediaNode* node,
                         BLT_StreamNodeInfo*  info)
{
    Stream*     stream = (Stream*)instance;
    StreamNode* stream_node;

    /* check parameters */
    if (instance == NULL || node == NULL || info == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* find the node */
    stream_node = Stream_FindNode(stream, node);
    if (stream_node == NULL) {
        return BLT_ERROR_NO_SUCH_MEDIA_NODE;
    }

    /* return the info */
    info->media_node       = stream_node->media_node;
    info->flags            = stream_node->flags;
    info->input.connected  = stream_node->input.connected;
    info->input.protocol   = stream_node->input.protocol;
    info->output.connected = stream_node->output.connected;
    info->output.protocol  = stream_node->output.protocol;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetFirstNode
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_GetFirstNode(BLT_StreamInstance* instance,
                    BLT_MediaNode*      node)
{
    Stream* stream = (Stream*)instance;

    /* check parameters */
    if (instance == NULL || node == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* return the first media node, if any */
    if (stream->nodes.head) {
        *node = stream->nodes.head->media_node;
    } else {
        ATX_CLEAR_OBJECT(node);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetNextNode
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_GetNextNode(BLT_StreamInstance* instance,
                   BLT_MediaNode*      node,
                   BLT_MediaNode*      next)
{
    Stream*     stream = (Stream*)instance;
    StreamNode* stream_node;

    /* check parameters */
    if (instance == NULL || node == NULL || next == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* find the node */
    stream_node = Stream_FindNode(stream, node);
    if (stream_node == NULL) {
        ATX_CLEAR_OBJECT(next);
        return BLT_ERROR_NO_SUCH_MEDIA_NODE;
    }

    /* return the next media node */
    if (stream_node->next) {
        *next = stream_node->next->media_node;
    } else {
        ATX_CLEAR_OBJECT(next);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_ConnectNodes
+---------------------------------------------------------------------*/
static void
Stream_ConnectNodes(Stream* stream, StreamNode* from, StreamNode* to)
{
    STREAM_LOG(0, ("Stream::ConnectNodes - connected\n"));
    from->output.connected = BLT_TRUE;
    to->input.connected    = BLT_TRUE;
    /* only notify of the connection of the 'from' node, as the other */
    /* one can be found by following the chain                        */
    Stream_TopologyChanged(stream, BLT_STREAM_TOPOLOGY_NODE_CONNECTED, from);
}

#define DBG_TRYING													      \
STREAM_LOG(0, ("  Trying from %s:%s to %s:%s\n", 				    	  \
        Stream_GetProtocolName(constructor.spec.input.protocol),	      \
        Stream_GetTypeName(stream, constructor.spec.input.media_type),	  \
        Stream_GetProtocolName(constructor.spec.output.protocol),	      \
        Stream_GetTypeName(stream, constructor.spec.output.media_type)))  \
        
/*----------------------------------------------------------------------
|    Stream_CreateCompatibleMediaNode
+---------------------------------------------------------------------*/
static BLT_Result
Stream_CreateCompatibleMediaNode(Stream*              stream, 
                                 StreamNode*          from_node,
                                 const BLT_MediaType* from_type,
                                 StreamNode*          to_node,
                                 BLT_MediaNode*       media_node)
{
    BLT_MediaNodeConstructor constructor;
    BLT_MediaType            output_media_type;
    BLT_Result               result;
    
    /* setup the constructor */
    constructor.spec.input.protocol   = from_node->output.protocol;
    constructor.spec.input.media_type = from_type;
    constructor.name                  = NULL;

    STREAM_LOG(0, ("Stream::CreateCompatibleMediaNode trying to create compatible node:\n"));

    /* first, try to join the to_node */
    if (to_node) {
		/* first try with an expected type and the requested protocol */
        BLT_Ordinal index = 0;
        constructor.spec.output.protocol = to_node->input.protocol;
        for (;;) {
            /* get the 'nth' media type expected by the port */
            constructor.spec.output.media_type = from_type;
            result = BLT_MediaPort_QueryMediaType(
                &to_node->input.port, index, 
                &constructor.spec.output.media_type);
            if (BLT_FAILED(result)) break;

			DBG_TRYING;
            result = BLT_Core_CreateCompatibleMediaNode(&stream->core,
                                                        &constructor,
                                                        media_node);
            if (BLT_SUCCEEDED(result)) return BLT_SUCCESS;

            /* try the next type */
            index++;
        }
        
		/* next try with an expected type and any protocol */
        index = 0;
        constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_ANY;
        for (;;) {
            /* get the 'nth' media type expected by the port */
            constructor.spec.output.media_type = from_type;
            result = BLT_MediaPort_QueryMediaType(
                &to_node->input.port, index, 
                &constructor.spec.output.media_type);
            if (BLT_FAILED(result)) break;

			DBG_TRYING;                  
            result = BLT_Core_CreateCompatibleMediaNode(&stream->core,
                                                        &constructor,
                                                        media_node);
            if (BLT_SUCCEEDED(result)) return BLT_SUCCESS;

            /* try the next type */
            index++;
        }
    }

	/* use a locally-allocated media type */
    constructor.spec.output.media_type = &output_media_type;

    /* try with any type and any protocol */
    constructor.spec.output.protocol = BLT_MEDIA_PORT_PROTOCOL_ANY;
    BLT_MediaType_Init(&output_media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
	DBG_TRYING;                  
    result = BLT_Core_CreateCompatibleMediaNode(&stream->core,
                                                &constructor,
                                                media_node);
    if (BLT_SUCCEEDED(result)) return BLT_SUCCESS;

    return BLT_ERROR_STREAM_NO_COMPATIBLE_NODE;
}
                      
/*----------------------------------------------------------------------
|    Stream_InterpolateChain
+---------------------------------------------------------------------*/
static BLT_Result
Stream_InterpolateChain(Stream*              stream, 
                        StreamNode*          from_node, 
                        const BLT_MediaType* from_type,
                        StreamNode*          to_node,
                        StreamNode**         new_node)
{
    BLT_MediaNode media_node;
    BLT_Result    result;
        
    /* default value */
    *new_node = NULL;

    /* find a compatible media node to be next */
    result = Stream_CreateCompatibleMediaNode(stream, 
                                              from_node, 
                                              from_type,
                                              to_node,
                                              &media_node);
    if (BLT_FAILED(result)) return result;

    /* create a stream node to represent the media node */
    result = StreamNode_Create(&stream->me, &media_node, new_node);
    ATX_RELEASE_OBJECT(&media_node);
    if (BLT_FAILED(result)) return result;
    (*new_node)->flags |= BLT_STREAM_NODE_FLAG_TRANSIENT;

    /* add the new node to the tail of the chain */
    return Stream_InsertChain(stream, from_node, *new_node);
}

/*----------------------------------------------------------------------
|    Stream_DeliverPacket
+---------------------------------------------------------------------*/
static BLT_Result
Stream_DeliverPacket(Stream*          stream, 
                     BLT_MediaPacket* packet, 
                     StreamNode*      from_node)
{
    StreamNode*          new_node;
    StreamNode*          to_node;
    const BLT_MediaType* media_type;
    BLT_Result           result;

    if (BLT_MediaPacket_GetFlags(packet)) {
        STREAM_LOG(0, 
                   ("Stream::DeliverPacket - flags = %x --------------------\n", 
                    BLT_MediaPacket_GetFlags(packet)));
    }
    
    /* set the recipient node */
    to_node = from_node->next;

    /* if we're connected, we can only try once */
    if (from_node->output.connected == BLT_TRUE) {
        result = BLT_PacketConsumer_PutPacket(
            &to_node->input.iface.packet_consumer, packet);
        goto done;
    } 
    
    /* check if the recipient uses the PACKET protocol */
    if (to_node->input.protocol == BLT_MEDIA_PORT_PROTOCOL_PACKET) {
        /* try to deliver the packet to the recipient */
        result = BLT_PacketConsumer_PutPacket(
            &to_node->input.iface.packet_consumer, packet);
        if (BLT_SUCCEEDED(result) || 
	    result != BLT_ERROR_INVALID_MEDIA_FORMAT) {
            /* success, or fatal error */
            goto done;
        }
    } 

    /* get the media type of the packet */
    BLT_MediaPacket_GetMediaType(packet, &media_type);

    /* try to create a compatible node to receive the packet */
    result = Stream_InterpolateChain(stream, 
                                     from_node, 
                                     media_type, 
                                     to_node, 
                                     &new_node);
    if (BLT_FAILED(result)) goto done;

    /* check that the new node has the right interface */
    if (new_node->input.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET) {
        result = BLT_ERROR_INTERNAL;
        goto done;
    }

    /* try to deliver the packet */
    to_node = new_node;
    result = BLT_PacketConsumer_PutPacket(&to_node->input.iface.packet_consumer, 
                                          packet);

done:
    if (BLT_SUCCEEDED(result)) {
        if (to_node == stream->output.node) {
            /* if the packet has been delivered to the output, keep */
            /* its timestamp                                        */
            stream->output.last_time_stamp = 
                BLT_MediaPacket_GetTimeStamp(packet);
        }
        if (from_node->output.connected == BLT_FALSE) {
            /* we're now connected */
            Stream_ConnectNodes(stream, from_node, to_node);
        }
    }
        
    /* do not keep the reference to the packet */
    BLT_MediaPacket_Release(packet);

    return result;
}

/*----------------------------------------------------------------------
|    Stream_SetupByteStreams
+---------------------------------------------------------------------*/
static BLT_Result
Stream_SetupByteStreams(Stream*     stream, 
                        StreamNode* from_node, 
                        StreamNode* to_node)
{
    ATX_InputStream      input_stream  = ATX_NULL_OBJECT;
    ATX_OutputStream     output_stream = ATX_NULL_OBJECT;
    BLT_MediaType        unknown_media_type;
    const BLT_MediaType* media_type = NULL;
    BLT_Result           result = BLT_SUCCESS;

    /* get the 'from' node's stream type */
    result = BLT_MediaPort_QueryMediaType(
        &from_node->output.port,
        0,
        &media_type);
    if (BLT_FAILED(result)) {
        BLT_MediaType_Init(&unknown_media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
        media_type = &unknown_media_type;
    }

    switch (from_node->output.protocol) {
      case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
        /* get the stream from the from_node */
        result = BLT_InputStreamProvider_GetStream(
            &from_node->output.iface.stream_provider, 
            &input_stream);
        if (BLT_FAILED(result)) goto done;

        /* connect the stream to the to_node */
        result = BLT_InputStreamUser_SetStream(
            &to_node->input.iface.stream_user,
            &input_stream,
            media_type);
        break;

      case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
        /* get the stream from to_node */
        result = BLT_OutputStreamProvider_GetStream(
            &to_node->input.iface.stream_provider,
            &output_stream,
            media_type);
        if (BLT_FAILED(result)) goto done;

        /* connect the stream to from_node */
        result = BLT_OutputStreamUser_SetStream(
            &from_node->output.iface.stream_user,
            &output_stream);
        break;

      default:
        return BLT_ERROR_INTERNAL;
    }

 done:
    /* release the streams */
    ATX_RELEASE_OBJECT(&input_stream);
    ATX_RELEASE_OBJECT(&output_stream);

    /* if we were successful, we're connected */
    if (BLT_SUCCEEDED(result)) {
        Stream_ConnectNodes(stream, from_node, to_node);
    }

    return result;
}

/*----------------------------------------------------------------------
|    Stream_ConnectPort
+---------------------------------------------------------------------*/
static BLT_Result
Stream_ConnectPort(Stream*     stream, 
                   StreamNode* from_node, 
                   StreamNode* to_node)
{
    StreamNode*          new_node;
    ATX_InputStream      input_stream  = ATX_NULL_OBJECT;
    ATX_OutputStream     output_stream = ATX_NULL_OBJECT;
    const BLT_MediaType* media_type = NULL;
    BLT_Result           result = BLT_SUCCESS;

    /* if the protocols match, try to do a stream setup */
    if (from_node->output.protocol == to_node->input.protocol) {
        result = Stream_SetupByteStreams(stream, from_node, to_node);
        if (BLT_SUCCEEDED(result)) return BLT_SUCCESS;
    }
    
    /* get the media type of the 'from' node's output port */
    result = BLT_MediaPort_QueryMediaType(
        &from_node->output.port,
        0,
        &media_type);
    if (BLT_FAILED(result)) goto done;

    /* create a new node by interpolating the chain */
    result = Stream_InterpolateChain(stream, 
                                     from_node, 
                                     media_type, 
                                     to_node, 
                                     &new_node);
    if (BLT_FAILED(result)) goto done;

    /* use the new node as the to_node */
    to_node = new_node;

    /* check that the protocols match */
    if (from_node->output.protocol != to_node->input.protocol) {
        StreamNode_Destroy(new_node);
        result = BLT_ERROR_INTERNAL;
        goto done;
    }

    /* perform the connection if we're ready for it */
    switch (from_node->output.protocol) {
      case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
        /* get the stream from the from_node */
        result = BLT_InputStreamProvider_GetStream(
            &from_node->output.iface.stream_provider,
            &input_stream);
        if (BLT_FAILED(result)) goto done;

        /* connect the stream to the to_node */
        result = BLT_InputStreamUser_SetStream(
            &to_node->input.iface.stream_user,
            &input_stream,
            media_type);
        if (BLT_FAILED(result)) goto done;
        break;

      case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
        /* get the stream from the to_node */
        result = BLT_OutputStreamProvider_GetStream(
            &to_node->input.iface.stream_provider,
            &output_stream,
            media_type);
        if (BLT_FAILED(result)) goto done;

        /* connect the stream to from_node */
        result = BLT_OutputStreamUser_SetStream(
            &from_node->output.iface.stream_user,
            &output_stream);
        if (BLT_FAILED(result)) goto done;
        break;

      default:
        result = BLT_ERROR_INTERNAL;
        goto done;
    }

    /* we're connected ! */
    Stream_ConnectNodes(stream, from_node, to_node);

 done:
    ATX_RELEASE_OBJECT(&input_stream);
    ATX_RELEASE_OBJECT(&output_stream);
    return result;
}

/*----------------------------------------------------------------------
|    Stream_PumpPacket
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_PumpPacket(BLT_StreamInstance* instance)
{
    Stream*          stream = (Stream*)instance;
    StreamNode*      node;
    BLT_MediaPacket* packet;
    BLT_Result       result;

    /* check that we have an input and an output */
    if (stream->input.node  == NULL ||
        stream->output.node == NULL) {
        return BLT_FAILURE;
    }

    /* get the next available packet */
    node = stream->nodes.tail;
    while (node) {
        /* ensure that the node is started */
        result = StreamNode_Start(node);
        if (BLT_FAILED(result)) return result;

        switch (node->output.protocol) {
          case BLT_MEDIA_PORT_PROTOCOL_NONE:
            /* skip that node */
            break;

          case BLT_MEDIA_PORT_PROTOCOL_PACKET:
            /* get a packet from the node's output port */
            result = BLT_PacketProducer_GetPacket(
                &node->output.iface.packet_producer,
                &packet);
            if (BLT_SUCCEEDED(result) && packet != NULL) {
                return Stream_DeliverPacket(stream, packet, node);
            } else {
                if (result != BLT_ERROR_PORT_HAS_NO_DATA) {
                    return result;
                }
            }
            break;

          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL:
          case BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH:
            /* ensure that the node's port is connected */
            if (!node->output.connected) {
                result = Stream_ConnectPort(stream, node, node->next);
                if (result != BLT_ERROR_PORT_HAS_NO_DATA &&
                    result != BLT_ERROR_PORT_HAS_NO_STREAM) {
                    return result;
                }
            }
            break;

          default:
            return BLT_ERROR_INTERNAL;
        }

        /* move to the previous node */
        node = node->prev;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|    Stream_Stop
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_Stop(BLT_StreamInstance* instance)
{
    Stream*     stream = (Stream*)instance;
    StreamNode* node   = stream->nodes.head;

    /* stop all the nodes */
    while (node) {
        StreamNode_Stop(node);
        node = node->next;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_Pause
+---------------------------------------------------------------------*/
BLT_METHOD 
Stream_Pause(BLT_StreamInstance* instance)
{
    Stream*     stream = (Stream*)instance;
    StreamNode* node   = stream->nodes.head;

    /* pause all the nodes */
    while (node) {
        StreamNode_Pause(node);
        node = node->next;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_SetInfo
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_SetInfo(BLT_StreamInstance*   instance, 
               const BLT_StreamInfo* info)
{
    Stream*  stream = (Stream*)instance;
    BLT_Mask update_mask = 0;

    if ((info->mask & BLT_STREAM_INFO_MASK_NOMINAL_BITRATE) &&
        stream->info.nominal_bitrate != info->nominal_bitrate) {
        stream->info.nominal_bitrate  = info->nominal_bitrate;
        update_mask |= BLT_STREAM_INFO_MASK_NOMINAL_BITRATE;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_AVERAGE_BITRATE) &&
        stream->info.average_bitrate != info->average_bitrate) {
        stream->info.average_bitrate  = info->average_bitrate;
        update_mask |= BLT_STREAM_INFO_MASK_AVERAGE_BITRATE;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_INSTANT_BITRATE) &&
        stream->info.instant_bitrate != info->instant_bitrate) {
        stream->info.instant_bitrate  = info->instant_bitrate;
        update_mask |= BLT_STREAM_INFO_MASK_INSTANT_BITRATE;
    }   
    if ((info->mask & BLT_STREAM_INFO_MASK_SIZE) &&
        stream->info.size != info->size) {
        stream->info.size  = info->size;
        update_mask |= BLT_STREAM_INFO_MASK_SIZE;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_DURATION) &&
        stream->info.duration != info->duration) {
        stream->info.duration  = info->duration;
        update_mask |= BLT_STREAM_INFO_MASK_DURATION;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_SAMPLE_RATE) &&
        stream->info.sample_rate != info->sample_rate) {
        stream->info.sample_rate  = info->sample_rate;
        update_mask |= BLT_STREAM_INFO_MASK_SAMPLE_RATE;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_CHANNEL_COUNT) &&
        stream->info.channel_count != info->channel_count) {
        stream->info.channel_count  = info->channel_count;
        update_mask |= BLT_STREAM_INFO_MASK_CHANNEL_COUNT;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_FLAGS) &&
        stream->info.flags != info->flags) {
        stream->info.flags  = info->flags;
        update_mask |= BLT_STREAM_INFO_MASK_FLAGS;
    }
    if ((info->mask & BLT_STREAM_INFO_MASK_DATA_TYPE) &&
        (stream->info.data_type == NULL ||
         info->data_type == NULL        ||
         !ATX_StringsEqual(stream->info.data_type, info->data_type))) {
        if (stream->info.data_type != NULL) {
            ATX_FreeMemory((void*)stream->info.data_type);
            stream->info.data_type = NULL;
        }
        if (info->data_type) {
            stream->info.data_type = ATX_DuplicateString(info->data_type);
        }
        update_mask |= BLT_STREAM_INFO_MASK_DATA_TYPE;
    }

    if (update_mask && !ATX_OBJECT_IS_NULL(&stream->event_listener)) {
        BLT_StreamInfoEvent event;

        event.update_mask = update_mask;
        event.info        = stream->info;
        
        BLT_EventListener_OnEvent(&stream->event_listener, 
                                  (ATX_Object*)&stream->me, 
                                  BLT_EVENT_TYPE_STREAM_INFO,
                                  (const BLT_Event*)&event);
    }   
        
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetInfo
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_GetInfo(BLT_StreamInstance* instance, BLT_StreamInfo* info)
{
    Stream* stream = (Stream*)instance;

    *info = stream->info;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_GetStatus(BLT_StreamInstance* instance, 
                 BLT_StreamStatus*   status)
{
    Stream*    stream = (Stream*)instance;
    BLT_Result result;

    /* set the stream status */
    status->time_stamp = stream->output.last_time_stamp;
    if (stream->info.duration) {
        /* estimate the position from the time stamp and duration */
        ATX_Int64 offset;
        ATX_Int64_Set_Int32(offset, status->time_stamp.seconds);
        ATX_Int64_Mul_Int32(offset, 1000);
        ATX_Int64_Add_Int32(offset, status->time_stamp.nanoseconds/1000000);
        ATX_Int64_Mul_Int32(offset, stream->info.size);
        ATX_Int64_Div_Int32(offset, stream->info.duration);
        status->position.offset = ATX_Int64_Get_Int32(offset);
        status->position.range  = stream->info.size;
    } else {
        status->position.offset = 0;
        status->position.range  = 0;
    }
    
    /* compute the output node status */
    if (ATX_OBJECT_IS_NULL(&stream->output.output_node)) {
        /* the output does not implement the BLT_OutputNode interface */
        /* so we return the time stamp of the last packet that was    */
        /* delivered to the output                                    */
        status->output_status.time_stamp = stream->output.last_time_stamp;
        BLT_TimeStamp_Set(status->output_status.delay, 0, 0);
    } else {
        /* get the output status from the output node */
        status->output_status.time_stamp = stream->output.last_time_stamp;
        result = BLT_OutputNode_GetStatus(&stream->output.output_node, 
                                          &status->output_status);
        if (BLT_FAILED(result)) {
            status->output_status.time_stamp = stream->output.last_time_stamp;
            BLT_TimeStamp_Set(status->output_status.delay, 0, 0);
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_GetProperties
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_GetProperties(BLT_StreamInstance* instance, ATX_Properties* properties)
{
    Stream* stream = (Stream*)instance;
    *properties = stream->properties;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_EstimateSeekPoint
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_EstimateSeekPoint(BLT_StreamInstance* instance, 
                         BLT_SeekMode        mode,
                         BLT_SeekPoint*      point)
{
    Stream* stream = (Stream*)instance;

    switch (mode) {
      case BLT_SEEK_MODE_IGNORE:
        return BLT_SUCCESS;

      case BLT_SEEK_MODE_BY_TIME_STAMP:
        /* estimate from the timestamp */
        if ((point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) &&
            stream->info.duration) {
            /* estimate the offset from the time stamp and duration */
            ATX_Int64 offset;
            ATX_Int64_Set_Int32(offset, point->time_stamp.seconds);
            ATX_Int64_Mul_Int32(offset, 1000);
            ATX_Int64_Add_Int32(offset, point->time_stamp.nanoseconds/1000000);
            ATX_Int64_Mul_Int32(offset, stream->info.size);
            ATX_Int64_Div_Int32(offset, stream->info.duration);
            if (!(point->mask & BLT_SEEK_POINT_MASK_OFFSET)) {
                point->offset = ATX_Int64_Get_Int32(offset);
                point->mask |= BLT_SEEK_POINT_MASK_OFFSET;
            }
                
            /* estimate the position from the offset and size */
            if (!(point->mask & BLT_SEEK_POINT_MASK_POSITION)) {
                point->mask |= BLT_SEEK_POINT_MASK_POSITION;
                point->position.offset = ATX_Int64_Get_Int32(offset);
                point->position.range  = stream->info.size;
                point->mask |= BLT_SEEK_POINT_MASK_POSITION;
            }
            break;
        } else {
            return BLT_FAILURE;
        }

      case BLT_SEEK_MODE_BY_POSITION:
        /* estimate from the position */
        if (point->mask & BLT_SEEK_POINT_MASK_POSITION) {
            /* estimate the offset from the position and size */
            ATX_Int64 offset;
            ATX_Int64_Set_Int32(offset, point->position.offset);
            ATX_Int64_Mul_Int32(offset, stream->info.size);
            ATX_Int64_Div_Int32(offset, point->position.range);
            if (!(point->mask & BLT_SEEK_POINT_MASK_OFFSET)) {
                point->offset = ATX_Int64_Get_Int32(offset);
                point->mask |= BLT_SEEK_POINT_MASK_OFFSET;
            }

            /* estimate the time stamp from the position and duration */
            if (!(point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) &&
                point->position.range) {
                ATX_Int64    time64;   
                BLT_Cardinal time;
                ATX_Int64_Set_Int32(time64, stream->info.duration);
                ATX_Int64_Mul_Int32(time64, point->position.offset);
                ATX_Int64_Div_Int32(time64, point->position.range);
                time = ATX_Int64_Get_Int32(time64);
                point->time_stamp.seconds = time/1000;
                point->time_stamp.nanoseconds = 
                    (time-1000*point->time_stamp.seconds)*1000000;
                point->mask |= BLT_SEEK_POINT_MASK_TIME_STAMP;
            }
            break;
        } else {
            return BLT_FAILURE; 
        }

      case BLT_SEEK_MODE_BY_OFFSET:
        /* estimate from the offset */
        if (point->mask & BLT_SEEK_POINT_MASK_OFFSET) {
            if (!(point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) &&
                stream->info.size) {
                /* estimate the time stamp from offset, size and duration */
                ATX_Int64    time64;
                BLT_Cardinal time;
                ATX_Int64_Set_Int32(time64, stream->info.duration);
                ATX_Int64_Mul_Int32(time64, point->offset);
                ATX_Int64_Div_Int32(time64, stream->info.size);
                time = ATX_Int64_Get_Int32(time64);
                point->time_stamp.seconds = time/1000;
                point->time_stamp.seconds = time/1000;
                point->time_stamp.nanoseconds = 
                    (time-1000*point->time_stamp.seconds)*1000000;
                point->mask |= BLT_SEEK_POINT_MASK_TIME_STAMP;
            }
            if (!(point->mask & BLT_SEEK_POINT_MASK_POSITION) &&
                stream->info.size) {
                point->position.offset = point->offset;
                point->position.range  = stream->info.size;
                point->mask |= BLT_SEEK_POINT_MASK_POSITION;
            }
            break;
        } else {
            return BLT_FAILURE;
        }

      case BLT_SEEK_MODE_BY_SAMPLE:
        /* estimate from the sample offset */
        if (point->mask & BLT_SEEK_POINT_MASK_SAMPLE) {
            if (stream->info.duration && stream->info.sample_rate) {
                /* compute position from duration and sample rate */
                ATX_Int64 samples;
                ATX_Int64 sample;
                ATX_Int64 position;
                ATX_Int64 range;
                ATX_Int64 offset;
                ATX_Int64_Set_Int32(samples, stream->info.duration);
                ATX_Int64_Mul_Int32(samples, stream->info.sample_rate);
                ATX_Int64_Div_Int32(samples, 1000);
                range    = samples;
                position = point->sample;
                ATX_Int64_Div_Int32(position, 100);
                ATX_Int64_Div_Int32(range,    100);
                point->position.offset = ATX_Int64_Get_Int32(position);
                point->position.range  = ATX_Int64_Get_Int32(range);
                point->mask |= BLT_SEEK_POINT_MASK_POSITION;

                /* compute offset from size and duration */
                sample = point->sample;
                ATX_Int64_Div_Int32(sample, 100);
                ATX_Int64_Div_Int32(samples, 100);
                ATX_Int64_Set_Int32(offset, stream->info.size);
                ATX_Int64_Mul_Int64(offset, sample);
                ATX_Int64_Div_Int64(offset, samples);
                point->offset = ATX_Int64_Get_Int32(offset);
                point->mask |= BLT_SEEK_POINT_MASK_OFFSET;
                
                /* compute time stamp */
                {
                    ATX_Int64    msecs64 = point->sample;
                    ATX_Cardinal msecs;
                    ATX_Int64_Mul_Int32(msecs64, 1000);
                    ATX_Int64_Div_Int32(msecs64, stream->info.sample_rate);
                    msecs = ATX_Int64_Get_Int32(msecs64);
                    point->time_stamp.seconds     = msecs/1000;
                    point->time_stamp.nanoseconds = 
                        1000000*(msecs-1000*(point->time_stamp.seconds));
                    point->mask |= BLT_SEEK_POINT_MASK_TIME_STAMP;
                }
            }
            break;
        } else {
            return BLT_FAILURE;
        }
    }

    /* estimate the sample offset from the time stamp and sample rate */
    if (point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP &&
        !(point->mask & BLT_SEEK_POINT_MASK_SAMPLE) &&
        stream->info.sample_rate) {
        ATX_Int64 sample;
        ATX_Int64_Set_Int32(sample, point->time_stamp.seconds*1000);
        ATX_Int64_Add_Int32(sample, point->time_stamp.nanoseconds/1000000);
        ATX_Int64_Mul_Int32(sample, stream->info.sample_rate);
        ATX_Int64_Div_Int32(sample, 1000);
        point->sample = sample;
        point->mask |= BLT_SEEK_POINT_MASK_SAMPLE;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Stream_Seek
+---------------------------------------------------------------------*/
static BLT_Result
Stream_Seek(Stream*        stream, 
            BLT_SeekMode*  mode,
            BLT_SeekPoint* point)
{
    StreamNode* node = stream->nodes.tail;
    BLT_Result  result;

    /* go through all the nodes in reverse order */
    while (node) {
        /* tell the node to seek */
        result = StreamNode_Seek(node, mode, point);
        if (BLT_FAILED(result)) return result;

        /* move to the previous node */
        node = node->prev;
    }

    /* keep a copy of the time stamp as our last time stamt */
    if (point->mask & BLT_SEEK_POINT_MASK_TIME_STAMP) {
        stream->output.last_time_stamp = point->time_stamp;
    }

    return BLT_SUCCESS;
}

/*---------------------------------------------------------------------
|    Stream_SeekToTime
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_SeekToTime(BLT_StreamInstance* instance, BLT_Cardinal time)
{
    Stream*       stream = (Stream*)instance;
    BLT_SeekPoint point;
    BLT_SeekMode  mode;

    /* setup the seek request */
    ATX_SetMemory(&point, 0, sizeof(point));
    mode       = BLT_SEEK_MODE_BY_TIME_STAMP;
    point.mask = BLT_SEEK_POINT_MASK_TIME_STAMP;
    point.time_stamp.seconds     = time/1000;
    point.time_stamp.nanoseconds = 
        (time-(point.time_stamp.seconds*1000))*1000000;

    /* fill in the other seek point fields */
    Stream_EstimateSeekPoint(instance, mode, &point);

    /* perform the seek */
    return Stream_Seek(stream, &mode, &point);
}

/*----------------------------------------------------------------------
|    Stream_SeekToPosition
+---------------------------------------------------------------------*/
BLT_METHOD
Stream_SeekToPosition(BLT_StreamInstance* instance, 
                      BLT_Size            offset,
                      BLT_Size            range)
{
    Stream*       stream = (Stream*)instance;
    BLT_SeekPoint point;
    BLT_SeekMode  mode;

    /* sanitize the parameters */
    if (offset > range) offset = range;

    /* setup the seek request */
    ATX_SetMemory(&point, 0, sizeof(point));
    mode       = BLT_SEEK_MODE_BY_POSITION;
    point.mask = BLT_SEEK_POINT_MASK_POSITION;
    point.position.offset = offset;
    point.position.range  = range;

    /* fill in the other seek point fields */
    Stream_EstimateSeekPoint(instance, mode, &point);

    /* perform the seek */
    return Stream_Seek(stream, &mode, &point);
}

/*----------------------------------------------------------------------
|    BLT_Stream interface
+---------------------------------------------------------------------*/
static const BLT_StreamInterface
Stream_BLT_StreamInterface = {
    Stream_GetInterface,
    Stream_SetEventListener,
    Stream_ResetInput,
    Stream_SetInput,
    Stream_SetInputNode,
    Stream_GetInputNode,
    Stream_ResetOutput,
    Stream_SetOutput,
    Stream_SetOutputNode,
    Stream_GetOutputNode,
    Stream_AddNode,
    Stream_AddNodeByName,
    Stream_GetStreamNodeInfo,
    Stream_GetFirstNode,
    Stream_GetNextNode,
    Stream_PumpPacket,
    Stream_Stop,
    Stream_Pause,
    Stream_SetInfo,
    Stream_GetInfo,
    Stream_GetStatus,
    Stream_GetProperties,
    Stream_EstimateSeekPoint,
    Stream_SeekToTime,
    Stream_SeekToPosition
};

/*----------------------------------------------------------------------
|    Stream_OnEvent
+---------------------------------------------------------------------*/
BLT_VOID_METHOD
Stream_OnEvent(BLT_EventListenerInstance* instance,
               const ATX_Object*          source,
               BLT_EventType              type,
               const BLT_Event*           event)
{
    Stream* stream = (Stream*)instance;

    if (!ATX_OBJECT_IS_NULL(&stream->event_listener)) {
        BLT_EventListener_OnEvent(&stream->event_listener, 
                                  source, type, event);
    }
}

/*----------------------------------------------------------------------
|    BLT_EventListener interface
+---------------------------------------------------------------------*/
static const BLT_EventListenerInterface
Stream_BLT_EventListenerInterface = {
    Stream_GetInterface,
    Stream_OnEvent
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(Stream, reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Stream)
ATX_INTERFACE_MAP_ADD(Stream, BLT_Stream)
ATX_INTERFACE_MAP_ADD(Stream, BLT_EventListener)
ATX_INTERFACE_MAP_ADD(Stream, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Stream)


