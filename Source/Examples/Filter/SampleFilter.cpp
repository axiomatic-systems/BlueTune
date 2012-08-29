/*****************************************************************
|
|   Sample Filter Module
|
|   This is a very simple example of a BlueTune media node that acts
|   as a PCM filter, implementing a naive implementation of an FIR
|   filter for illustration purposes.
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BlueTune.h"
#include "Neptune.h" // this sample uses a few Neptune functions
#include "SampleFilter.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("sample.filter")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef BLT_BaseModule SampleFilterModule;

// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketConsumer);
} SampleFilterInput;

// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_PacketProducer);

    /* members */
    BLT_MediaPacket* packet;
} SampleFilterOutput;

// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(ATX_PropertyListener);

    /* members */
    SampleFilterInput          input;
    SampleFilterOutput         output;
    
    /* FIR coefficients and buffers */
    double*                    coefficients;
    unsigned int               coefficient_count;
    short*                     buffer;
    short*                     workspace;
    unsigned int               buffer_size;
    
    ATX_PropertyListenerHandle property_listener_handle;
} SampleFilter;

/*----------------------------------------------------------------------
|    SampleFilter_SetCoefficients
|
|    This function is not part of the standard node interface.
|    It is a custom function specific to this sample filter node.
+---------------------------------------------------------------------*/
static void
SampleFilter_SetCoefficients(SampleFilter* self, const char* coefficients)
{
    // free existing coefficients
    if (self->coefficients) {
        ATX_LOG_INFO("clearing coefficients");
        delete[] self->coefficients;
        self->coefficients = NULL;
        self->coefficient_count = 0;
    }
    
    if (coefficients) {
        ATX_LOG_INFO_1("new coefficients: %s", coefficients);
        NPT_DataBuffer coef_buffer;
        NPT_HexToBytes(coefficients, coef_buffer);
        const unsigned char* coef_bytes = coef_buffer.GetData();
        self->coefficient_count = coef_buffer.GetDataSize()/4;
        if (self->coefficient_count) {
            self->coefficients = new double[self->coefficient_count];
            for (unsigned int i=0; i<self->coefficient_count; i++) {
                NPT_Int32 fixed = (NPT_Int32)NPT_BytesToInt32Be(&coef_bytes[i*4]);
                self->coefficients[i] = ((double)fixed)/65536.0;
                ATX_LOG_FINE_2("coef %d = %f", i, (float)self->coefficients[i]);
            }
        }
    }
}

/*----------------------------------------------------------------------
|    SampleFilterInput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilterInput_PutPacket(BLT_PacketConsumer* _self,
                            BLT_MediaPacket*    packet)
{
    SampleFilter* self = ATX_SELF_M(input, SampleFilter, BLT_PacketConsumer);
    BLT_PcmMediaType*  media_type;
    BLT_Result         result;

    ATX_LOG_FINER("received packet");

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)(const void*)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        ATX_LOG_FINE("rejecting non-PCM packet");
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    BLT_PcmMediaType* pcm_type = (BLT_PcmMediaType*)media_type;
    
    /* keep a reference to the packet */
    self->output.packet = packet;
    BLT_MediaPacket_AddReference(packet);

    /* exit now if we're inactive (no coefficients set) */
    if (!self->coefficients) {
        ATX_LOG_FINER("filter inactive, leaving packet unmodified");
        return BLT_SUCCESS;
    }

    /* we only support 16-bit signed ints in native-endian format */
    if (media_type->bits_per_sample != 16 ||
        media_type->channel_count == 0 ||
        media_type->sample_format != BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE) {
        ATX_LOG_FINER("unsupported PCM format, leaving packet unmodified");
        return BLT_SUCCESS;
    }

    /* check that the packet looks OK to be processed */
    if (BLT_MediaPacket_GetPayloadSize(packet) < 2) {
        // less than one sample, skip this
        return BLT_SUCCESS;
    }

    /* ensure we have the proper buffer in place */
    unsigned int channel_count = pcm_type->channel_count;
    unsigned int sample_count  = BLT_MediaPacket_GetPayloadSize(packet)/(channel_count*2);
    unsigned int buffer_needed = (self->coefficient_count-1)*channel_count*2;
    if (self->buffer_size < buffer_needed) {
        delete[] self->buffer;
        delete[] self->workspace;
        self->buffer      = new short[buffer_needed];
        self->workspace   = new short[buffer_needed];
        self->buffer_size = buffer_needed;
        NPT_SetMemory(self->buffer, 0, self->buffer_size);
    }
    
    /* save the last samples for the next round */
    short* pcm = (short*)BLT_MediaPacket_GetPayloadBuffer(packet);
    NPT_CopyMemory(self->workspace, self->buffer, self->buffer_size);
    NPT_CopyMemory(self->buffer, &pcm[channel_count*(sample_count-(self->coefficient_count-1))], self->buffer_size);
    
    /* apply the FIR coefficients */
    for (unsigned int c=0; c<channel_count; c++) {
        for (int i=sample_count-1; i>=0; i--) {
            double out = 0.0;
            for (int x=0; x<(int)self->coefficient_count; x++) {
                double sample;
                if (i >= x) {
                    sample = (double)pcm[c+channel_count*(i-x)];
                } else {
                    sample = (double)self->workspace[c+channel_count*((self->coefficient_count-1)-x+i)];
                }
                out += sample*self->coefficients[x];
            }
            pcm[c+channel_count*i] = (short)out;
        }
    }
            
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SampleFilterInput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilterInput_QueryMediaType(BLT_MediaPort*         self,
                                 BLT_Ordinal            index,
                                 const BLT_MediaType**  media_type)
{
    BLT_COMPILER_UNUSED(self);
    if (index == 0) {
        *media_type = &BLT_GenericPcmMediaType;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SampleFilterInput)
    ATX_GET_INTERFACE_ACCEPT(SampleFilterInput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SampleFilterInput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SampleFilterInput, BLT_PacketConsumer)
    SampleFilterInput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SampleFilterInput,
                                         "input",
                                         PACKET,
                                         IN)
