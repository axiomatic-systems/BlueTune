/*****************************************************************
|
|   BlueTune - File Input Module
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltFileInput.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltModule.h"
#include "BltByteStreamProvider.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.inputs.file")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} FileInputModule;

typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);
    
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_InputStreamProvider);

    /* members */
    ATX_File*        file;
    ATX_InputStream* stream;
    BLT_MediaType*   media_type;
} FileInput;

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(FileInputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(FileInput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(FileInput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(FileInput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(FileInput, BLT_InputStreamProvider)
static BLT_Result FileInput_Destroy(FileInput* self);

/*----------------------------------------------------------------------
|    FileInput_DecideMediaType
+---------------------------------------------------------------------*/
static BLT_Result
FileInput_DecideMediaType(FileInput* self, BLT_CString name)
{
    BLT_Registry* registry;
    BLT_CString   extension;
    BLT_Result    result;

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
    result = BLT_Core_GetRegistry(ATX_BASE(self, BLT_BaseMediaNode).core, &registry);
    if (BLT_FAILED(result)) return result;

    /* query the registry */
    return BLT_Registry_GetMediaTypeIdForExtension(registry, 
                                                   extension, 
                                                   &self->media_type->id);
}

/*----------------------------------------------------------------------
|    FileInput_Create
+---------------------------------------------------------------------*/
static BLT_Result
FileInput_Create(BLT_Module*              module,
                 BLT_Core*                core, 
                 BLT_ModuleParametersType parameters_type,
                 BLT_AnyConst             parameters, 
                 BLT_MediaNode**          object)
{
    FileInput*                input;
    BLT_MediaNodeConstructor* constructor = 
        (BLT_MediaNodeConstructor*)parameters;
    BLT_Result                result;

    ATX_LOG_FINE("FileInput::Create");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    input = (FileInput*)ATX_AllocateZeroMemory(sizeof(FileInput));
    if (input == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(input, BLT_BaseMediaNode), module, core);
    
    /* strip the "file:" prefix if it is present */
    if (ATX_StringsEqualN(constructor->name, "file:", 5)) {
        constructor->name += 5;
    }

    /* create the file object */
    result = ATX_File_Create(constructor->name, &input->file);
    if (ATX_FAILED(result)) {
        input->file = NULL;
        goto failure;
    }

    /* open the file */
    result = ATX_File_Open(input->file, ATX_FILE_OPEN_MODE_READ);
    if (ATX_FAILED(result)) goto failure;

    /* get the input stream */
    result = ATX_File_GetInputStream(input->file, &input->stream);
    if (ATX_FAILED(result)) {
        input->stream = NULL;
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
    ATX_SET_INTERFACE_EX(input, FileInput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(input, FileInput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (input, FileInput, BLT_MediaPort);
    ATX_SET_INTERFACE   (input, FileInput, BLT_InputStreamProvider);
    *object = &ATX_BASE_EX(input, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;

failure:
    FileInput_Destroy(input);
    object = NULL;
    return result;
}

/*----------------------------------------------------------------------
|    FileInput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
FileInput_Destroy(FileInput* self)
{
    ATX_LOG_FINE("FileInput::Destroy");

    /* release the byte stream */
    ATX_RELEASE_OBJECT(self->stream);
    
    /* destroy the file */
    ATX_DESTROY_OBJECT(self->file);

    /* free the media type extensions */
    BLT_MediaType_Free(self->media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FileInput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_GetPortByName(BLT_MediaNode*  _self,
                        BLT_CString     name,
                        BLT_MediaPort** port)
{
    FileInput* self = ATX_SELF_EX(FileInput, BLT_BaseMediaNode, BLT_MediaNode);
    if (ATX_StringsEqual(name, "output")) {
        /* we implement the BLT_MediaPort interface ourselves */
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    FileInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_QueryMediaType(BLT_MediaPort*        _self,
                         BLT_Ordinal           index,
                         const BLT_MediaType** media_type)
{
    FileInput* self = ATX_SELF(FileInput, BLT_MediaPort);
    
    if (index == 0) {
        *media_type = self->media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   FileInput_GetStream
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_GetStream(BLT_InputStreamProvider* _self,
                    ATX_InputStream**        stream)
{
    FileInput* self = ATX_SELF(FileInput, BLT_InputStreamProvider);

    /* return our stream object */
    *stream = self->stream;
    ATX_REFERENCE_OBJECT(*stream);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    FileInput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
FileInput_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    FileInput* self = ATX_SELF_EX(FileInput, BLT_BaseMediaNode, BLT_MediaNode);

    /* update the stream info */
    {
        BLT_StreamInfo info;
        ATX_Size       file_size;
        BLT_Result     result;

        result = ATX_File_GetSize(self->file, &file_size);
        if (BLT_SUCCEEDED(result)) {
            info.mask = BLT_STREAM_INFO_MASK_SIZE;
            info.size = (BLT_UInt32)file_size;
            BLT_Stream_SetInfo(stream, &info);
        }
    }
    
    /* keep the stream as our context */
    ATX_BASE(self, BLT_BaseMediaNode).context = stream;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FileInput)
    ATX_GET_INTERFACE_ACCEPT_EX(FileInput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(FileInput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (FileInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT   (FileInput, BLT_InputStreamProvider)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(FileInput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    FileInput_GetPortByName,
    FileInput_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(FileInput, 
                                         "output", 
                                         STREAM_PULL, 
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(FileInput, BLT_MediaPort)
    FileInput_GetName,
    FileInput_GetProtocol,
    FileInput_GetDirection,
    FileInput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_InputStreamProvider interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(FileInput, BLT_InputStreamProvider)
    FileInput_GetStream
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(FileInput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   FileInputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
FileInputModule_Probe(BLT_Module*              self, 
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

            ATX_LOG_FINE_1("FileInputModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(FileInputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(FileInputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(FileInputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(FileInputModule, FileInput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(FileInputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    FileInputModule_CreateInstance,
    FileInputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define FileInputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(FileInputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_FileInputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("File Input", NULL, 0,
                                 &FileInputModule_BLT_ModuleInterface,
                                 &FileInputModule_ATX_ReferenceableInterface,
                                 object);
}
