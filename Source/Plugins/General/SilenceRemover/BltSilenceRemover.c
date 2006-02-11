/*****************************************************************
|
|      File: BltSilenceRemover.c
|
|      Silence Remover Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "Fluo.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltSilenceRemover.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverModule)
static const BLT_ModuleInterface SilenceRemoverModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemover)
static const BLT_MediaNodeInterface SilenceRemover_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverInputPort)

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverOutputPort)

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_SILENCE_REMOVER_THRESHOLD 64

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} SilenceRemoverModule;

typedef struct {
    BLT_MediaPacket* pending;
} SilenceRemoverInputPort;

typedef struct {
    ATX_List* packets;
} SilenceRemoverOutputPort;

typedef enum {
    SILENCE_REMOVER_STATE_START_OF_STREAM,
    SILENCE_REMOVER_STATE_IN_STREAM
} SilenceRemoverState;

typedef struct {
    BLT_BaseMediaNode        base;
    BLT_Stream               context;
    SilenceRemoverState      state;
    SilenceRemoverInputPort  input;
    SilenceRemoverOutputPort output;
} SilenceRemover;

/*----------------------------------------------------------------------
|    ScanPacket
+---------------------------------------------------------------------*/
static BLT_Result
ScanPacket(BLT_MediaPacket* packet, 
           BLT_Cardinal*    zero_head, 
           BLT_Cardinal*    zero_tail)
{
    BLT_PcmMediaType* media_type;
    short*            pcm;
    BLT_Cardinal      sample_count;
    BLT_Ordinal       sample;
    BLT_Cardinal      zero_head_count = 0;
    BLT_Cardinal      non_zero_run = 0;

    /* default values */
    *zero_head = 0;
    *zero_tail = 0;

    /* get the media type */
    BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* for now, we only support 16-bit, stereo, PCM */
    if (media_type->bits_per_sample != 16 || media_type->channel_count != 2) {
        return BLT_SUCCESS;
    }

    /* check for zero samples */
    sample_count = BLT_MediaPacket_GetPayloadSize(packet)/4;
    if (sample_count == 0) return BLT_SUCCESS;
    pcm = BLT_MediaPacket_GetPayloadBuffer(packet);
    for (sample = 0; sample < sample_count; sample++, pcm+=2) { 
        if (pcm[0] > -BLT_SILENCE_REMOVER_THRESHOLD && 
            pcm[0] <  BLT_SILENCE_REMOVER_THRESHOLD && 
            pcm[1] > -BLT_SILENCE_REMOVER_THRESHOLD && 
            pcm[1] <  BLT_SILENCE_REMOVER_THRESHOLD) {
            if (sample == zero_head_count) {
                zero_head_count++;
            }
        } else {
            non_zero_run = sample+1;
        }
    }
    *zero_head = zero_head_count*4;
    if (non_zero_run > 0) {
        *zero_tail = (sample_count-non_zero_run)*4;
    }

    return BLT_SUCCESS;     
}

/*----------------------------------------------------------------------
|    SilenceRemover_TrimPending
+---------------------------------------------------------------------*/
static void
SilenceRemover_TrimPending(SilenceRemover* remover)
{
    BLT_MediaPacket* packet = remover->input.pending;
    short*           pcm;
    BLT_Cardinal     sample_count;
    BLT_Cardinal     skip = 0;
    int              sample;

    /* quick check */
    if (!packet) return;

    BLT_Debug("SilenceRemover: trimming pending packet\n");

    /* remove silence at the end of the packet */
    pcm = (short*)BLT_MediaPacket_GetPayloadBuffer(packet);
    sample_count = BLT_MediaPacket_GetPayloadSize(packet)/4;
    pcm += sample_count*2;
    for (sample = sample_count-1; sample >= 0; sample--, pcm-=2) {
        if (pcm[0] > -BLT_SILENCE_REMOVER_THRESHOLD && 
            pcm[0] <  BLT_SILENCE_REMOVER_THRESHOLD && 
            pcm[1] > -BLT_SILENCE_REMOVER_THRESHOLD && 
            pcm[1] <  BLT_SILENCE_REMOVER_THRESHOLD) {
            skip++;
        }
    }
    BLT_MediaPacket_SetPayloadSize(packet, (sample_count-skip)*4);
}

