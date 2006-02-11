/*****************************************************************
|
|      File: BltPcmAdapter.c
|
|      PCM Adapter Module
|
|      (c) 2002-2005 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltPcmAdapter.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterModule)
static const BLT_ModuleInterface PcmAdapterModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapter)
static const BLT_MediaNodeInterface PcmAdapter_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterInputPort)
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterOutputPort)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} PcmAdapterModule;

typedef struct {
    BLT_BaseMediaNode base;
    BLT_MediaPacket*  packet;
    BLT_PcmMediaType  pcm_type;
} PcmAdapter;

/*----------------------------------------------------------------------
|    PcmAdapterInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
PcmAdapterInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                              BLT_MediaPacket*            packet)
{
    PcmAdapter* adapter = (PcmAdapter*)instance;
    
    /* transform the packet data */
    return BLT_Pcm_ConvertMediaPacket(&adapter->base.core,
                                      packet, 
                                      &adapter->pcm_type, 
                                      &adapter->packet);
}

/*----------------------------------------------------------------------
|   PcmAdapter_GetExpectedMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
PcmAdapterInputPort_GetExpectedMediaType(BLT_MediaPortInstance* instance,
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(PcmAdapterInputPort,
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
PcmAdapterInputPort_BLT_MediaPortInterface = {
    PcmAdapterInputPort_GetInterface,
    PcmAdapterInputPort_GetName,
    PcmAdapterInputPort_GetProtocol,
    PcmAdapterInputPort_GetDirection,
    PcmAdapterInputPort_GetExpectedMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
PcmAdapterInputPort_BLT_PacketConsumerInterface = {
    PcmAdapterInputPort_GetInterface,
    PcmAdapterInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterInputPort)
ATX_INTERFACE_MAP_ADD(PcmAdapterInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(PcmAdapterInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterInputPort)

/*----------------------------------------------------------------------
|    PcmAdapterOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
PcmAdapterOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                               BLT_MediaPacket**           packet)
{
    PcmAdapter* adapter = (PcmAdapter*)instance;

    if (adapter->packet) {
        *packet = adapter->packet;
        adapter->packet = NULL;
        return BLT_SUCCESS;
    } else {
        *packet = NULL;
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
}

/*----------------------------------------------------------------------
|   PcmAdapterOutputPort_GetExpectedMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
PcmAdapterOutputPort_GetExpectedMediaType(
    BLT_MediaPortInstance* instance,
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(PcmAdapterOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
PcmAdapterOutputPort_BLT_MediaPortInterface = {
    PcmAdapterOutputPort_GetInterface,
    PcmAdapterOutputPort_GetName,
    PcmAdapterOutputPort_GetProtocol,
    PcmAdapterOutputPort_GetDirection,
    PcmAdapterOutputPort_GetExpectedMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
PcmAdapterOutputPort_BLT_PacketProducerInterface = {
    PcmAdapterOutputPort_GetInterface,
    PcmAdapterOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterOutputPort)
ATX_INTERFACE_MAP_ADD(PcmAdapterOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(PcmAdapterOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterOutputPort)

/*----------------------------------------------------------------------
|    PcmAdapter_Create
+---------------------------------------------------------------------*/
static BLT_Result
PcmAdapter_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_AnyConst             parameters, 
                  ATX_Object*              object)
{
    BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;
    PcmAdapter*               adapter;

    BLT_Debug("PcmAdapter::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    adapter = ATX_AllocateZeroMemory(sizeof(PcmAdapter));
    if (adapter == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&adapter->base, module, core);

    /* check the media type */
    if (constructor->spec.output.media_type->id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* construct the object */
    adapter->pcm_type = *(BLT_PcmMediaType*)constructor->spec.output.media_type;

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)adapter;
    ATX_INTERFACE(object) = (ATX_Interface*)&PcmAdapter_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    PcmAdapter_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
PcmAdapter_Destroy(PcmAdapter* adapter)
{ 
    BLT_Debug("PcmAdapter::Destroy\n");

    /* release any input packet we may hold */
    if (adapter->packet) {
        BLT_MediaPacket_Release(adapter->packet);
    }

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&adapter->base);

    /* free the object memory */
    ATX_FreeMemory(adapter);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|       PcmAdapter_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
PcmAdapter_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    PcmAdapter* adapter = (PcmAdapter*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)adapter;
        ATX_INTERFACE(port) = &PcmAdapterInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)adapter;
        ATX_INTERFACE(port) = &PcmAdapterOutputPort_BLT_MediaPortInterface; 
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
PcmAdapter_BLT_MediaNodeInterface = {
    PcmAdapter_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    PcmAdapter_GetPortByName,
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
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(PcmAdapter, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapter)
ATX_INTERFACE_MAP_ADD(PcmAdapter, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(PcmAdapter, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapter)

/*----------------------------------------------------------------------
|       PcmAdapterModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
PcmAdapterModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* compute match based on specified name */
            if (constructor->name == NULL) {
                *match = BLT_MODULE_PROBE_MATCH_DEFAULT;

                /* the input protocol should be PACKET */
                if (constructor->spec.input.protocol != 
                    BLT_MEDIA_PORT_PROTOCOL_PACKET) {
                    return BLT_FAILURE;
                }

                /* output protocol should be PACKET */
                if (constructor->spec.output.protocol != 
                    BLT_MEDIA_PORT_PROTOCOL_PACKET) {
                    return BLT_FAILURE;
                }

                /* check that the in and out formats are supported */
                if (BLT_Pcm_CanConvert(constructor->spec.input.media_type, 
                                       constructor->spec.output.media_type)) {
                    return BLT_FAILURE;
                }
            } else {
                /* if a name is a specified, it needs to match exactly */
                if (!ATX_StringsEqual(constructor->name, "PcmAdapter")) {
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

                /* output protocol should be PACKET or ANY */
                if ((constructor->spec.output.protocol !=
                    BLT_MEDIA_PORT_PROTOCOL_ANY &&
                    constructor->spec.output.protocol != 
                    BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                    return BLT_FAILURE;
                }

                /* check that the in and out formats are supported */
                if (BLT_Pcm_CanConvert(constructor->spec.input.media_type, 
                                       constructor->spec.output.media_type)) {
                    return BLT_FAILURE;
                }
            }

            BLT_Debug("PcmAdapterModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(PcmAdapter)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface PcmAdapterModule_BLT_ModuleInterface = {
    PcmAdapterModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    PcmAdapterModule_CreateInstance,
    PcmAdapterModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define PcmAdapterModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(PcmAdapterModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterModule) 
ATX_INTERFACE_MAP_ADD(PcmAdapterModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(PcmAdapterModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(PcmAdapterModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_PcmAdapterModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("PCM Adapter", NULL, 0,
                                 &PcmAdapterModule_BLT_ModuleInterface,
                                 object);
}
