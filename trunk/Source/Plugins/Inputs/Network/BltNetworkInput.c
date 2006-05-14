/*****************************************************************
|
|   Network: BltNetworkInput.cpp
|
|   Network Input Module
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltNetworkInput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltModule.h"
#include "BltByteStreamProvider.h"
#include "BltTcpNetworkStream.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} NetworkInputModule;

typedef struct {
    BLT_BaseMediaNode base;
    BLT_Stream        context;
    ATX_ByteStream    stream;
    BLT_MediaType     media_type;
} NetworkInput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInput)
BLT_METHOD NetworkInput_GetPortByName(BLT_MediaNodeInstance* instance,
                                      BLT_String             name,
                                      BLT_MediaPort*         port);
BLT_METHOD NetworkInput_Activate(BLT_MediaNodeInstance* instance, 
                                 BLT_Stream* stream);
BLT_METHOD NetworkInput_Deactivate(BLT_MediaNodeInstance* instance);
BLT_METHOD NetworkInput_GetStream(BLT_ByteStreamProviderInstance* instance,
                                  ATX_ByteStream*                 stream,
                                  BLT_MediaType*                  media_type);

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
NetworkInput_BLT_MediaNodeInterface = {
    NetworkInput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    NetworkInput_GetPortByName,
    NetworkInput_Activate,
    NetworkInput_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_GET_PROTOCOL(NetworkInput, STREAM_PULL)
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_GET_DIRECTION(NetworkInput, OUT)
static const BLT_MediaPortInterface
NetworkInput_BLT_MediaPortInterface = {
    NetworkInput_GetInterface,
    NetworkInput_GetProtocol,
    NetworkInput_GetDirection,
    BLT_MediaPort_DefaultGetExpectedMediaType
};

/*----------------------------------------------------------------------
|    BLT_ByteStreamProducer interface
+---------------------------------------------------------------------*/
static const BLT_ByteStreamProviderInterface
NetworkInput_BLT_ByteStreamProviderInterface = {
    NetworkInput_GetInterface,
    NetworkInput_GetStream
};

/*----------------------------------------------------------------------
|    NetworkInput_DecideMediaType
+---------------------------------------------------------------------*/
static BLT_Result
NetworkInput_DecideMediaType(NetworkInput* input, BLT_String name)
{
    BLT_Registry registry;
    BLT_String   extension;
    BLT_Result   result;

    /* compute network extension */
    extension = NULL;
    while (*name) {
        if (*name == '.') {
            extension = name;
        }
        name++;
    }
    if (extension == NULL) return BLT_SUCCESS;

    /* get the registry */
    result = BLT_Core_GetRegistry(&input->base.core, &registry);
    if (BLT_FAILED(result)) return result;

    /* query the registry */
    return BLT_Registry_GetMediaTypeIdForExtension(&registry, 
                                                   extension, 
                                                   &input->media_type.id);
}