/*----------------------------------------------------------------------
|    SilenceRemover_AcceptPending
+---------------------------------------------------------------------*/
static void
SilenceRemover_AcceptPending(SilenceRemover* remover)
{
    BLT_MediaPacket* packet = remover->input.pending;
    BLT_Result       result;

    if (packet != NULL) {
        result = ATX_List_AddData(remover->output.packets, packet);
        if (ATX_FAILED(result)) {
            BLT_MediaPacket_Release(packet);
        }
        remover->input.pending = NULL;
        /*BLT_Debug("SilenceRemover: accepting pending packet\n");*/
    } else {
        /*BLT_Debug("SilenceRemover: no pending packet\n");*/
    }
}

/*----------------------------------------------------------------------
|    SilenceRemover_HoldPacket
+---------------------------------------------------------------------*/
static void
SilenceRemover_HoldPacket(SilenceRemover* remover, BLT_MediaPacket* packet)
{
    BLT_Debug("SilenceRemover: holding packet\n");

    /* accept the previously pending packet if any */
    SilenceRemover_AcceptPending(remover);

    /* hold the packet as a pending input */
    remover->input.pending = packet;
    BLT_MediaPacket_AddReference(packet);
}

/*----------------------------------------------------------------------
|    SilenceRemover_AcceptPacket
+---------------------------------------------------------------------*/
static void
SilenceRemover_AcceptPacket(SilenceRemover* remover, BLT_MediaPacket* packet)
{
    BLT_Result result;
    /*BLT_Debug("SilenceRemover: accepting packet\n");*/

    /* first, use any pending packet */
    SilenceRemover_AcceptPending(remover);

    /* add the packet to the output list */
    result = ATX_List_AddData(remover->output.packets, packet);
    if (ATX_SUCCEEDED(result)) {
        BLT_MediaPacket_AddReference(packet);
    }
}

