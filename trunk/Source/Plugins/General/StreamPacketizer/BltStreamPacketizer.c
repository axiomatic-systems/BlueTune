/*****************************************************************
|
|      File: BltStreamPacketizer.c
|
|      Stream Packetizer Module
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
#include "BltStreamPacketizer.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"
#include "BltMediaPort.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltByteStreamUser.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerModule)
static const BLT_ModuleInterface StreamPacketizerModule_BLT_ModuleInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizer)
static const BLT_MediaNodeInterface StreamPacketizer_BLT_MediaNodeInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerInputPort)
static const BLT_MediaPortInterface StreamPacketizerInputPort_BLT_MediaPortInterface;

ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerOutputPort)
static const BLT_MediaPortInterface StreamPacketizerOutputPort_BLT_MediaPortInterface;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_BaseModule base;
} StreamPacketizerModule;

typedef struct {
    BLT_BaseMediaNode base;
    ATX_InputStream   stream;
    BLT_Size          packet_size;
    BLT_Cardinal      packet_count;
    ATX_Int64         sample_count;
    BLT_MediaType*    media_type;
    BLT_Boolean       eos;
} StreamPacketizer;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
#define BLT_STREAM_PACKETIZER_DEFAULT_PACKET_SIZE  4096
#define BLT_STREAM_PACKETIZER_DEFAULT_PACKET_SIZE_24BITS 6144 /* 24*256 */

