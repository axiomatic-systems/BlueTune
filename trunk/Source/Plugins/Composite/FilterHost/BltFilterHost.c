/*****************************************************************
|
|      File: BltFilterHost.c
|
|      Filter Host Module
|
|      (c) 2002-2006 Gilles Boccon-Gibod
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
#include "BltFilterHost.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostModule)
static const BLT_ModuleInterface FilterHostModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHost)
static const BLT_MediaNodeInterface FilterHost_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostInputPort)
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostOutputPort)

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} FilterHostModule;

typedef struct {
    BLT_BaseMediaNode base;
    BLT_MediaPacket*  packet;
} FilterHost;

/*----------------------------------------------------------------------
|    FilterHostInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FilterHostInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                              BLT_MediaPacket*            packet)
{
    FilterHost* filter = (FilterHost*)instance;
    BLT_PcmMediaType*  media_type;
    BLT_Result         result;

    /*BLT_Debug("FilterHostInputPort_PutPacket\n");*/

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    
    /* keep the packet */
    filter->packet = packet;
    BLT_MediaPacket_AddReference(packet);

    /* exit now if we're inactive */
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FilterHost_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
FilterHostInputPort_QueryMediaType(
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FilterHostInputPort,
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
FilterHostInputPort_BLT_MediaPortInterface = {
    FilterHostInputPort_GetInterface,
    FilterHostInputPort_GetName,
    FilterHostInputPort_GetProtocol,
    FilterHostInputPort_GetDirection,
    FilterHostInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
FilterHostInputPort_BLT_PacketConsumerInterface = {
    FilterHostInputPort_GetInterface,
    FilterHostInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostInputPort)
ATX_INTERFACE_MAP_ADD(FilterHostInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(FilterHostInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostInputPort)

/*----------------------------------------------------------------------
|    FilterHostOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
FilterHostOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                               BLT_MediaPacket**           packet)
{
    FilterHost* filter = (FilterHost*)instance;

    if (filter->packet) {
        *packet = filter->packet;
        filter->packet = NULL;
        return BLT_SUCCESS;
    } else {
        *packet = NULL;
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
}

/*----------------------------------------------------------------------
|   FilterHostOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
FilterHostOutputPort_QueryMediaType(
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FilterHostOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
FilterHostOutputPort_BLT_MediaPortInterface = {
    FilterHostOutputPort_GetInterface,
    FilterHostOutputPort_GetName,
    FilterHostOutputPort_GetProtocol,
    FilterHostOutputPort_GetDirection,
    FilterHostOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
FilterHostOutputPort_BLT_PacketProducerInterface = {
    FilterHostOutputPort_GetInterface,
    FilterHostOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostOutputPort)
ATX_INTERFACE_MAP_ADD(FilterHostOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(FilterHostOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostOutputPort)

/*----------------------------------------------------------------------
|    FilterHost_Create
+---------------------------------------------------------------------*/
static BLT_Result
FilterHost_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  ATX_Object*              object)
{
    FilterHost* filter;

    BLT_Debug("FilterHost::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    filter = ATX_AllocateZeroMemory(sizeof(FilterHost));
    if (filter == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&filter->base, module, core);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)filter;
    ATX_INTERFACE(object) = (ATX_Interface*)&FilterHost_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FilterHost_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FilterHost_Destroy(FilterHost* filter)
{ 
    BLT_Debug("FilterHost::Destroy\n");

    /* release any input packet we may hold */
    if (filter->packet) {
        BLT_MediaPacket_Release(filter->packet);
    }

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&filter->base);

    /* free the object memory */
    ATX_FreeMemory(filter);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|       FilterHost_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FilterHost_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    FilterHost* filter = (FilterHost*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)filter;
        ATX_INTERFACE(port) = &FilterHostInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)filter;
        ATX_INTERFACE(port) = &FilterHostOutputPort_BLT_MediaPortInterface; 
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
FilterHost_BLT_MediaNodeInterface = {
    FilterHost_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    FilterHost_GetPortByName,
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
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FilterHost, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHost)
ATX_INTERFACE_MAP_ADD(FilterHost, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(FilterHost, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHost)

/*----------------------------------------------------------------------
|       FilterHostModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FilterHostModule_Probe(BLT_ModuleInstance*      instance, 
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
                !ATX_StringsEqual(constructor->name, "FilterHost")) {
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

            BLT_Debug("FilterHostModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FilterHost)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface FilterHostModule_BLT_ModuleInterface = {
    FilterHostModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    FilterHostModule_CreateInstance,
    FilterHostModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FilterHostModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FilterHostModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostModule) 
ATX_INTERFACE_MAP_ADD(FilterHostModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(FilterHostModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FilterHostModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_FilterHostModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Filter Host", NULL, 0,
                                 &FilterHostModule_BLT_ModuleInterface,
                                 object);
}
