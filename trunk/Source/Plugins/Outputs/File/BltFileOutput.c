/*****************************************************************
|
|      File: BltFileOutput.c
|
|      File Output Module
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
#include "BltFileOutput.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltByteStreamProvider.h"
#include "BltPcm.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} FileOutputModule;

typedef struct {
    BLT_BaseMediaNode base;
    ATX_File          file;
    ATX_OutputStream  stream;
    BLT_MediaType*    media_type;
} FileOutput;

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutputModule)
static const BLT_ModuleInterface FileOutputModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutput)
static const BLT_MediaNodeInterface FileOutput_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutput)
static const BLT_MediaPortInterface FileOutput_BLT_MediaPortInterface;
static const BLT_OutputStreamProviderInterface FileOutput_BLT_OutputStreamProviderInterface;
static BLT_Result FileOutput_Destroy(FileOutput* output);

/*----------------------------------------------------------------------
|    FileOutput_DecideMediaType
+---------------------------------------------------------------------*/
static BLT_Result
FileOutput_DecideMediaType(FileOutput* output, BLT_CString name)
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
    result = BLT_Core_GetRegistry(&output->base.core, &registry);
    if (BLT_FAILED(result)) return result;

    /* query the registry */
    return BLT_Registry_GetMediaTypeIdForExtension(&registry, 
                                                   extension, 
                                                   &output->media_type->id);
}

/*----------------------------------------------------------------------
|       FileOutput_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
FileOutput_GetStream(BLT_OutputStreamProviderInstance* instance,
                     ATX_OutputStream*                 stream,
                     const BLT_MediaType*              media_type)
{
    FileOutput* output = (FileOutput*)instance;

    *stream = output->stream;
    ATX_REFERENCE_OBJECT(stream);

    /* we're providing the stream, but we *receive* the type */
    if (media_type) {
        if (output->media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN) {
            BLT_MediaType_Free(output->media_type);
            BLT_MediaType_Clone(media_type, &output->media_type);
        } else if (output->media_type->id != media_type->id) {
            return BLT_ERROR_INVALID_MEDIA_FORMAT;
        }
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FileOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
FileOutput_QueryMediaType(BLT_MediaPortInstance* instance,
                          BLT_Ordinal            index,
                          const BLT_MediaType**  media_type)
{
    FileOutput* output = (FileOutput*)instance;
    if (index == 0) {
        *media_type = output->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    FileOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
FileOutput_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  BLT_CString              parameters, 
                  ATX_Object*              object)
{
    FileOutput*               output;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    BLT_Result                result;

    BLT_Debug("FileOutput::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* check the name */
    if (constructor->name == NULL || ATX_StringLength(constructor->name) < 4) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }
        
    /* allocate memory for the object */
    output = ATX_AllocateZeroMemory(sizeof(FileOutput));
    if (output == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&output->base, module, core);

    /* figure out the media type */
    if (constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN) {
        /* unknown type, try to figure it out from the file extension */
        BLT_MediaType_Clone(&BLT_MediaType_Unknown, &output->media_type);
        result = FileOutput_DecideMediaType(output, constructor->name);
        if (BLT_FAILED(result)) {
            /* if the type is not found, assume audio/pcm */
            BLT_PcmMediaType pcm_type;
            BLT_PcmMediaType_Init(&pcm_type);
            BLT_MediaType_Clone((BLT_MediaType*)&pcm_type, &output->media_type);
        }
    } else {
        /* use the media type from the input spec */
        BLT_MediaType_Clone(constructor->spec.input.media_type,
                            &output->media_type);
    }

    /* create the output file object */
    result = ATX_File_Create(constructor->name+5, &output->file);
    if (BLT_FAILED(result)) {
        ATX_CLEAR_OBJECT(&output->file);
        goto failure;
    }

    /* open the output file */
    result = ATX_File_Open(&output->file,
                           ATX_FILE_OPEN_MODE_WRITE  |
                           ATX_FILE_OPEN_MODE_CREATE |
                           ATX_FILE_OPEN_MODE_TRUNCATE);
    if (ATX_FAILED(result)) goto failure;

    /* get the output stream */
    result = ATX_File_GetOutputStream(&output->file, &output->stream);
    if (BLT_FAILED(result)) {
        ATX_CLEAR_OBJECT(&output->stream);
        goto failure;
    }

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)output;
    ATX_INTERFACE(object) = (ATX_Interface*)&FileOutput_BLT_MediaNodeInterface;

    return BLT_SUCCESS;

 failure:
    FileOutput_Destroy(output);
    ATX_CLEAR_OBJECT(object);
    return result;
}

/*----------------------------------------------------------------------
|    FileOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FileOutput_Destroy(FileOutput* output)
{
    BLT_Debug("FileOutput::Destroy\n");

    /* release the stream */
    ATX_RELEASE_OBJECT(&output->stream);

    /* destroy the file */
    ATX_DESTROY_OBJECT(&output->file);

    /* free the media type extensions */
    BLT_MediaType_Free(output->media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&output->base);

    /* free the object memory */
    ATX_FreeMemory(output);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       FileOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FileOutput_GetPortByName(BLT_MediaNodeInstance* instance,
                         BLT_CString            name,
                         BLT_MediaPort*         port)
{
    FileOutput* output = (FileOutput*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)output;
        ATX_INTERFACE(port) = &FileOutput_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FileOutput, 
                                         "input", 
                                         STREAM_PUSH,
                                         IN)
static const BLT_MediaPortInterface
FileOutput_BLT_MediaPortInterface = {
    FileOutput_GetInterface,
    FileOutput_GetName,
    FileOutput_GetProtocol,
    FileOutput_GetDirection,
    FileOutput_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_OutputStreamProvider interface
+---------------------------------------------------------------------*/
static const BLT_OutputStreamProviderInterface
FileOutput_BLT_OutputStreamProviderInterface = {
    FileOutput_GetInterface,
    FileOutput_GetStream
};

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
FileOutput_BLT_MediaNodeInterface = {
    FileOutput_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    FileOutput_GetPortByName,
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
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FileOutput, base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutput)
ATX_INTERFACE_MAP_ADD(FileOutput, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(FileOutput, ATX_Referenceable)
ATX_INTERFACE_MAP_ADD(FileOutput, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(FileOutput, BLT_OutputStreamProvider)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutput)

/*----------------------------------------------------------------------
|       FileOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FileOutputModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* the input protocol should be STREAM_PUSH and the */
            /* output protocol should be NONE                   */
            if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PUSH) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* we need a name */
            if (constructor->name == NULL) {
                return BLT_FAILURE;
            }

            /* the name needs to be file:<filename> */
            if (!ATX_StringsEqualN(constructor->name, "file:", 5)) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            BLT_Debug("FileOutputModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FileOutput)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface FileOutputModule_BLT_ModuleInterface = {
    FileOutputModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    FileOutputModule_CreateInstance,
    FileOutputModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FileOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(FileOutputModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutputModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutputModule) 
ATX_INTERFACE_MAP_ADD(FileOutputModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(FileOutputModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(FileOutputModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_FileOutputModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("File Output", NULL, 0,
                                 &FileOutputModule_BLT_ModuleInterface,
                                 object);
}