ATX_BEGIN_INTERFACE_MAP(SampleFilterInput, BLT_MediaPort)
    SampleFilterInput_GetName,
    SampleFilterInput_GetProtocol,
    SampleFilterInput_GetDirection,
    SampleFilterInput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    SampleFilterOutput_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilterOutput_GetPacket(BLT_PacketProducer* _self,
                             BLT_MediaPacket**   packet)
{
    SampleFilter* self = ATX_SELF_M(output, SampleFilter, BLT_PacketProducer);

    if (self->output.packet) {
        ATX_LOG_FINER("returning pending packet");
        *packet = self->output.packet;
        self->output.packet = NULL;
        return BLT_SUCCESS;
    } else {
        ATX_LOG_FINER("no packet available");
        *packet = NULL;
        return BLT_ERROR_PORT_HAS_NO_DATA;
    }
}

/*----------------------------------------------------------------------
|   SampleFilterOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilterOutput_QueryMediaType(BLT_MediaPort*        self,
                                  BLT_Ordinal           index,
                                  const BLT_MediaType** media_type)
{
    BLT_COMPILER_UNUSED(self);
    if (index == 0) {
        *media_type = &BLT_GenericPcmMediaType;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SampleFilterOutput)
    ATX_GET_INTERFACE_ACCEPT(SampleFilterOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT(SampleFilterOutput, BLT_PacketProducer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(SampleFilterOutput,
                                         "output",
                                         PACKET,
                                         OUT)
ATX_BEGIN_INTERFACE_MAP(SampleFilterOutput, BLT_MediaPort)
    SampleFilterOutput_GetName,
    SampleFilterOutput_GetProtocol,
    SampleFilterOutput_GetDirection,
    SampleFilterOutput_QueryMediaType
ATX_END_INTERFACE_MAP


/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SampleFilterOutput, BLT_PacketProducer)
    SampleFilterOutput_GetPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    SampleFilter_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
SampleFilter_Destroy(SampleFilter* self)
{ 
    ATX_LOG_INFO("destroying sample filter node instance");

    /* release any input packet we may hold */
    if (self->output.packet) {
        BLT_MediaPacket_Release(self->output.packet);
    }

    /* release allocated buffers */
    delete[] self->coefficients;
    delete[] self->buffer;
    delete[] self->workspace;
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory((void*)self);

    return BLT_SUCCESS;
}
                    