/*----------------------------------------------------------------------
|    SilenceRemoverInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemoverInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                                  BLT_MediaPacket*            packet)
{
    SilenceRemover* remover = (SilenceRemover*)instance;
    BLT_Flags       packet_flags;
    BLT_Cardinal    zero_head = 0;
    BLT_Cardinal    zero_tail = 0;
    BLT_Offset      payload_offset;
    BLT_Size        payload_size;
    ATX_Result      result;

    /*BLT_Debug("SilenceRemoverInputPort_PutPacket\n");*/

    /* get the packet info */
    packet_flags   = BLT_MediaPacket_GetFlags(packet);
    payload_offset = BLT_MediaPacket_GetPayloadOffset(packet);
    payload_size   = BLT_MediaPacket_GetPayloadSize(packet);

    /* scan the packet for zeros */
    if (payload_size != 0) {
        result = ScanPacket(packet, &zero_head, &zero_tail);    
        if (BLT_FAILED(result)) return result;
        if (zero_head || zero_tail) {
            BLT_Debug("SilenceRemover: packet: zero_head=%ld, zero_tail=%ld\n",
                      zero_head, zero_tail);
        }
    }

    /* decide how to process the packet */
    if (remover->state == SILENCE_REMOVER_STATE_START_OF_STREAM) {
        if (zero_head == payload_size) {
            /* packet is all silence */
            if (packet_flags != 0) {
                /* packet has flags, don't discard it, just empty it */
                BLT_Debug("SilenceRemover: emptying packet\n");
                BLT_MediaPacket_SetPayloadSize(packet, 0);
                SilenceRemover_AcceptPacket(remover, packet);
            } else {
                BLT_Debug("SilenceRemover: dropping packet\n");
            }
        } else {
            /* remove silence at the start of the packet */
            BLT_MediaPacket_SetPayloadOffset(packet, payload_offset+zero_head);
            SilenceRemover_AcceptPacket(remover, packet);

            /* we're now in the stream unless this is also the end */
            if (!(packet_flags & BLT_MEDIA_PACKET_FLAG_END_OF_STREAM)) {
                BLT_Debug("SilenceRemover: new state = IN_STREAM\n");
                remover->state =  SILENCE_REMOVER_STATE_IN_STREAM;
            }
        }
    } else {
        /* in stream */
        if (zero_head == payload_size) {
            /* packet is all silence */
            BLT_Debug("SilenceRemover: packet is all silence\n");
            if (packet_flags) {
                /* packet has flags, don't discard it, just empty it */
                SilenceRemover_TrimPending(remover);
                BLT_MediaPacket_SetPayloadSize(packet, 0);
                SilenceRemover_AcceptPacket(remover, packet);
            } else {
                BLT_Debug("SilenceRemover: dropping packet\n");
            }
        } else {
            /* accept the pending packet */
            SilenceRemover_AcceptPending(remover);
            if (zero_tail) {
                /* packet has some silence at the end */
                BLT_Debug("SilenceRemover: packet has silence at end\n");
                SilenceRemover_HoldPacket(remover, packet);
            } else {
                /* packet has no silence at the end */
                /*BLT_Debug("SilenceRemover: packet has no silence at end\n");*/
                SilenceRemover_AcceptPacket(remover, packet);
            }
        }
        if (packet_flags & BLT_MEDIA_PACKET_FLAG_END_OF_STREAM ||
            packet_flags & BLT_MEDIA_PACKET_FLAG_START_OF_STREAM) {
            BLT_Debug("SilenceRemover: new state = START_OF_STREAM\n");
            remover->state = SILENCE_REMOVER_STATE_START_OF_STREAM;
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SilenceRemoverInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemoverInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                       BLT_Ordinal            index,
                                       const BLT_MediaType**  media_type)
{
    BLT_COMPILER_UNUSED(instance);
    if (index == 0) {
        *media_type = &BLT_GenericPcmMediaType;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SilenceRemoverInputPort, 
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
SilenceRemoverInputPort_BLT_MediaPortInterface = {
    SilenceRemoverInputPort_GetInterface,
    SilenceRemoverInputPort_GetName,
    SilenceRemoverInputPort_GetProtocol,
    SilenceRemoverInputPort_GetDirection,
    SilenceRemoverInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
SilenceRemoverInputPort_BLT_PacketConsumerInterface = {
    SilenceRemoverInputPort_GetInterface,
    SilenceRemoverInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverInputPort)
ATX_INTERFACE_MAP_ADD(SilenceRemoverInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(SilenceRemoverInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverInputPort)

/*----------------------------------------------------------------------
|    SilenceRemoverOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemoverOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                                   BLT_MediaPacket**           packet)
{
    SilenceRemover* remover = (SilenceRemover*)instance;
    ATX_ListItem*   item;

    item = ATX_List_GetFirstItem(remover->output.packets);
    if (item) {
        *packet = ATX_ListItem_GetData(item);
        ATX_List_RemoveItem(remover->output.packets, item);
        return BLT_SUCCESS;
    } else {
        *packet = NULL;
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
}

/*----------------------------------------------------------------------
|   SilenceRemoverOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemoverOutputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                        BLT_Ordinal            index,
                                        const BLT_MediaType**  media_type)
{
    BLT_COMPILER_UNUSED(instance);
    if (index == 0) {
        *media_type = &BLT_GenericPcmMediaType;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SilenceRemoverOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
SilenceRemoverOutputPort_BLT_MediaPortInterface = {
    SilenceRemoverOutputPort_GetInterface,
    SilenceRemoverOutputPort_GetName,
    SilenceRemoverOutputPort_GetProtocol,
    SilenceRemoverOutputPort_GetDirection,
    SilenceRemoverOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
SilenceRemoverOutputPort_BLT_PacketProducerInterface = {
    SilenceRemoverOutputPort_GetInterface,
    SilenceRemoverOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverOutputPort)
ATX_INTERFACE_MAP_ADD(SilenceRemoverOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(SilenceRemoverOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverOutputPort)

/*----------------------------------------------------------------------
|    SilenceRemover_SetupPorts
+---------------------------------------------------------------------*/
static BLT_Result
SilenceRemover_SetupPorts(SilenceRemover* remover)
{
    ATX_Result result;

    /* no pending packet yet */
    remover->input.pending = NULL;

    /* create a list of output packets */
    result = ATX_List_Create(&remover->output.packets);
    if (ATX_FAILED(result)) return result;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SilenceRemover_Create
+---------------------------------------------------------------------*/
static BLT_Result
SilenceRemover_Create(BLT_Module*              module,
                      BLT_Core*                core, 
                      BLT_ModuleParametersType parameters_type,
                      BLT_CString              parameters, 
                      ATX_Object*              object)
{
    SilenceRemover* remover;
    BLT_Result  result;

    BLT_Debug("SilenceRemover::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    remover = ATX_AllocateZeroMemory(sizeof(SilenceRemover));
    if (remover == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&remover->base, module, core);

    /* construct the object */
    remover->state = SILENCE_REMOVER_STATE_START_OF_STREAM;

    /* setup the input and output ports */
    result = SilenceRemover_SetupPorts(remover);
    if (BLT_FAILED(result)) {
        BLT_BaseMediaNode_Destruct(&remover->base);
        ATX_FreeMemory(remover);
        ATX_CLEAR_OBJECT(object);
        return result;
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)remover;
    ATX_INTERFACE(object) = (ATX_Interface*)&SilenceRemover_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SilenceRemover_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
SilenceRemover_Destroy(SilenceRemover* remover)
{ 
    ATX_ListItem* item;

    BLT_Debug("SilenceRemover::Destroy\n");

    /* release any input packet we may hold */
    if (remover->input.pending) {
        BLT_MediaPacket_Release(remover->input.pending);
    }

    /* release any output packet we may hold */
    item = ATX_List_GetFirstItem(remover->output.packets);
    while (item) {
        BLT_MediaPacket* packet = ATX_ListItem_GetData(item);
        if (packet) {
            BLT_MediaPacket_Release(packet);
        }
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Destroy(remover->output.packets);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&remover->base);

    /* free the object memory */
    ATX_FreeMemory(remover);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|    SilenceRemover_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemover_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    SilenceRemover* remover = (SilenceRemover*)instance;

    /* keep a reference to the stream */
    remover->context = *stream;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SilenceRemover_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemover_Deactivate(BLT_MediaNodeInstance* instance)
{
    SilenceRemover* remover = (SilenceRemover*)instance;

    /* we're detached from the stream */
    ATX_CLEAR_OBJECT(&remover->context);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       SilenceRemover_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemover_GetPortByName(BLT_MediaNodeInstance* instance,
                             BLT_CString            name,
                             BLT_MediaPort*         port)
{
    SilenceRemover* remover = (SilenceRemover*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)remover;
        ATX_INTERFACE(port) = &SilenceRemoverInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)remover;
        ATX_INTERFACE(port) = &SilenceRemoverOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    SilenceRemover_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemover_Seek(BLT_MediaNodeInstance* instance,
                    BLT_SeekMode*          mode,
                    BLT_SeekPoint*         point)
{
    SilenceRemover* remover = (SilenceRemover*)instance;
    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);
    
    /* flush teh pending packet */
    if (remover->input.pending) {
        BLT_MediaPacket_Release(remover->input.pending);
    } 
    remover->input.pending = NULL;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
SilenceRemover_BLT_MediaNodeInterface = {
    SilenceRemover_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    SilenceRemover_GetPortByName,
    SilenceRemover_Activate,
    SilenceRemover_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    SilenceRemover_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(SilenceRemover, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemover)
ATX_INTERFACE_MAP_ADD(SilenceRemover, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(SilenceRemover, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemover)

/*----------------------------------------------------------------------
|       SilenceRemoverModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
SilenceRemoverModule_Probe(BLT_ModuleInstance*      instance, 
                           BLT_Core*                core,
                           BLT_ModuleParametersType parameters_type,
                           BLT_AnyConst             parameters,
                           BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(core);
    BLT_COMPILER_UNUSED(instance);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* we need a name */
            if (constructor->name == NULL ||
                !ATX_StringsEqual(constructor->name, "SilenceRemover")) {
                return BLT_FAILURE;
            }

            /* the input and output protocols should be PACKET */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be unspecified, or audio/pcm */
            if (!(constructor->spec.input.media_type->id == 
                  BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.input.media_type->id ==
                  BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == 
                  BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.output.media_type->id ==
                  BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* match level is always exact */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            BLT_Debug("SilenceRemoverModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(SilenceRemover)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface SilenceRemoverModule_BLT_ModuleInterface = {
    SilenceRemoverModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    SilenceRemoverModule_CreateInstance,
    SilenceRemoverModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define SilenceRemoverModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(SilenceRemoverModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverModule) 
ATX_INTERFACE_MAP_ADD(SilenceRemoverModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(SilenceRemoverModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(SilenceRemoverModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_SilenceRemoverModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Silence Remover", NULL, 0,
                                 &SilenceRemoverModule_BLT_ModuleInterface,
                                 object);
}
