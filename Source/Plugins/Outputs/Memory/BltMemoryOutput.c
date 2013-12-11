/*****************************************************************
|
|   Memory Output Module
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltMemoryOutput.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketConsumer.h"
#include "BltPacketProducer.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.memory")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} MemoryOutputModule;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_PacketProducer);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    BLT_MediaType*   expected_media_type;
    BLT_MediaPacket* packet;
} MemoryOutput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(MemoryOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(MemoryOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(MemoryOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(MemoryOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(MemoryOutput, BLT_PacketConsumer)
ATX_DECLARE_INTERFACE_MAP(MemoryOutput, BLT_PacketProducer)

/*----------------------------------------------------------------------
|    MemoryOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
MemoryOutput_PutPacket(BLT_PacketConsumer* _self,
                       BLT_MediaPacket*    packet)
{
    MemoryOutput*        self = ATX_SELF(MemoryOutput, BLT_PacketConsumer);
    const BLT_MediaType* media_type;

    /* check the media type */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (self->expected_media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN &&
        self->expected_media_type->id != media_type->id) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }

    /* release the previous packet if we have one */
    if (self->packet) {
        BLT_MediaPacket_Release(self->packet);
        self->packet = NULL;
    }
    
    /* keep a reference to the packet */
    self->packet = packet;
    BLT_MediaPacket_AddReference(packet);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MemoryOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
MemoryOutput_GetPacket(BLT_PacketProducer* _self,
                       BLT_MediaPacket**   packet)
{
    MemoryOutput* self = ATX_SELF(MemoryOutput, BLT_PacketProducer);
    *packet = self->packet;
    if (self->packet) {
        BLT_MediaPacket_AddReference(self->packet);
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MemoryOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
MemoryOutput_QueryMediaType(BLT_MediaPort*        _self,
                            BLT_Ordinal           index,
                            const BLT_MediaType** media_type)
{
    MemoryOutput* self = ATX_SELF(MemoryOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    MemoryOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
MemoryOutput_Create(BLT_Module*              module,
                    BLT_Core*                core,
                    BLT_ModuleParametersType parameters_type,
                    BLT_CString              parameters,
                    BLT_MediaNode**          object)
{
    MemoryOutput*             self;
    BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;
    
    ATX_LOG_FINE("Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(MemoryOutput));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* keep the media type info */
    BLT_MediaType_Clone(constructor->spec.input.media_type, 
                        &self->expected_media_type); 

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, MemoryOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, MemoryOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(self, MemoryOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE(self, MemoryOutput, BLT_PacketProducer);
    ATX_SET_INTERFACE(self, MemoryOutput, BLT_MediaPort);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    MemoryOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
MemoryOutput_Destroy(MemoryOutput* self)
{
    ATX_LOG_FINE("Destroy");

    /* free the media type extensions */
    BLT_MediaType_Free(self->expected_media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   MemoryOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
MemoryOutput_GetPortByName(BLT_MediaNode*  _self,
                          BLT_CString     name,
                          BLT_MediaPort** port)
{
    MemoryOutput* self = ATX_SELF_EX(MemoryOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(MemoryOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(MemoryOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(MemoryOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(MemoryOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(MemoryOutput, BLT_PacketConsumer)
    ATX_GET_INTERFACE_ACCEPT(MemoryOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(MemoryOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(MemoryOutput, BLT_MediaPort)
    MemoryOutput_GetName,
    MemoryOutput_GetProtocol,
    MemoryOutput_GetDirection,
    MemoryOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(MemoryOutput, BLT_PacketConsumer)
    MemoryOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(MemoryOutput, BLT_PacketProducer)
    MemoryOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(MemoryOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    MemoryOutput_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(MemoryOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   MemoryOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
MemoryOutputModule_Probe(BLT_Module*              self, 
                         BLT_Core*                core,
                         BLT_ModuleParametersType parameters_type,
                         BLT_AnyConst             parameters,
                         BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* the input protocol should be PACKET and the */
            /* output protocol should be NONE              */
            if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* the name should be 'memory' */
            if (constructor->name == NULL ||
                !ATX_StringsEqual(constructor->name, "memory")) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            ATX_LOG_FINE_1("MemoryOutputModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(MemoryOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(MemoryOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(MemoryOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(MemoryOutputModule, MemoryOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(MemoryOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    MemoryOutputModule_CreateInstance,
    MemoryOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define MemoryOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(MemoryOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(MemoryOutputModule,
                                         "Memory Output",
                                         "com.axiosys.output.debug",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