/*----------------------------------------------------------------------
|    NetworkInput_Create
+---------------------------------------------------------------------*/
static BLT_Result
NetworkInput_Create(BLT_Module*              module,
                    BLT_Core*                core, 
                    BLT_ModuleParametersType parameters_type,
                    BLT_AnyConst             parameters, 
                    ATX_Object*              object)
{
    NetworkInput*             input;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    BLT_Result                result;

    BLT_Debug("NetworkInput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    input = (NetworkInput*)ATX_AllocateZeroMemory(sizeof(NetworkInput));
    if (input == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&input->base, module, core);
    
    /* create the network stream */
    if (ATX_StringsEqualN(constructor->name, "tcp://", 7)) {
        /* create a TCP byte stream */
        result = BLT_TcpNetworkStream::CreateStream(constructor->name+6, 
                                                    &input->stream);
    } else {
        result = BLT_ERROR_INVALID_PARAMETERS;
    }

    if (ATX_FAILED(result)) {
        ATX_CLEAR_OBJECT(object);
        ATX_FreeMemory(input);
        return result;
    }

    /* figure out the media type */
    if (constructor->spec.output.media_type.id == BLT_MEDIA_TYPE_ID_UNKNOWN) {
        /* unknown type, try to figure it out from the network extension */
        BLT_MediaType_Init(&input->media_type, BLT_MEDIA_TYPE_ID_UNKNOWN);
        NetworkInput_DecideMediaType(input, constructor->name);
    } else {
        /* use the media type from the output spec */
        BLT_MediaType_Clone(&input->media_type, 
                            &constructor->spec.output.media_type);
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)input;
    ATX_INTERFACE(object) = (ATX_Interface*)&NetworkInput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    NetworkInput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
NetworkInput_Destroy(NetworkInput* input)
{
    BLT_Debug("NetworkInput::Destroy\n");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(&input->stream);
    
    /* free the media type extensions */
    BLT_MediaType_Free(&input->media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&input->base);

    /* free the object memory */
    ATX_FreeMemory(input);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   NetworkInput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
NetworkInput_GetPortByName(BLT_MediaNodeInstance* instance,
                           BLT_String             name,
                           BLT_MediaPort*         port)
{
    if (ATX_StringsEqual(name, "output")) {
        /* we implement the BLT_MediaPort interface ourselves */
        ATX_INSTANCE(port) = (BLT_MediaPortInstance*)instance;
        ATX_INTERFACE(port) = &NetworkInput_BLT_MediaPortInterface;
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|   NetworkInput_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
NetworkInput_GetStream(BLT_ByteStreamProviderInstance* instance,
                       ATX_ByteStream*                 stream,
                       BLT_MediaType*                  media_type)
{
    NetworkInput* input = (NetworkInput*)instance;

    *stream = input->stream;
    ATX_REFERENCE_OBJECT(stream);
    if (media_type) {
        *media_type = input->media_type;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    NetworkInput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
NetworkInput_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    NetworkInput* input = (NetworkInput*)instance;

    /* update the stream info */
    {
        BLT_StreamInfo info;
        BLT_Result     result;

        result = ATX_ByteStream_GetSize(&input->stream, &info.size);
        if (BLT_SUCCEEDED(result)) {
            info.mask = BLT_STREAM_INFO_MASK_SIZE;
            BLT_Stream_SetInfo(stream, &info);
        }
    }
    
    /* keep the stream as our context */
    input->context = *stream;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    NetworkInput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
NetworkInput_Deactivate(BLT_MediaNodeInstance* instance)
{
    NetworkInput* input = (NetworkInput*)instance;

    /* we're detached from the stream */
    ATX_CLEAR_OBJECT(&input->context);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(NetworkInput, base.reference_count)

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInput)
ATX_INTERFACE_MAP_ADD(NetworkInput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(NetworkInput, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(NetworkInput, BLT_ByteStreamProvider)
ATX_INTERFACE_MAP_ADD(NetworkInput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInput)

/*----------------------------------------------------------------------
|   NetworkInputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
NetworkInputModule_Probe(BLT_ModuleInstance*      /*instance*/, 
                         BLT_Core*                /*core*/,
                         BLT_ModuleParametersType parameters_type,
                         BLT_AnyConst             parameters,
                         BLT_Cardinal*            match)
{
    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = 
                (BLT_MediaNodeConstructor*)parameters;

            /* we need a network name */
            if (constructor->name == NULL) return BLT_FAILURE;

            /* the input protocol should be NONE, and the output */
            /* protocol should be STREAM_PULL                    */
            if ((constructor->spec.input.protocol !=
                BLT_MEDIA_PORT_PROTOCOL_ANY &&
                constructor->spec.input.protocol != 
                BLT_MEDIA_PORT_PROTOCOL_NONE) ||
                (constructor->spec.output.protocol !=
                BLT_MEDIA_PORT_PROTOCOL_ANY &&
                constructor->spec.output.protocol != 
                BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL)) {
                return BLT_FAILURE;
            }

            /* check the name */
            if (ATX_StringsEqualN(constructor->name, "tcp://", 7)) {
                /* this is an exact match for us */
                *match = BLT_MODULE_PROBE_MATCH_EXACT;
            } else {
                /* default match level */
                *match = BLT_MODULE_PROBE_MATCH_DEFAULT;
            }

            BLT_Debug("NetworkInputModule::Probe - Ok [%d]\n", *match);
            return BLT_SUCCESS;
        }    
        break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInputModule)
BLT_Result 
NetworkInputModule_CreateInstance(BLT_ModuleInstance*      module,
                                  BLT_Core*                core,
                                  BLT_ModuleParametersType parameters_type,
                                  BLT_AnyConst             parameters,
                                  const ATX_InterfaceId*   interface_id,
                                  ATX_Object*              object);

BLT_Result 
NetworkInputModule_Probe(BLT_ModuleInstance*      module,
                         BLT_Core*                core,
                         BLT_ModuleParametersType parameters_type,
                         BLT_AnyConst             parameters,
                         BLT_Cardinal*            match);

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface NetworkInputModule_BLT_ModuleInterface = {
    NetworkInputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    NetworkInputModule_CreateInstance,
    NetworkInputModule_Probe
};

/*----------------------------------------------------------------------
|   template instantiations
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(NetworkInput)

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define NetworkInputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(NetworkInputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|   standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInputModule) 
ATX_INTERFACE_MAP_ADD(NetworkInputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(NetworkInputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(NetworkInputModule)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkInputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Network Input", NULL, 0,
        &NetworkInputModule_BLT_ModuleInterface,
        object);
}