/*----------------------------------------------------------------------
|   SampleFilter_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilter_GetPortByName(BLT_MediaNode*  _self,
                           BLT_CString     name,
                           BLT_MediaPort** port)
{
    SampleFilter* self = ATX_SELF_EX(SampleFilter, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(&self->input, BLT_MediaPort);
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        *port = &ATX_BASE(&self->output, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    SampleFilter_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilter_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    SampleFilter* self = ATX_SELF_EX(SampleFilter, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_INFO("activating");
    
    /* keep a reference to the stream */
    ATX_BASE(self, BLT_BaseMediaNode).context = stream;

    /* listen to settings on the new stream */
    if (stream) {
        ATX_Properties* properties;
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).context, 
                                                   &properties))) {
            ATX_PropertyValue property;
            ATX_Properties_AddListener(properties,
                                       SAMPLE_FILTER_COEFFICIENTS_PROPERTY,
                                       &ATX_BASE(self, ATX_PropertyListener),
                                       &self->property_listener_handle);

            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(
                    properties,
                    SAMPLE_FILTER_COEFFICIENTS_PROPERTY,
                    &property)) &&
                property.type == ATX_PROPERTY_VALUE_TYPE_STRING) {
                SampleFilter_SetCoefficients(self, property.data.string);
            }
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SampleFilter_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilter_Deactivate(BLT_MediaNode* _self)
{
    SampleFilter* self = ATX_SELF_EX(SampleFilter, BLT_BaseMediaNode, BLT_MediaNode);

    ATX_LOG_INFO("de-activating");

    /* remove our listener */
    if (ATX_BASE(self, BLT_BaseMediaNode).context) {
        ATX_Properties* properties;
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).context, &properties))) {
            ATX_Properties_RemoveListener(properties, &self->property_listener_handle);
        }
    }

    /* we're detached from the stream */
    ATX_BASE(self, BLT_BaseMediaNode).context = NULL;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SampleFilter_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilter_Seek(BLT_MediaNode* _self,
                  BLT_SeekMode*  mode,
                  BLT_SeekPoint* point)
{
    SampleFilter* self = ATX_SELF_EX(SampleFilter, BLT_BaseMediaNode, BLT_MediaNode);

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    ATX_LOG_INFO("seek received, flushing internal state");

    /* reset filter state */
    NPT_SetMemory(self->buffer, 0, self->buffer_size);
    
    /* release any pending output packet */
    if (self->output.packet) {
        BLT_MediaPacket_Release(self->output.packet);
        self->output.packet = NULL;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    SampleFilter_OnPropertyChanged
+---------------------------------------------------------------------*/
BLT_VOID_METHOD
SampleFilter_OnPropertyChanged(ATX_PropertyListener*    _self,
                               ATX_CString              name,
                               const ATX_PropertyValue* value)
{
    SampleFilter* self = ATX_SELF(SampleFilter, ATX_PropertyListener);
    
    (void)self;
    (void)value;
    
    if (name) {
        ATX_LOG_INFO_1("property changed (%s)", name);
        if (value) {
            // check the type
            if (value->type == ATX_PROPERTY_VALUE_TYPE_STRING) {
                SampleFilter_SetCoefficients(self, value->data.string);
            }
        } else {
            // the property has been removed
            SampleFilter_SetCoefficients(self, NULL);
        }
    }
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SampleFilter)
    ATX_GET_INTERFACE_ACCEPT_EX(SampleFilter, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(SampleFilter, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(SampleFilter, ATX_PropertyListener)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(SampleFilter, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    SampleFilter_GetPortByName,
    SampleFilter_Activate,
    SampleFilter_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    SampleFilter_Seek
};

/*----------------------------------------------------------------------
|    ATX_PropertyListener interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SampleFilter, ATX_PropertyListener)
    SampleFilter_OnPropertyChanged,
};

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(SampleFilter, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|    SampleFilter_Create
+---------------------------------------------------------------------*/
static BLT_Result
SampleFilter_Create(BLT_Module*              module,
                    BLT_Core*                core,
                    BLT_ModuleParametersType parameters_type,
                    BLT_AnyConst             parameters,
                    BLT_MediaNode**          object)
{
    ATX_LOG_INFO("creating SampleFilter node instance");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    SampleFilter* self = (SampleFilter*)ATX_AllocateZeroMemory(sizeof(SampleFilter));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, SampleFilter, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, SampleFilter, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE(self, SampleFilter, ATX_PropertyListener);
    ATX_SET_INTERFACE(&self->input,  SampleFilterInput,  BLT_MediaPort);
    ATX_SET_INTERFACE(&self->input,  SampleFilterInput,  BLT_PacketConsumer);
    ATX_SET_INTERFACE(&self->output, SampleFilterOutput, BLT_MediaPort);
    ATX_SET_INTERFACE(&self->output, SampleFilterOutput, BLT_PacketProducer);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   SampleFilterModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
SampleFilterModule_Probe(BLT_Module*              self,
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

            /* we need a name */
            if (constructor->name == NULL ||
                !ATX_StringsEqual(constructor->name, SAMPLE_FILTER_MODULE_NAME)) {
                return BLT_FAILURE;
            }

            /* the input and output protocols should be PACKET */
            if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* the input type should be unspecified, or audio/pcm */
            if (!(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* the output type should be unspecified, or audio/pcm */
            if (!(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.output.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* match level is always exact */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

            ATX_LOG_FINE_1("SampleFilterModule::Probe - Ok [%d]", *match);
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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(SampleFilterModule)
    ATX_GET_INTERFACE_ACCEPT(SampleFilterModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT(SampleFilterModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(SampleFilterModule, SampleFilter)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(SampleFilterModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    SampleFilterModule_CreateInstance,
    SampleFilterModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define SampleFilterModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE(SampleFilterModule, reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(SampleFilterModule,
                                         "Sample Filter",
                                         SAMPLE_FILTER_MODULE_NAME,
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
