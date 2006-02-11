/*****************************************************************
|
|      File: BltFileInput.c
|
|      File Input Module
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
#include "BltFileInput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltModule.h"
#include "BltByteStreamProvider.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInputModule)
static const BLT_ModuleInterface FileInputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInput)
static const BLT_InputStreamProviderInterface FileInput_BLT_InputStreamProviderInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} FileInputModule;

typedef struct {
    BLT_BaseMediaNode base;
    ATX_File          file;
    ATX_InputStream   stream;
    BLT_MediaType*    media_type;
} FileInput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInput)
static const BLT_MediaNodeInterface FileInput_BLT_MediaNodeInterface;
static const BLT_MediaPortInterface FileInput_BLT_MediaPortInterface;
static BLT_Result FileInput_Destroy(FileInput* input);

/*----------------------------------------------------------------------
|    FileInput_DecideMediaType
+---------------------------------------------------------------------*/
static BLT_Result
FileInput_DecideMediaType(FileInput* input, BLT_CString name)
{
    BLT_Registry registry;
    BLT_CString  extension;
    BLT_Result   result;

    /* compute file extension */
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
                                                   &input->media_type->id);
}

/*----------------------------------------------------------------------
|    FileInput_Create
+---------------------------------------------------------------------*/
static BLT_Result
FileInput_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_CString              parameters, 
                 ATX_Object*              object)
{
    FileInput*                input;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    BLT_Result                result;

    BLT_Debug("FileInput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    input = ATX_AllocateZeroMemory(sizeof(FileInput));
    if (input == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&input->base, module, core);
    
    /* strip the "file:" prefix if it is present */
    if (ATX_StringsEqualN(constructor->name, "file:", 5)) {
        constructor->name += 5;
    }

    /* create the file object */
    result = ATX_File_Create(constructor->name, &input->file);
    if (ATX_FAILED(result)) {
        ATX_CLEAR_OBJECT(&input->file);
        goto failure;
    }

    /* open the file */
    result = ATX_File_Open(&input->file, ATX_FILE_OPEN_MODE_READ);
    if (ATX_FAILED(result)) goto failure;

    /* get the input stream */
    result = ATX_File_GetInputStream(&input->file, &input->stream);
    if (ATX_FAILED(result)) {
        ATX_CLEAR_OBJECT(&input->stream);
        goto failure;
    }

    /* figure out the media type */
    if (constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN) {
        /* unknown type, try to figure it out from the file extension */
        BLT_MediaType_Clone(&BLT_MediaType_Unknown, &input->media_type);
        FileInput_DecideMediaType(input, constructor->name);
    } else {
        /* use the media type from the output spec */
        BLT_MediaType_Clone(constructor->spec.output.media_type, 
                            &input->media_type);
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)input;
    ATX_INTERFACE(object) = (ATX_Interface*)&FileInput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;

failure:
    FileInput_Destroy(input);
    ATX_CLEAR_OBJECT(object);
    return result;
}

/*----------------------------------------------------------------------
|    FileInput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FileInput_Destroy(FileInput* input)
{
    BLT_Debug("FileInput::Destroy\n");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(&input->stream);
    
    /* destroy the file */
    ATX_DESTROY_OBJECT(&input->file);

    /* free the media type extensions */
    BLT_MediaType_Free(input->media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&input->base);

    /* free the object memory */
    ATX_FreeMemory(input);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       FileInput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_GetPortByName(BLT_MediaNodeInstance* instance,
                        BLT_CString            name,
                        BLT_MediaPort*         port)
{
    if (ATX_StringsEqual(name, "output")) {
        /* we implement the BLT_MediaPort interface ourselves */
        ATX_INSTANCE(port) = (BLT_MediaPortInstance*)instance;
        ATX_INTERFACE(port) = &FileInput_BLT_MediaPortInterface;
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    FileInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_QueryMediaType(BLT_MediaPortInstance* instance,
                         BLT_Ordinal            index,
                         const BLT_MediaType**  media_type)
{
    FileInput* input = (FileInput*)instance;
    
    if (index == 0) {
        *media_type = input->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       FileInput_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_GetStream(BLT_InputStreamProviderInstance* instance,
                    ATX_InputStream*                 stream)
{
    FileInput* input = (FileInput*)instance;

    /* return our stream object */
    *stream = input->stream;
    ATX_REFERENCE_OBJECT(stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FileInput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    FileInput* input = (FileInput*)instance;

    /* update the stream info */
    {
        BLT_StreamInfo info;
        BLT_Result     result;

        result = ATX_File_GetSize(&input->file, &info.size);
        if (BLT_SUCCEEDED(result)) {
            info.mask = BLT_STREAM_INFO_MASK_SIZE;
            BLT_Stream_SetInfo(stream, &info);
        }
    }
    
    /* keep the stream as our context */
    input->base.context = *stream;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
FileInput_BLT_MediaNodeInterface = {
    FileInput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    FileInput_GetPortByName,
    FileInput_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FileInput, 
                                         "output", 
                                         STREAM_PULL, 
                                         OUT)
static const BLT_MediaPortInterface
FileInput_BLT_MediaPortInterface = {
    FileInput_GetInterface,
    FileInput_GetName,
    FileInput_GetProtocol,
    FileInput_GetDirection,
    FileInput_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamProviderInterface
FileInput_BLT_InputStreamProviderInterface = {
    FileInput_GetInterface,
    FileInput_GetStream
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FileInput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInput)
ATX_INTERFACE_MAP_ADD(FileInput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(FileInput, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(FileInput, BLT_InputStreamProvider)
ATX_INTERFACE_MAP_ADD(FileInput, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInput)

/*----------------------------------------------------------------------
|       FileInputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FileInputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* we need a file name */
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
            if (ATX_StringsEqualN(constructor->name, "file:", 5)) {
                /* this is an exact match for us */
                *match = BLT_MODULE_PROBE_MATCH_EXACT;
            } else if (constructor->spec.input.protocol ==
                       BLT_MEDIA_PORT_PROTOCOL_NONE) {
                /* default match level */
                *match = BLT_MODULE_PROBE_MATCH_DEFAULT;
            } else {
                return BLT_FAILURE;
            }

            BLT_Debug("FileInputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FileInput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface FileInputModule_BLT_ModuleInterface = {
    FileInputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    FileInputModule_CreateInstance,
    FileInputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FileInputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FileInputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInputModule) 
ATX_INTERFACE_MAP_ADD(FileInputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(FileInputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileInputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_FileInputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("File Input", NULL, 0,
                                 &FileInputModule_BLT_ModuleInterface,
                                 object);
}
