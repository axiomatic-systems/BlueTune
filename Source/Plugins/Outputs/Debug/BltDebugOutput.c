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
#include "BltDebugOutput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketConsumer.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutputModule)
static const BLT_ModuleInterface DebugOutputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutput)
static const BLT_MediaNodeInterface DebugOutput_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutput)
static const BLT_MediaPortInterface DebugOutput_BLT_MediaPortInterface;
static const BLT_PacketConsumerInterface DebugOutput_BLT_PacketConsumerInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} DebugOutputModule;

typedef struct {
    BLT_BaseMediaNode base;
    BLT_MediaType*    expected_media_type;
} DebugOutput;

/*----------------------------------------------------------------------
|    DebugOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
DebugOutput_PutPacket(BLT_PacketConsumerInstance* instance,
                      BLT_MediaPacket*            packet)
{
    DebugOutput*         output = (DebugOutput*)instance;
    const BLT_MediaType* media_type;

    /* check the media type */
    BLT_MediaPacket_GetMediaType(packet, &media_type);
    if (output->expected_media_type->id != BLT_MEDIA_TYPE_ID_UNKNOWN &&
        output->expected_media_type->id != media_type->id) {
        return BLT_ERROR_INVALID_MEDIA_FORMAT;
    }

    /* print type info extensions if they are known to us */
    if (media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        BLT_PcmMediaType* pcm_type = (BLT_PcmMediaType*)media_type;
        BLT_Debug("DebugOutput::PutPacket - type=%d, sr=%ld, ch=%d, bps=%d\n",
                   media_type->id,
                   pcm_type->sample_rate,
                   pcm_type->channel_count,
                   pcm_type->bits_per_sample);
    } else {
        BLT_Debug("DebugOutput::PutPacket - type=%d\n",
                  media_type->id);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    DebugOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
DebugOutput_QueryMediaType(BLT_MediaPortInstance* instance,
                           BLT_Ordinal            index,
                           const BLT_MediaType**  media_type)
{
    DebugOutput* output = (DebugOutput*)instance;

    if (index == 0) {
        *media_type = output->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    DebugOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
DebugOutput_Create(BLT_Module*              module,
                   BLT_Core*                core, 
                   BLT_ModuleParametersType parameters_type,
                   BLT_CString              parameters, 
                   ATX_Object*              object)
{
    DebugOutput*              output;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    
    BLT_Debug("DebugOutput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(DebugOutput));
    if (output == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&output->base, module, core);

    /* keep the media type info */
    BLT_MediaType_Clone(constructor->spec.input.media_type, 
                        &output->expected_media_type); 

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)output;
    ATX_INTERFACE(object) = (ATX_Interface*)&DebugOutput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    DebugOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
DebugOutput_Destroy(DebugOutput* output)
{
    BLT_Debug("DebugOutput::Destroy\n");

    /* free the media type extensions */
    BLT_MediaType_Free(output->expected_media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&output->base);

    /* free the object memory */
    ATX_FreeMemory(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       DebugOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
DebugOutput_GetPortByName(BLT_MediaNodeInstance* instance,
                          BLT_CString            name,
                          BLT_MediaPort*         port)
{
    DebugOutput* output = (DebugOutput*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)output;
        ATX_INTERFACE(port) = &DebugOutput_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(DebugOutput, 
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
DebugOutput_BLT_MediaPortInterface = {
    DebugOutput_GetInterface,
    DebugOutput_GetName,
    DebugOutput_GetProtocol,
    DebugOutput_GetDirection,
    DebugOutput_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
DebugOutput_BLT_PacketConsumerInterface = {
    DebugOutput_GetInterface,
    DebugOutput_PutPacket
};

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
DebugOutput_BLT_MediaNodeInterface = {
    DebugOutput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    DebugOutput_GetPortByName,
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
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(DebugOutput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutput)
ATX_INTERFACE_MAP_ADD(DebugOutput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(DebugOutput, ATX_Referenceable)
ATX_INTERFACE_MAP_ADD(DebugOutput, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(DebugOutput, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutput)

/*----------------------------------------------------------------------
|       DebugOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
DebugOutputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* the name should be 'debug:<level>' */
            if (constructor->name == NULL ||
                !ATX_StringsEqualN(constructor->name, "debug:", 6)) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            BLT_Debug("DebugOutputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(DebugOutput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface DebugOutputModule_BLT_ModuleInterface = {
    DebugOutputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    DebugOutputModule_CreateInstance,
    DebugOutputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define DebugOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(DebugOutputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutputModule) 
ATX_INTERFACE_MAP_ADD(DebugOutputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(DebugOutputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(DebugOutputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DebugOutputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Debug Output", NULL, 0,
                                 &DebugOutputModule_BLT_ModuleInterface,
                                 object);
}
