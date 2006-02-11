/*****************************************************************
|
|      File: BltGainControlFilter.c
|
|      Gain Control Filter Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <math.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltGainControlFilter.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterModule)
static const BLT_ModuleInterface GainControlFilterModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilter)
static const BLT_MediaNodeInterface GainControlFilter_BLT_MediaNodeInterface;
static const ATX_PropertyListenerInterface 
GainControlFilter_ATX_PropertyListenerInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterInputPort)

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterOutputPort)

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_GAIN_CONTROL_FILTER_FACTOR_RANGE 1024
#define BLT_GAIN_CONTROL_FILTER_MAX_FACTOR   16*1024

#define BLT_GAIN_CONTROL_REPLAY_GAIN_TRACK_VALUE_SET 1
#define BLT_GAIN_CONTROL_REPLAY_GAIN_ALBUM_VALUE_SET 2

#define BLT_GAIN_CONTROL_REPLAY_GAIN_MIN (-2300)
#define BLT_GAIN_CONTROL_REPLAY_GAIN_MAX (1700)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} GainControlFilterModule;

typedef enum {
    BLT_GAIN_CONTROL_FILTER_MODE_INACTIVE,
    BLT_GAIN_CONTROL_FILTER_MODE_AMPLIFY,
    BLT_GAIN_CONTROL_FILTER_MODE_ATTENUATE
} GainControlMode;

typedef struct {
    BLT_BaseMediaNode base;
    BLT_MediaPacket*  packet;
    GainControlMode   mode;
    unsigned short    factor;
    struct {
        BLT_Flags flags;
        int       track_gain;
        int       album_gain;
    }                 replay_gain_info;
    ATX_PropertyListenerHandle track_gain_listener_handle;
    ATX_PropertyListenerHandle album_gain_listener_handle;
} GainControlFilter;

/*----------------------------------------------------------------------
|    GainControlFilterInputPort_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilterInputPort_PutPacket(BLT_PacketConsumerInstance* instance,
                                     BLT_MediaPacket*            packet)
{
    GainControlFilter* filter = (GainControlFilter*)instance;
    BLT_PcmMediaType*  media_type;
    BLT_Cardinal       sample_count;
    short*             pcm;
    BLT_Result         result;

    /*BLT_Debug("GainControlFilterInputPort_PutPacket\n");*/

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
    if (filter->mode == BLT_GAIN_CONTROL_FILTER_MODE_INACTIVE ||
        filter->factor == 0) {
        return BLT_SUCCESS;
    }

    /* for now, we only support 16-bit */
    if (media_type->bits_per_sample != 16) {
        return BLT_SUCCESS;
    }

    /* adjust the gain */
    pcm = (short*)BLT_MediaPacket_GetPayloadBuffer(packet);
    sample_count = BLT_MediaPacket_GetPayloadSize(packet)/2;
    if (filter->mode == BLT_GAIN_CONTROL_FILTER_MODE_AMPLIFY) {
        register unsigned short factor = filter->factor;
        while (sample_count--) {
            *pcm = (*pcm * factor) / BLT_GAIN_CONTROL_FILTER_FACTOR_RANGE;
            pcm++;
        }
    } else {
        register unsigned short factor = filter->factor;
        while (sample_count--) {
            *pcm = (*pcm * BLT_GAIN_CONTROL_FILTER_FACTOR_RANGE)/factor;
            pcm++;
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GainControlFilter_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilterInputPort_QueryMediaType(
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(GainControlFilterInputPort,
                                         "input",
                                         PACKET,
                                         IN)
static const BLT_MediaPortInterface
GainControlFilterInputPort_BLT_MediaPortInterface = {
    GainControlFilterInputPort_GetInterface,
    GainControlFilterInputPort_GetName,
    GainControlFilterInputPort_GetProtocol,
    GainControlFilterInputPort_GetDirection,
    GainControlFilterInputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
static const BLT_PacketConsumerInterface
GainControlFilterInputPort_BLT_PacketConsumerInterface = {
    GainControlFilterInputPort_GetInterface,
    GainControlFilterInputPort_PutPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterInputPort)
ATX_INTERFACE_MAP_ADD(GainControlFilterInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(GainControlFilterInputPort, BLT_PacketConsumer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterInputPort)

/*----------------------------------------------------------------------
|    GainControlFilterOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilterOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                                      BLT_MediaPacket**           packet)
{
    GainControlFilter* filter = (GainControlFilter*)instance;

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
|   GainControlFilterOutputPort_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilterOutputPort_QueryMediaType(
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
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(GainControlFilterOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
GainControlFilterOutputPort_BLT_MediaPortInterface = {
    GainControlFilterOutputPort_GetInterface,
    GainControlFilterOutputPort_GetName,
    GainControlFilterOutputPort_GetProtocol,
    GainControlFilterOutputPort_GetDirection,
    GainControlFilterOutputPort_QueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
GainControlFilterOutputPort_BLT_PacketProducerInterface = {
    GainControlFilterOutputPort_GetInterface,
    GainControlFilterOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterOutputPort)
ATX_INTERFACE_MAP_ADD(GainControlFilterOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(GainControlFilterOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterOutputPort)

/*----------------------------------------------------------------------
|    GainControlFilter_Create
+---------------------------------------------------------------------*/
static BLT_Result
GainControlFilter_Create(BLT_Module*              module,
                         BLT_Core*                core, 
                         BLT_ModuleParametersType parameters_type,
                         BLT_CString              parameters, 
                         ATX_Object*              object)
{
    GainControlFilter* filter;

    BLT_Debug("GainControlFilter::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    filter = ATX_AllocateZeroMemory(sizeof(GainControlFilter));
    if (filter == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&filter->base, module, core);

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)filter;
    ATX_INTERFACE(object) = (ATX_Interface*)&GainControlFilter_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    GainControlFilter_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
GainControlFilter_Destroy(GainControlFilter* filter)
{ 
    BLT_Debug("GainControlFilter::Destroy\n");

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
|       GainControlFilter_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilter_GetPortByName(BLT_MediaNodeInstance* instance,
                                BLT_CString            name,
                                BLT_MediaPort*         port)
{
    GainControlFilter* filter = (GainControlFilter*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)filter;
        ATX_INTERFACE(port) = &GainControlFilterInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)filter;
        ATX_INTERFACE(port) = &GainControlFilterOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    GainControlFilter_DbToFactor
|
|    The input parameter is the gain expressed in 100th of decibels
+---------------------------------------------------------------------*/
static unsigned short
GainControlFilter_DbToFactor(int gain)
{
    double f = 
        (double)BLT_GAIN_CONTROL_FILTER_FACTOR_RANGE *
        pow(10.0, ((double)gain)/2000);
    unsigned int factor = (unsigned int)f; 
    if (f >= BLT_GAIN_CONTROL_FILTER_FACTOR_RANGE && 
        f <= BLT_GAIN_CONTROL_FILTER_MAX_FACTOR) {
        return (unsigned short)factor;    
    } else {
        return BLT_GAIN_CONTROL_FILTER_FACTOR_RANGE;
    }
}

/*----------------------------------------------------------------------
|    GainControlFilter_UpdateReplayGain
+---------------------------------------------------------------------*/
static void
GainControlFilter_UpdateReplayGain(GainControlFilter* filter)
{
    int gain_value = 0;
    if (filter->replay_gain_info.flags & BLT_GAIN_CONTROL_REPLAY_GAIN_ALBUM_VALUE_SET) {
        gain_value = filter->replay_gain_info.album_gain;   
    } else if (filter->replay_gain_info.flags & BLT_GAIN_CONTROL_REPLAY_GAIN_TRACK_VALUE_SET) {
        gain_value = filter->replay_gain_info.track_gain;
    } else {
        gain_value = 0;
    }
    
    /* if the gain is 0, deactivate the filter */
    if (gain_value == 0) {
        /* disable the filter */
        if (filter->factor != 0) {
            BLT_Debug("GainControlFilter::UpdateReplayGain - filter now inactive\n");
        }
        filter->factor = 0;
        filter->mode = BLT_GAIN_CONTROL_FILTER_MODE_INACTIVE;
        return;
    }

    /* convert the gain value into a mode and a factor */
    if (gain_value > 0) {
        filter->mode = BLT_GAIN_CONTROL_FILTER_MODE_AMPLIFY;
        filter->factor = GainControlFilter_DbToFactor(gain_value);
        BLT_Debug("GainControlFilter::UpdateReplayGain - filter amplification = %d\n", filter->factor);
    } else {
        filter->mode = BLT_GAIN_CONTROL_FILTER_MODE_ATTENUATE;
        filter->factor = GainControlFilter_DbToFactor(-gain_value);
        BLT_Debug("GainControlFilter::UpdateReplayGain - filter attenuation = %d\n", filter->factor);
    }
}

/*----------------------------------------------------------------------
|    GainControlFilter_UpdateReplayGainTrackValue
+---------------------------------------------------------------------*/
static void
GainControlFilter_UpdateReplayGainTrackValue(GainControlFilter*       filter,
                                             const ATX_PropertyValue* value)
{
    if (value) {
        filter->replay_gain_info.track_gain = value->integer;
        filter->replay_gain_info.flags |= BLT_GAIN_CONTROL_REPLAY_GAIN_TRACK_VALUE_SET;
    } else {
        filter->replay_gain_info.track_gain = 0;
        filter->replay_gain_info.flags &= ~BLT_GAIN_CONTROL_REPLAY_GAIN_TRACK_VALUE_SET;
    }
    GainControlFilter_UpdateReplayGain(filter);
}

/*----------------------------------------------------------------------
|    GainControlFilter_UpdateReplayGainAlbumValue
+---------------------------------------------------------------------*/
static void
GainControlFilter_UpdateReplayGainAlbumValue(GainControlFilter*       filter,
                                             const ATX_PropertyValue* value)
{
    if (value) {
        filter->replay_gain_info.album_gain = value->integer;
        filter->replay_gain_info.flags |= BLT_GAIN_CONTROL_REPLAY_GAIN_ALBUM_VALUE_SET;
    } else {
        filter->replay_gain_info.album_gain = 0;
        filter->replay_gain_info.flags &= ~BLT_GAIN_CONTROL_REPLAY_GAIN_ALBUM_VALUE_SET;
    }
    GainControlFilter_UpdateReplayGain(filter);
}

/*----------------------------------------------------------------------
|    GainControlFilter_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilter_Activate(BLT_MediaNodeInstance* instance, BLT_Stream* stream)
{
    GainControlFilter* filter = (GainControlFilter*)instance;

    /* keep a reference to the stream */
    filter->base.context = *stream;

    /* listen to settings on the new stream */
    if (!ATX_OBJECT_IS_NULL(&filter->base.context)) {
        ATX_Properties properties;
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(&filter->base.context, 
                                                   &properties))) {
            ATX_Property         property;
            ATX_PropertyListener me;
            ATX_INSTANCE(&me)  = (ATX_PropertyListenerInstance*)filter;
            ATX_INTERFACE(&me) = &GainControlFilter_ATX_PropertyListenerInterface;
            ATX_Properties_AddListener(&properties, 
                                       BLT_REPLAY_GAIN_TRACK_GAIN_VALUE,
                                       &me,
                                       &filter->track_gain_listener_handle);
            ATX_Properties_AddListener(&properties, 
                                       BLT_REPLAY_GAIN_ALBUM_GAIN_VALUE,
                                       &me,
                                       &filter->album_gain_listener_handle);

            /* read the initial values of the replay gain info */
            filter->replay_gain_info.flags = 0;
            filter->replay_gain_info.track_gain = 0;
            filter->replay_gain_info.album_gain = 0;

            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(
                    &properties,
                    BLT_REPLAY_GAIN_TRACK_GAIN_VALUE,
                    &property)) &&
                property.type == ATX_PROPERTY_TYPE_INTEGER) {
                GainControlFilter_UpdateReplayGainTrackValue(filter, &property.value);
            }
            if (ATX_SUCCEEDED(ATX_Properties_GetProperty(
                    &properties,
                    BLT_REPLAY_GAIN_TRACK_GAIN_VALUE,
                    &property)) &&
                property.type == ATX_PROPERTY_TYPE_INTEGER) {
                GainControlFilter_UpdateReplayGainAlbumValue(filter, &property.value);
            }
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    GainControlFilter_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilter_Deactivate(BLT_MediaNodeInstance* instance)
{
    GainControlFilter* filter = (GainControlFilter*)instance;

    /* reset the replay gain info */
    filter->replay_gain_info.flags      = 0;
    filter->replay_gain_info.album_gain = 0;
    filter->replay_gain_info.track_gain = 0;

    /* remove our listener */
    if (!ATX_OBJECT_IS_NULL(&filter->base.context)) {
        ATX_Properties properties;
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(&filter->base.context, 
                                                   &properties))) {
            ATX_Properties_RemoveListener(&properties, 
                                          &filter->track_gain_listener_handle);
            ATX_Properties_RemoveListener(&properties, 
                                          &filter->album_gain_listener_handle);
        }
    }

    /* we're detached from the stream */
    ATX_CLEAR_OBJECT(&filter->base.context);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
GainControlFilter_BLT_MediaNodeInterface = {
    GainControlFilter_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    GainControlFilter_GetPortByName,
    GainControlFilter_Activate,
    GainControlFilter_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
};

/*----------------------------------------------------------------------
|    GainControlFilter_OnPropertyChanged
+---------------------------------------------------------------------*/
BLT_VOID_METHOD
GainControlFilter_OnPropertyChanged(ATX_PropertyListenerInstance* instance,
                                    ATX_CString                   name,
                                    ATX_PropertyType              type,
                                    const ATX_PropertyValue*      value)
{
    GainControlFilter* filter = (GainControlFilter*)instance;

    if (name != NULL && 
        (type == ATX_PROPERTY_TYPE_INTEGER || 
         type == ATX_PROPERTY_TYPE_NONE)) {
        if (ATX_StringsEqual(name, BLT_REPLAY_GAIN_TRACK_GAIN_VALUE)) {
            GainControlFilter_UpdateReplayGainTrackValue(filter, value);
        } else if (ATX_StringsEqual(name, BLT_REPLAY_GAIN_ALBUM_GAIN_VALUE)) {
            GainControlFilter_UpdateReplayGainAlbumValue(filter, value);
        }
    }
}

/*----------------------------------------------------------------------
|    ATX_PropertyListener interface
+---------------------------------------------------------------------*/
static const ATX_PropertyListenerInterface
GainControlFilter_ATX_PropertyListenerInterface = {
    GainControlFilter_GetInterface,
    GainControlFilter_OnPropertyChanged,
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(GainControlFilter, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilter)
ATX_INTERFACE_MAP_ADD(GainControlFilter, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(GainControlFilter, ATX_PropertyListener)
ATX_INTERFACE_MAP_ADD(GainControlFilter, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilter)

/*----------------------------------------------------------------------
|       GainControlFilterModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
GainControlFilterModule_Probe(BLT_ModuleInstance*      instance, 
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
                !ATX_StringsEqual(constructor->name, "GainControlFilter")) {
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

            BLT_Debug("GainControlFilterModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(GainControlFilter)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface GainControlFilterModule_BLT_ModuleInterface = {
    GainControlFilterModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    GainControlFilterModule_CreateInstance,
    GainControlFilterModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define GainControlFilterModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(GainControlFilterModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterModule) 
ATX_INTERFACE_MAP_ADD(GainControlFilterModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(GainControlFilterModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(GainControlFilterModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_GainControlFilterModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Gain Control Filter", NULL, 0,
                                 &GainControlFilterModule_BLT_ModuleInterface,
                                 object);
}