/*----------------------------------------------------------------------
|       StreamPacketizerInputPort_SetStream
+---------------------------------------------------------------------*/
BLT_METHOD
StreamPacketizerInputPort_SetStream(BLT_InputStreamUserInstance* instance,
                                    ATX_InputStream*             stream,
                                    const BLT_MediaType*         media_type)
{
    StreamPacketizer* packetizer = (StreamPacketizer*)instance;

    /* if we had a stream, release it */
    ATX_RELEASE_OBJECT(&packetizer->stream);

    /* keep a reference to the stream */
    packetizer->stream = *stream;
    ATX_REFERENCE_OBJECT(stream);

    /* keep the media type */
    BLT_MediaType_Free(packetizer->media_type);
    if (media_type) {
        BLT_MediaType_Clone(media_type, &packetizer->media_type);

        /* update the packet size if we're in 24 bits per sample */
        if (media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
            const BLT_PcmMediaType* pcm_type = (const BLT_PcmMediaType*)media_type;
            if (((pcm_type->bits_per_sample+7)/8) == 3) {
                packetizer->packet_size = BLT_STREAM_PACKETIZER_DEFAULT_PACKET_SIZE_24BITS;
            }
        }
    } else {
        BLT_MediaType_Clone(&BLT_MediaType_Unknown, &packetizer->media_type);
    }
    
    /* reset the packet count */
    packetizer->packet_count = 0;
    ATX_Int64_Set_Int32(packetizer->sample_count, 0);

    /* reset the eos flag */
    packetizer->eos = BLT_FALSE;

    /* update the stream info */
    {
        BLT_StreamInfo info;
        BLT_Result     result;

        result = ATX_InputStream_GetSize(stream, &info.size);
        if (BLT_SUCCEEDED(result)) {
            if (!ATX_OBJECT_IS_NULL(&packetizer->base.context)) {
                info.mask = BLT_STREAM_INFO_MASK_SIZE;
                BLT_Stream_SetInfo(&packetizer->base.context, &info);
            }
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_InputStreamUser interface
+---------------------------------------------------------------------*/
static const BLT_InputStreamUserInterface
StreamPacketizerInputPort_BLT_InputStreamUserInterface = {
    StreamPacketizerInputPort_GetInterface,
    StreamPacketizerInputPort_SetStream
};

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(StreamPacketizerInputPort,
                                         "input",
                                         STREAM_PULL,
                                         IN)
static const BLT_MediaPortInterface
StreamPacketizerInputPort_BLT_MediaPortInterface = {
    StreamPacketizerInputPort_GetInterface,
    StreamPacketizerInputPort_GetName,
    StreamPacketizerInputPort_GetProtocol,
    StreamPacketizerInputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerInputPort)
ATX_INTERFACE_MAP_ADD(StreamPacketizerInputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(StreamPacketizerInputPort, BLT_InputStreamUser)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerInputPort)

/*----------------------------------------------------------------------
|    StreamPacketizerOutputPort_GetPacket
+---------------------------------------------------------------------*/
BLT_METHOD
StreamPacketizerOutputPort_GetPacket(BLT_PacketProducerInstance* instance,
                                     BLT_MediaPacket**           packet)
{
    StreamPacketizer* packetizer = (StreamPacketizer*)instance;
    BLT_Any           buffer;
    BLT_Size          bytes_read;
    BLT_Result        result;

    /* check for EOS */
    if (packetizer->eos) {
        *packet = NULL;
        return BLT_ERROR_EOS;
    }

    /* get a packet from the core */
    result = BLT_Core_CreateMediaPacket(&packetizer->base.core,
                                        packetizer->packet_size,
                                        packetizer->media_type,
                                        packet);
    if (BLT_FAILED(result)) return result;

    /* get the addr of the buffer */
    buffer = BLT_MediaPacket_GetPayloadBuffer(*packet);

    /* read the data from the input stream */
    result = ATX_InputStream_Read(&packetizer->stream,
                                  buffer,
                                  packetizer->packet_size,
                                  &bytes_read);
    if (BLT_FAILED(result)) {
        if (result == BLT_ERROR_EOS) {
            packetizer->eos = BLT_TRUE;
            bytes_read = 0;
            BLT_MediaPacket_SetFlags(*packet, 
                                     BLT_MEDIA_PACKET_FLAG_END_OF_STREAM);
        } else {
            BLT_MediaPacket_Release(*packet);
            *packet = NULL;
            return result;
        }
    }

    /* update the size of the packet */
    BLT_MediaPacket_SetPayloadSize(*packet, bytes_read);

    /* set flags */     
    if (packetizer->packet_count == 0) {
        /* this is the first packet */
        BLT_MediaPacket_SetFlags(*packet,
                                 BLT_MEDIA_PACKET_FLAG_START_OF_STREAM);
    }

    /* update the packet count */
    packetizer->packet_count++;

    /* update the sample count and timestamp */
    if (packetizer->media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        BLT_PcmMediaType* pcm_type = (BLT_PcmMediaType*)packetizer->media_type;
        if (pcm_type->channel_count   != 0 && 
            pcm_type->bits_per_sample != 0 &&
            pcm_type->sample_rate     != 0) {
            BLT_UInt32 sample_count;

            /* compute time stamp */
            BLT_TimeStamp time_stamp;
            BLT_TimeStamp_FromSamples(&time_stamp, 
                                      packetizer->sample_count,
                                      pcm_type->sample_rate);
            BLT_MediaPacket_SetTimeStamp(*packet, time_stamp);

            /* update sample count */
            sample_count = bytes_read/(pcm_type->channel_count*
                                       pcm_type->bits_per_sample/8);
            ATX_Int64_Add_Int32(packetizer->sample_count, sample_count);
        }
    } 

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(StreamPacketizerOutputPort,
                                         "output",
                                         PACKET,
                                         OUT)
static const BLT_MediaPortInterface
StreamPacketizerOutputPort_BLT_MediaPortInterface = {
    StreamPacketizerOutputPort_GetInterface,
    StreamPacketizerOutputPort_GetName,
    StreamPacketizerOutputPort_GetProtocol,
    StreamPacketizerOutputPort_GetDirection,
    BLT_MediaPort_DefaultQueryMediaType
};

/*----------------------------------------------------------------------
|    BLT_PacketProducer interface
+---------------------------------------------------------------------*/
static const BLT_PacketProducerInterface
StreamPacketizerOutputPort_BLT_PacketProducerInterface = {
    StreamPacketizerOutputPort_GetInterface,
    StreamPacketizerOutputPort_GetPacket
};

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerOutputPort)
ATX_INTERFACE_MAP_ADD(StreamPacketizerOutputPort, BLT_MediaPort)
ATX_INTERFACE_MAP_ADD(StreamPacketizerOutputPort, BLT_PacketProducer)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerOutputPort)

/*----------------------------------------------------------------------
|    StreamPacketizer_Create
+---------------------------------------------------------------------*/
static BLT_Result
StreamPacketizer_Create(BLT_Module*              module,
                        BLT_Core*                core, 
                        BLT_ModuleParametersType parameters_type,
                        BLT_CString              parameters, 
                        ATX_Object*              object)
{
    StreamPacketizer* packetizer;

    BLT_Debug("StreamPacketizer::Create\n");

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    packetizer = ATX_AllocateZeroMemory(sizeof(StreamPacketizer));
    if (packetizer == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&packetizer->base, module, core);

    /* construct the object */
    ATX_CLEAR_OBJECT(&packetizer->stream);
    BLT_MediaType_Clone(&BLT_MediaType_None, &packetizer->media_type);
    packetizer->packet_size  = BLT_STREAM_PACKETIZER_DEFAULT_PACKET_SIZE;
    packetizer->packet_count = 0;
    packetizer->eos          = BLT_FALSE;

    /* construct reference */
    ATX_INSTANCE(object)  = (ATX_Instance*)packetizer;
    ATX_INTERFACE(object) = (ATX_Interface*)&StreamPacketizer_BLT_MediaNodeInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    StreamPacketizer_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
StreamPacketizer_Destroy(StreamPacketizer* packetizer)
{
    BLT_Debug("StreamPacketizer::Destroy\n");

    /* release the reference to the stream */
    ATX_RELEASE_OBJECT(&packetizer->stream);

    /* free the media type extensions */
    BLT_MediaType_Free(packetizer->media_type);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&packetizer->base);

    /* free the object memory */
    ATX_FreeMemory(packetizer);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       StreamPacketizer_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
StreamPacketizer_GetPortByName(BLT_MediaNodeInstance* instance,
                               BLT_CString            name,
                               BLT_MediaPort*         port)
{
    StreamPacketizer* packetizer = (StreamPacketizer*)instance;

    if (ATX_StringsEqual(name, "input")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)packetizer;
        ATX_INTERFACE(port) = &StreamPacketizerInputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else if (ATX_StringsEqual(name, "output")) {
        ATX_INSTANCE(port)  = (BLT_MediaPortInstance*)packetizer;
        ATX_INTERFACE(port) = &StreamPacketizerOutputPort_BLT_MediaPortInterface; 
        return BLT_SUCCESS;
    } else {
        ATX_CLEAR_OBJECT(port);
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    StreamPacketizer_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
StreamPacketizer_Seek(BLT_MediaNodeInstance* instance,
                      BLT_SeekMode*          mode,
                      BLT_SeekPoint*         point)
{
    StreamPacketizer* packetizer = (StreamPacketizer*)instance;
    BLT_Result result;

    /* clear any end-of-stream condition */
    packetizer->eos = BLT_FALSE;

    /* estimate the seek offset from the other stream parameters */
    result = BLT_Stream_EstimateSeekPoint(&packetizer->base.context, *mode, point);
    if (BLT_FAILED(result)) return result;

    BLT_Debug("StreamPacketizer_Seek: seek offset = %d\n", (int)point->offset);

    /* seek into the input stream (ignore return value) */
    ATX_InputStream_Seek(&packetizer->stream, point->offset);

    /* update the current sample */
    if (point->mask & BLT_SEEK_POINT_MASK_SAMPLE) {
        packetizer->sample_count = point->sample;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
static const BLT_MediaNodeInterface
StreamPacketizer_BLT_MediaNodeInterface = {
    StreamPacketizer_GetInterface,
    BLT_BaseMediaNode_GetInfo,
    StreamPacketizer_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    BLT_BaseMediaNode_Start,
    BLT_BaseMediaNode_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    StreamPacketizer_Seek
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(StreamPacketizer, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizer)
ATX_INTERFACE_MAP_ADD(StreamPacketizer, BLT_MediaNode)
ATX_INTERFACE_MAP_ADD(StreamPacketizer, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizer)

/*----------------------------------------------------------------------
|       StreamPacketizerModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
StreamPacketizerModule_Probe(BLT_ModuleInstance*      instance, 
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

            /* the input protocol should be  STREAM_PULL and the */
            /* output protocol should be PACKET                  */
             if ((constructor->spec.input.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL) ||
                (constructor->spec.output.protocol !=
                 BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != 
                 BLT_MEDIA_PORT_PROTOCOL_PACKET)) {
                return BLT_FAILURE;
            }

            /* media types must match */
            if (constructor->spec.input.media_type->id !=
                BLT_MEDIA_TYPE_ID_UNKNOWN &&
                constructor->spec.output.media_type->id !=
                BLT_MEDIA_TYPE_ID_UNKNOWN &&
                constructor->spec.input.media_type->id !=
                constructor->spec.output.media_type->id) {
                return BLT_FAILURE;
            }

            /* compute the match level */
            if (constructor->name != NULL) {
                /* we're being probed by name */
                if (ATX_StringsEqual(constructor->name, "StreamPacketizer")) {
                    /* our name */
                    *match = BLT_MODULE_PROBE_MATCH_EXACT;
                } else {
                    /* not our name */
                    return BLT_FAILURE;
                }
            } else {
                /* we're probed by protocol/type specs only */
                *match = BLT_MODULE_PROBE_MATCH_DEFAULT;
            }

            BLT_Debug("StreamPacketizerModule::Probe - Ok [%d]\n", *match);
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
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(StreamPacketizer)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
static const BLT_ModuleInterface StreamPacketizerModule_BLT_ModuleInterface = {
    StreamPacketizerModule_GetInterface,
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    StreamPacketizerModule_CreateInstance,
    StreamPacketizerModule_Probe
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define StreamPacketizerModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_SIMPLE_REFERENCEABLE_INTERFACE(StreamPacketizerModule, 
                                             base.reference_count)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerModule)
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerModule) 
ATX_INTERFACE_MAP_ADD(StreamPacketizerModule, BLT_Module)
ATX_INTERFACE_MAP_ADD(StreamPacketizerModule, ATX_Referenceable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(StreamPacketizerModule)

/*----------------------------------------------------------------------
|       module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_StreamPacketizerModule_GetModuleObject(BLT_Module* object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Stream Packetizer", NULL, 0,
                                 &StreamPacketizerModule_BLT_ModuleInterface,
                                 object);
}
