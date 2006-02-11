/*****************************************************************
|
|      File: BltDebugOutput.c
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
#include "BltNullOutput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPacketConsumer.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputModule)
static const BLT_ModuleInterface NullOutputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutput)
static const BLT_MediaNodeInterface NullOutput_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputInputPort)
static const BLT_MediaPortInterface NullOutputInputPort_BLT_MediaPortInterface;
static const BLT_PacketConsumerInterface NullOutputInputPort_BLT_PacketConsumerInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} NullOutputModule;

typedef struct {
    BLT_BaseMediaNode base;
    BLT_MediaType*    expected_media_type;
} NullOutput;

/*----------------------------------------------------------------------
|    NullOutputInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
NullOutputInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                              BLT_MediaPacket*            packet)
{
    NullOutput*          output = (NullOutput*)instance;
    const BLT_MediaType* media_type;

    /* check the media type */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (output->expected_media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN &&
        output->expected_media_type->id != media_type->id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    NullOutputInputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
NullOutputInputPort_QueryMediaType(BLT_MediaPortInstance* instance,
                                   BLT_Ordinal            index,
                                   const BLT_MediaType**  media_type)
{
    NullOutput* output = (NullOutput*)instance;
    if (index == 0) {
        *media_type = output->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(NullOutputInputPort, 
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
NullOutputInputPort_BLT_MediaPortInterface = {
    NullOutputInputPort_GetInterface,
    NullOutputInputPort_GetName,
    NullOutputInputPort_GetProtocol,
    NullOutputInputPort_GetDirection,
    NullOutputInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
NullOutputInputPort_BLT_PacketConsumerInterface = {
    NullOutputInputPort_GetInterface,
    NullOutputInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputInputPort)
ATX_INTERFACE_MAP_ADD(NullOutputInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(NullOutputInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputInputPort)

/*----------------------------------------------------------------------
|    NullOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
NullOutput_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  ATX_Object*              object)
{
    NullOutput*              output;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;

    BLT_Debug("NullOutput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(NullOutput));
    if (output == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&output->base, module, core);

    /* construct the object */
    BLT_MediaType_Clone(constructor->spec.input.media_type, 
                        &output->expected_media_type);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)output;
    ATX_INTERFACE(object) = (ATX_Interface*)&NullOutput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    NullOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
NullOutput_Destroy(NullOutput* output)
{
    BLT_Debug("NullOutput::Destroy\n");

    /* free the media type extensions */
    BLT_MediaType_Free(output->expected_media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&output->base);

    /* free the object memory */
    ATX_FreeMemory(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       NullOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
NullOutput_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    NullOutput* output = (NullOutput*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)output;
        ATX_INTERFACE(port) = &NullOutputInputPort_BLT_MediaPortInterface; 
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
NullOutput_BLT_MediaNodeInterface = {
    NullOutput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    NullOutput_GetPortByName,
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
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(NullOutput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutput)
ATX_INTERFACE_MAP_ADD(NullOutput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(NullOutput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutput)

/*----------------------------------------------------------------------
|       NullOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
NullOutputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* the input protocol should be PACKET and the */
            /* output protocol should be NONE              */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* the name should be 'null' */
            if (constructor->name == NULL ||
                !ATX_StringsEqual(constructor->name, "null")) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            BLT_Debug("NullOutputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(NullOutput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface NullOutputModule_BLT_ModuleInterface = {
    NullOutputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    NullOutputModule_CreateInstance,
    NullOutputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define NullOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(NullOutputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputModule) 
ATX_INTERFACE_MAP_ADD(NullOutputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(NullOutputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NullOutputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NullOutputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Null Output", NULL, 0,
                                 &NullOutputModule_BLT_ModuleInterface,
                                 object);
}
