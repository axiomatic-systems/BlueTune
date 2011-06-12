/*****************************************************************
|
|   OSX Audio Units Output Module
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/

#include "Atomix.h"
#include "Neptune.h"
#include "BltConfig.h"
#include "BltRaopOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"
#include "BltVolumeControl.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.raop")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
const NPT_Timeout BLT_RAOP_DEFAULT_CONNECT_TIMEOUT = 5000; // 5 seconds
const NPT_Timeout BLT_RAOP_DEFAULT_IO_TIMEOUT      = 5000; // 5 seconds

const unsigned int BLT_RAOP_SYNC_PACKET_INTERVAL    = 126;
const float        BLT_RAOP_MAX_DELAY               = 2.0;
const unsigned int BLT_RAOP_MAX_THREAD_WAIT_TIMEOUT = 3000; // 3 seconds
const unsigned int BLT_RAOP_DEFAULT_AUDIO_LATENCY   = 88200; // 2 seconds @ 44.1kHz
const unsigned int BLT_RAOP_RTP_TIME_ORIGIN         = 88200;
#define BLT_RAOP_RTP_TIME_ORIGIN_STR "88200"

#define BLT_RAOP_OUTPUT_USER_AGENT "BlueTune/1.0"

const unsigned int BLT_RAOP_AUDIO_BUFFER_SIZE_V1 = 4096*4;
const unsigned int BLT_RAOP_AUDIO_BUFFER_SIZE_V2 = 352*4;

const unsigned int BLT_RAOP_RTP_PACKET_FLAG_MARKER_BIT      = 0x80;
const unsigned int BLT_RAOP_RTP_PACKET_TYPE_TIMING_REQUEST  = 0x52;
const unsigned int BLT_RAOP_RTP_PACKET_TYPE_TIMING_RESPONSE = 0x53;
const unsigned int BLT_RAOP_RTP_PACKET_TYPE_SYNC            = 0x54;
const unsigned int BLT_RAOP_RTP_PACKET_TYPE_RESEND          = 0x55;
const unsigned int BLT_RAOP_RTP_PACKET_TYPE_AUDIO           = 0x60;

// internal RTP types 
const unsigned int BLT_RAOP_RTP_PACKET_TYPE_TERMINATE       = 0x7F;

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} RaopOutputModule;

class RaopOutput; // forward reference

class RaopTimingThread : public NPT_Thread {
public:
    RaopTimingThread(RaopOutput& output) : m_Output(output) {}
    ~RaopTimingThread();
    virtual void Run();
    
private:
    RaopOutput& m_Output;
};

class RaopOutput {
public:
    RaopOutput(unsigned int version, const char* hostname, unsigned int port, const char* password);
    ~RaopOutput();
    
    // methods
    NPT_Result SendRequest(const char*        command, 
                           const char*        resource,
                           const char*        extra_headers,
                           const char*        body, 
                           const char*        mime_type,
                           NPT_HttpResponse*& response,
                           bool               recurse = false);
    NPT_String Authorization(const char* method, const char* uri);
    NPT_Result ParseAuthentication(const NPT_String* header);
    NPT_Result Options();
    NPT_Result Announce();
    NPT_Result Setup();
    NPT_Result GetParameter(const char* parameter);
    NPT_Result SetParameter(const char* parameter);
    NPT_Result Record();
    NPT_Result Flush();
    //NPT_Result Pause();
    NPT_Result Teardown();
    
    NPT_Result Connect();
    NPT_Result AddAudio(const void* audio, unsigned int audio_size, const BLT_PcmMediaType* media_type);
    NPT_Result DrainAudio();
    NPT_Result SendAudioBuffer();
    NPT_Result SetVolume(float volume);
    void       Encrypt(const unsigned char* in, unsigned char* out, unsigned int size);
    void       Reset();
    
    // utils
    void DiscardResponseBody(NPT_HttpResponse* response);
    NPT_Result CreateUdpSocket(unsigned int& preferred_port, NPT_UdpSocket*& sock);
    
    // members 
    unsigned int                     m_Version;
    BLT_PcmMediaType                 m_ExpectedMediaType;
    BLT_PcmMediaType                 m_MediaType;
    bool                             m_Connected;
    bool                             m_Setup;
    bool                             m_Paused;
    bool                             m_OptionsReceived;
    float                            m_Volume;
    bool                             m_VolumePending;
    NPT_String                       m_AuthenticationNonce;
    NPT_String                       m_AuthenticationRealm;
    NPT_String                       m_AuthenticationPassword;
    NPT_String                       m_RemoteHostname;
    NPT_IpAddress                    m_RemoteIpAddress;
    NPT_UInt32                       m_RemotePort;
    NPT_BufferedInputStreamReference m_ControlInputStream;
    NPT_OutputStreamReference        m_ControlOutputStream;
    NPT_OutputStreamReference        m_AudioOutputStream;
    NPT_String                       m_InstanceId;
    NPT_IpAddress                    m_LocalIpAddress;
    NPT_String                       m_ClientSessionId;
    NPT_String                       m_ServerSessionId;
    unsigned int                     m_ServerAudioPort;
    unsigned int                     m_ServerControlPort;
    unsigned int                     m_ServerTimingPort;
    unsigned int                     m_AudioLatency;
    NPT_String                       m_RtspRecordUrl;
    unsigned int                     m_ConnectionSequence;
    NPT_BlockCipher*                 m_Cipher;
    NPT_UInt8*                       m_AudioBuffer;
    unsigned int                     m_AudioBufferSize;
    unsigned int                     m_AudioBufferFullness;
    signed short                     m_AudioResampleBuffer[2];
    bool                             m_UseEncryption;
    NPT_UdpSocket*                   m_RtpSocket;
    unsigned int                     m_RtpPort;
    NPT_UInt32                       m_RtpTime;
    NPT_UInt32                       m_RtpSequence;
    bool                             m_RtpMarker;
    NPT_TimeStamp                    m_StartTime;
    NPT_UdpSocket*                   m_ControlSocket;
    unsigned int                     m_ControlPort;
    NPT_UdpSocket*                   m_TimingSocket;
    unsigned int                     m_TimingPort;
    RaopTimingThread*                m_TimingThread;
};

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);
    ATX_IMPLEMENTS(BLT_VolumeControl);
    
    RaopOutput* object;
} _RaopOutput;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
BLT_METHOD RaopOutput_Resume(BLT_MediaNode* self);
BLT_METHOD RaopOutput_Stop(BLT_MediaNode* self);
BLT_METHOD RaopOutput_Drain(BLT_OutputNode* self);

/*----------------------------------------------------------------------
|    RaopBitWriter
+---------------------------------------------------------------------*/
class RaopBitWriter
{
public:
    RaopBitWriter(unsigned int size) : m_DataSize(size), m_BitCount(0) {
        m_Data = new unsigned char[size];
        NPT_SetMemory(m_Data, 0, size);
    }
    ~RaopBitWriter() { delete[] m_Data; }
    
    void Write(unsigned int bits, unsigned int bit_count);
    
    unsigned char* m_Data;
    unsigned int   m_DataSize;
    unsigned int   m_BitCount;
};

/*----------------------------------------------------------------------
|   RaopBitstream::Write
+---------------------------------------------------------------------*/
void 
RaopBitWriter::Write(unsigned int bits, unsigned int bit_count)
{
    unsigned char* data = m_Data;
    if (m_BitCount+bit_count > m_DataSize*8) return;
    data += m_BitCount/8;
    unsigned int space = 8-(m_BitCount%8);
    while (bit_count) {
        unsigned int mask = bit_count==32 ? 0xFFFFFFFF : ((1<<bit_count)-1);
        if (bit_count <= space) {
            *data |= ((bits&mask) << (space-bit_count));
            m_BitCount += bit_count;
            return;
        } else {
            *data |= ((bits&mask) >> (bit_count-space));
            ++data;
            m_BitCount += space;
            bit_count  -= space;
            space       = 8;
        }
    }
}

/*----------------------------------------------------------------------
|    BLT_TimeStampToNtpTime
+---------------------------------------------------------------------*/
static void
BLT_TimeStamp_ToNtpTime(NPT_TimeStamp& ts, unsigned char* ntp)
{
    ATX_UInt64 ticks = (ATX_UInt64)((double)ts*(double)(((ATX_UInt64)1)<<32));
    ATX_BytesFromInt32Be(ntp,   (ATX_UInt32)(ticks>>32));
    ATX_BytesFromInt32Be(ntp+4, (ATX_UInt32)ticks);
}

/*----------------------------------------------------------------------
|    RaopOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_PutPacket(BLT_PacketConsumer* _self,
                     BLT_MediaPacket*    packet)
{
    RaopOutput*             self = ATX_SELF(_RaopOutput, BLT_PacketConsumer)->object;
    const BLT_PcmMediaType* media_type;
    BLT_Result              result;

    /* check parameters */
    if (packet == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* get the media type */
    result = BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (BLT_FAILED(result)) return result;

    /* check the media type */
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_AUDIO_PCM) {
        return BLT_ERROR_INVALID_MEDIA_TYPE;
    }
    if ((media_type->sample_rate != 44100 /*&& 
         media_type->sample_rate != 22050*/) ||      
        media_type->channel_count != 2 ||
        media_type->bits_per_sample != 16 ||
        media_type->sample_format != BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE) {
        return BLT_ERROR_NOT_SUPPORTED;
    }
    const signed short* audio_data      = (const signed short*)BLT_MediaPacket_GetPayloadBuffer(packet);
    unsigned int        audio_data_size = BLT_MediaPacket_GetPayloadSize(packet);
    
#if 0
    // apply sample-rate conversion if necessary
    // NOTE: this is a very crude linear interpolator, which should
    // be replaced by a proper band-limited sample rate converter
    NPT_DataBuffer converted;
    if (media_type->sample_rate == 22050) {
        unsigned int sample_count = audio_data_size/4;
        audio_data_size *= 2;
        converted.SetDataSize(audio_data_size);
        signed short* pcm = (signed short*)converted.UseData();
        for (unsigned int i=0; i<sample_count; i++) {
            int l0, l1, r0, r1;
            if (i==0) {
                l0 = self->m_AudioResampleBuffer[0];
                r0 = self->m_AudioResampleBuffer[1];
            } else {
                l0 = audio_data[2*(i-1)  ];
                r0 = audio_data[2*(i-1)+1];
            }
            l1 = audio_data[2*i  ];
            r1 = audio_data[2*i+1];
            pcm[4*i  ] = (l1+l0)/2;
            pcm[4*i+1] = (r1+r0)/2;
            pcm[4*i+2] = l1;
            pcm[4*i+3] = r1;
        }
        self->m_AudioResampleBuffer[0] = audio_data[-2];
        self->m_AudioResampleBuffer[1] = audio_data[-1];
        audio_data = (const signed short*)pcm;;
    }
#endif

    // send the audio data
    result = self->AddAudio(audio_data, 
                            audio_data_size,
                            media_type);
    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_QueryMediaType(BLT_MediaPort*        _self,
                          BLT_Ordinal           index,
                          const BLT_MediaType** media_type)
{
    RaopOutput* self = ATX_SELF(_RaopOutput, BLT_MediaPort)->object;

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->m_ExpectedMediaType;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    RaopOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
_RaopOutput_Destroy(_RaopOutput* self)
{
    delete self->object;
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|   RaopOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_GetPortByName(BLT_MediaNode*  _self,
                         BLT_CString     name,
                         BLT_MediaPort** port)
{
    _RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|    RaopOutput_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Seek(BLT_MediaNode* _self,
                BLT_SeekMode*  mode,
                BLT_SeekPoint* point)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;

    BLT_COMPILER_UNUSED(mode);
    BLT_COMPILER_UNUSED(point);

    self->Flush();
    if (self->m_Version == 0) {
        self->Teardown();
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_GetStatus(BLT_OutputNode*       _self,
                     BLT_OutputNodeStatus* status)
{
    RaopOutput* self = ATX_SELF(_RaopOutput, BLT_OutputNode)->object;
    BLT_COMPILER_UNUSED(self);
    
    /* default values */
    status->flags = 0;
    status->media_time.seconds = 0;
    status->media_time.nanoseconds = 0;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput_Drain
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Drain(BLT_OutputNode* _self)
{
    RaopOutput* self = ATX_SELF(_RaopOutput, BLT_OutputNode)->object;

    return self->DrainAudio();
}

/*----------------------------------------------------------------------
|    RaopOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Start(BLT_MediaNode* _self)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;

    return self->Connect();
}

/*----------------------------------------------------------------------
|    RaopOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Stop(BLT_MediaNode* _self)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;
    
    self->Flush();
    if (self->m_Version == 0) {
        self->Teardown();
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput_Pause
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Pause(BLT_MediaNode* _self)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;
    
    if (!self->m_Paused) {
        ATX_LOG_FINE("pausing output");
        self->m_Paused = true;
        self->Flush();
        if (self->m_Version == 0) {
            self->Teardown();
        }
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput_Resume
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Resume(BLT_MediaNode* _self)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;

    if (self->m_Paused) {
        ATX_LOG_FINE("resuming output");
        self->m_Paused = false;
        if (self->m_Version == 0) {
            return self->Connect();
        }
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput_SetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_SetVolume(BLT_VolumeControl* _self, float volume)
{
    RaopOutput* self = ATX_SELF(_RaopOutput, BLT_VolumeControl)->object;

    return self->SetVolume(volume);
}

/*----------------------------------------------------------------------
|    RaopOutput_GetVolume
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_GetVolume(BLT_VolumeControl* _self, float* volume)
{
    RaopOutput* self = ATX_SELF(_RaopOutput, BLT_VolumeControl)->object;
    
    *volume = self->m_Volume;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   RaopOutput_Activate
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Activate(BLT_MediaNode* _self, BLT_Stream* stream)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(stream);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       RaopOutput_Deactivate
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutput_Deactivate(BLT_MediaNode* _self)
{
    RaopOutput* self = ATX_SELF_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)->object;
    
    self->Reset();
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(_RaopOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(_RaopOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (_RaopOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT   (_RaopOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT   (_RaopOutput, BLT_PacketConsumer)
    ATX_GET_INTERFACE_ACCEPT   (_RaopOutput, BLT_VolumeControl)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(RaopOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(_RaopOutput, BLT_MediaPort)
    RaopOutput_GetName,
    RaopOutput_GetProtocol,
    RaopOutput_GetDirection,
    RaopOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(_RaopOutput, BLT_PacketConsumer)
    RaopOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(_RaopOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    RaopOutput_GetPortByName,
    RaopOutput_Activate,
    RaopOutput_Deactivate,
    RaopOutput_Start,
    RaopOutput_Stop,
    RaopOutput_Pause,
    RaopOutput_Resume,
    RaopOutput_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(_RaopOutput, BLT_OutputNode)
    RaopOutput_GetStatus,
    RaopOutput_Drain
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_VolumeControl interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(_RaopOutput, BLT_VolumeControl)
    RaopOutput_GetVolume,
    RaopOutput_SetVolume
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(_RaopOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|    RaopOutput::RaopOutput
+---------------------------------------------------------------------*/
RaopOutput::RaopOutput(unsigned int version, const char* hostname, unsigned int port, const char* password) :
    m_Version(version),
    m_Connected(false),
    m_Setup(false),
    m_Paused(false),
    m_OptionsReceived(false),
    m_Volume(1.0f),
    m_VolumePending(false),
    m_AuthenticationPassword(password),
    m_RemoteHostname(hostname),
    m_RemotePort(port),
    m_ServerAudioPort(0),
    m_ServerControlPort(0),
    m_ServerTimingPort(0),
    m_AudioLatency(0),
    m_ConnectionSequence(1),
    m_Cipher(NULL),
    m_AudioBuffer(NULL),
    m_AudioBufferSize(0),
    m_AudioBufferFullness(0),
    m_UseEncryption(false),
    m_RtpSocket(NULL),
    m_RtpPort(0),
    m_RtpTime(BLT_RAOP_RTP_TIME_ORIGIN),
    m_RtpSequence(0),
    m_RtpMarker(false),
    m_ControlSocket(NULL),
    m_ControlPort(0),
    m_TimingSocket(NULL),
    m_TimingPort(0),
    m_TimingThread(NULL)
{
    m_MediaType.sample_rate     = 0;
    m_MediaType.channel_count   = 0;
    m_MediaType.bits_per_sample = 0;

    /* setup the expected media type */
    BLT_PcmMediaType_Init(&m_ExpectedMediaType);
    m_ExpectedMediaType.sample_format = BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE;
    
    // setup the cipher
    const NPT_UInt8 key[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    NPT_BlockCipher::Create(NPT_BlockCipher::AES_128, 
                            NPT_BlockCipher::ENCRYPT, 
                            key, 16, 
                            m_Cipher);     
                            
    m_AudioResampleBuffer[0] = 0;
    m_AudioResampleBuffer[1] = 0;
}

/*----------------------------------------------------------------------
|    RaopOutput::~RaopOutput
+---------------------------------------------------------------------*/
RaopOutput::~RaopOutput()
{
    delete m_TimingThread;
    delete[] m_AudioBuffer;
    delete m_RtpSocket;
    delete m_ControlSocket;
    delete m_TimingSocket;
}

/*----------------------------------------------------------------------
|    RaopOutput::ParseAuthentication
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::ParseAuthentication(const NPT_String* header)
{
    m_AuthenticationNonce = "";
    if (!header) return BLT_ERROR_PROTOCOL_FAILURE;
    if (!header->StartsWith("Digest")) return BLT_ERROR_PROTOCOL_FAILURE;

    // find the realm
    int pos = header->Find("realm=\"");
    if (pos < 0) return BLT_ERROR_PROTOCOL_FAILURE;
    unsigned int start = pos+7;
    pos = header->Find("\"", start);
    if (pos < 0) return BLT_ERROR_PROTOCOL_FAILURE;
    m_AuthenticationRealm = header->SubString(start, pos-start);

    // find the nonce
    pos = header->Find("nonce=\"");
    if (pos < 0) return BLT_ERROR_PROTOCOL_FAILURE;
    start = pos+7;
    pos = header->Find("\"", start);
    if (pos < 0) return BLT_ERROR_PROTOCOL_FAILURE;
    m_AuthenticationNonce = header->SubString(start, pos-start);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::Authorization
+---------------------------------------------------------------------*/
NPT_String
RaopOutput::Authorization(const char* method, const char* uri)
{
    NPT_Digest* md5 = NULL;
    
    NPT_DataBuffer ha1;
    NPT_String a1 = "iTunes:";
    a1 += m_AuthenticationRealm;
    a1 += ":";
    a1 += m_AuthenticationPassword;
    NPT_Digest::Create(NPT_Digest::ALGORITHM_MD5, md5);
    md5->Update((const NPT_UInt8*)a1.GetChars(), a1.GetLength());
    md5->GetDigest(ha1);
    delete md5;

    NPT_DataBuffer ha2;
    NPT_String a2 = method;
    a2 += ":";
    a2 += uri;
    NPT_Digest::Create(NPT_Digest::ALGORITHM_MD5, md5);
    md5->Update((const NPT_UInt8*)a2.GetChars(), a2.GetLength());
    md5->GetDigest(ha2);
    delete md5;
    
    NPT_DataBuffer hresponse;
    NPT_String hex_ha1 = NPT_HexString(ha1.GetData(), ha1.GetDataSize(), NULL, false);
    NPT_String hex_ha2 = NPT_HexString(ha2.GetData(), ha2.GetDataSize(), NULL, false);
    NPT_String response = hex_ha1 + ":" + m_AuthenticationNonce + ":" + hex_ha2;
    NPT_Digest::Create(NPT_Digest::ALGORITHM_MD5, md5);
    md5->Update((const NPT_UInt8*)response.GetChars(), response.GetLength());
    md5->GetDigest(hresponse);
    delete md5;
    
    NPT_String authorization = "Digest username=\"iTunes\", realm=\"";
    authorization += m_AuthenticationRealm;
    authorization += "\", nonce=\"";
    authorization += m_AuthenticationNonce;
    authorization += "\", uri=\"";
    authorization += uri;
    authorization += "\", response=\"";
    authorization += NPT_HexString(hresponse.GetData(), hresponse.GetDataSize(), NULL, false);
    authorization += "\"";
    
    return authorization;
}

/*----------------------------------------------------------------------
|    RaopOutput::SendRequest
+---------------------------------------------------------------------*/
NPT_Result RaopOutput::SendRequest(const char*        command, 
                                   const char*        resource,
                                   const char*        exta_headers,
                                   const char*        body, 
                                   const char*        mime_type,
                                   NPT_HttpResponse*& response,
                                   bool               recurse)
{
    NPT_String request = command;
    request += " ";
    request += resource;
    request += " ";
    request += "RTSP/1.0\r\n";
    if (exta_headers) {
        request += exta_headers;
    }
    request += NPT_String::Format("CSeq: %u\r\n", m_ConnectionSequence++);
    if (!m_ServerSessionId.IsEmpty()) {
        request += "Session: ";
        request += m_ServerSessionId;
        request += "\r\n";
    }
    request += "User-Agent: " BLT_RAOP_OUTPUT_USER_AGENT "\r\n";
    request += "Client-Instance: ";
    request += m_InstanceId;
    request += "\r\n";
    request += "DACP-ID: ";
    request += m_InstanceId;
    request += "\r\n";
    if (m_AuthenticationNonce.GetLength()) {
        request += "Authorization: ";
        request += Authorization(command, resource);
        request += "\r\n";
    }
    if (body) {
        if (mime_type) {
            request += "Content-Type: ";
            request += mime_type;
            request += "\r\n";
        }
        request += NPT_String::Format("Content-Length: %u\r\n\r\n", NPT_StringLength(body));
        request += body;
    } else {
        //request += "Content-Length: 0\r\n\r\n";
        request += "\r\n";
    }
    ATX_LOG_FINE_1("%s", request.GetChars());
    NPT_Result result = m_ControlOutputStream->WriteFully(request.GetChars(), request.GetLength());
    if (NPT_FAILED(result)) return result;
    
    // parse the response
    result = NPT_HttpResponse::Parse(*m_ControlInputStream, response);
    if (NPT_FAILED(result)) return result;
    
#if ATX_CONFIG_ENABLE_LOGGING
    ATX_LOG_FINE_3("RESPONSE: protocol=%s, code=%d, reason=%s",
                 response->GetProtocol().GetChars(),
                 response->GetStatusCode(),
                 response->GetReasonPhrase().GetChars());
    NPT_HttpHeaders& headers = response->GetHeaders();
    NPT_List<NPT_HttpHeader*>::Iterator header = headers.GetHeaders().GetFirstItem();
    while (header) {
        ATX_LOG_FINE_2("%s: %s", 
                       (const char*)(*header)->GetName(),
                       (const char*)(*header)->GetValue());
        ++header;
    }
#endif

    // check the status 
    if (response->GetStatusCode() != 200) {
        ATX_LOG_WARNING_2("response code is not 200 (%d:%s)", 
                          response->GetStatusCode(), 
                          response->GetReasonPhrase().GetChars());
        switch (response->GetStatusCode()) {
            case 401:
                if (recurse) {
                    // we're already authenticating, something's wrong
                    return BLT_ERROR_ACCESS_DENIED;
                }
                result = ParseAuthentication(response->GetHeaders().GetHeaderValue("WWW-Authenticate"));
                if (NPT_FAILED(result)) {
                    return BLT_ERROR_ACCESS_DENIED;
                }
                delete response; 
                response = NULL;
                return SendRequest(command, resource, exta_headers, body, mime_type, response, true);
                
            case 453:
                return BLT_ERROR_DEVICE_BUSY;
            
            default:
                return BLT_ERROR_PROTOCOL_FAILURE;
        }
    }
    
    NPT_HttpEntity* response_entity = new NPT_HttpEntity(response->GetHeaders());
    response->SetEntity(response_entity);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::DiscardResponseBody
+---------------------------------------------------------------------*/
void
RaopOutput::DiscardResponseBody(NPT_HttpResponse* response) 
{
    NPT_HttpEntity* entity = response->GetEntity();
    if (entity && entity->GetContentLength()) {
        NPT_DataBuffer body;
        entity->Load(body);
    }
}

/*----------------------------------------------------------------------
|    RaopOutput::CreateUdpSocket
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::CreateUdpSocket(unsigned int& port, NPT_UdpSocket*& socket)
{
    socket = new NPT_UdpSocket();
    
    // try to bind until we get a free port
    unsigned int max_port = port+4096; // don't try more than 4096 ports!
    for (; port < max_port; port++) {
        NPT_SocketAddress socket_address;
        socket_address.SetPort(port);
        NPT_Result result = socket->Bind(socket_address, false);
        if (NPT_SUCCEEDED(result)) return NPT_SUCCESS;
    }
    ATX_LOG_WARNING_1("unable to find a free, up to %d", port);
    delete socket;
    socket = NULL;
    return NPT_FAILURE;
}

/*----------------------------------------------------------------------
|    RaopOutput::Connect
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Connect()
{
    if (!m_Connected) {
        // resolve the hostname
        NPT_Result result = m_RemoteIpAddress.ResolveName(m_RemoteHostname);
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("failed to resolve remote hostname (%d)", result);
            return BLT_ERROR_NO_SUCH_DEVICE;
        }
        NPT_SocketAddress remote_address(m_RemoteIpAddress, m_RemotePort);
        
        // clear any existing connections
        m_ControlInputStream  = NULL;
        m_ControlOutputStream = NULL;
        m_AudioOutputStream   = NULL;
        
        // connect to the remote
        NPT_TcpClientSocket socket;
        result = socket.Connect(remote_address, BLT_RAOP_DEFAULT_CONNECT_TIMEOUT);
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("failed to connect to remote (%d)", result);
            return result;
        }
        socket.SetReadTimeout(BLT_RAOP_DEFAULT_IO_TIMEOUT);
        socket.SetWriteTimeout(BLT_RAOP_DEFAULT_IO_TIMEOUT);
        NPT_InputStreamReference input_stream;
        socket.GetInputStream(input_stream);
        m_ControlInputStream = new NPT_BufferedInputStream(input_stream);
        socket.GetOutputStream(m_ControlOutputStream);
        
        // get the socket info
        NPT_SocketInfo socket_info;
        socket.GetInfo(socket_info);
            
        // compute the session ID and client ID
        NPT_TimeStamp now;
        NPT_System::GetCurrentTimeStamp(now);
        NPT_Digest* digest = NULL;
        NPT_Digest::Create(NPT_Digest::ALGORITHM_SHA1, digest);
        digest->Update((const NPT_UInt8*)&now, sizeof(now));
        digest->Update((const NPT_UInt8*)&socket_info, sizeof(socket_info));
        NPT_DataBuffer random_data;
        digest->GetDigest(random_data);
        if (m_InstanceId.IsEmpty()) {
            m_InstanceId = NPT_HexString(random_data.GetData(), 8, NULL, true);
        }
        m_ClientSessionId = NPT_String::FromIntegerU((NPT_UInt32)now.ToMillis());
        m_LocalIpAddress  = socket_info.local_address.GetIpAddress();
        m_RemoteIpAddress = socket_info.remote_address.GetIpAddress();
        m_RtspRecordUrl   = "rtsp://";
        m_RtspRecordUrl  += m_LocalIpAddress.ToString();
        m_RtspRecordUrl  += "/";
        m_RtspRecordUrl  += m_ClientSessionId;
        m_ConnectionSequence = 1;
        
        m_Connected = true;
    }
    
    if (!m_Setup) {
        // go through the post-connect protocol sequence
        NPT_Result result;
        if (!m_OptionsReceived) {
            result = Options();
            if (NPT_FAILED(result)) {
                ATX_LOG_WARNING_1("OPTIONS failed (%d)", result);
            }
        }
        result = Announce();
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("ANNOUNCE failed (%d)", result);
            return result;
        }
        result = Setup();
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("SETUP failed (%d)", result);
            return result;
        }
        result = Record();
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("RECORD failed (%d)", result);
            return result;
        }
        if (m_VolumePending) {
            SetVolume(m_Volume);
        }

        m_Setup = true;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::Options
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Options()
{
    if (!m_Connected) return BLT_ERROR_INVALID_STATE;

    NPT_String extra_headers = "Apple-Challenge: AAAAAAAAAAAAAAAAAAAAAA\r\n";

    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("OPTIONS", 
                                    "*", 
                                    extra_headers,
                                    NULL,
                                    NULL, 
                                    response);
    if (NPT_FAILED(result)) return result;
    
    // check the response
    NPT_HttpHeaders& rtsp_headers = response->GetHeaders();
    if (rtsp_headers.GetHeaderValue("Apple-Response")) {
        m_UseEncryption = true;
    }

    DiscardResponseBody(response);
    delete response;

    m_OptionsReceived = true;
    
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::Announce
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Announce()
{
    if (!m_Connected) return BLT_ERROR_INVALID_STATE;

    // reset the server session ID
    m_ServerSessionId = "";
    
    // construct the SDP
    NPT_String sdp = NPT_String::Format(
        "v=0\r\n"
        "o=iTunes %s 0 IN IP4 %s\r\n"
        "s=iTunes\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio 0 RTP/AVP 96\r\n"
        "a=rtpmap:96 AppleLossless\r\n"
        "a=fmtp:96 %d 0 16 40 10 14 2 255 0 0 44100\r\n"
        "a=rsaaeskey:"
        "ruhL6ogbzAZwHFR/53gmXCPGslyJxhUOaJCUeISYt93/h7CsNLP0jeMt"
        "pMH8P6xktJXvlh8uws8GqjnBo2uskF01okfgUsfTXuqhTmeLH+E8spox"
        "euon80TWZoUviHRkdYjLBH30s9G8ZcbmblvPQBRB5gU1Qs6Wc3rXCKns"
        "Bu5KN9bIWE4hFEmgGSAIpjlMQOwfVP1TmJhcAFw8kkrZDpDa6iOOWsaI"
        "iEJpuD75I/lw+hyIGlZwKfgztkck+YKzdE+tANWCvwG/XNOALDwsVD0z"
        "TnAcx1qAicFC5eQAzkBC8M8TdAFshdHdwkMk7pYnWX4eown4YEg1sZFq"
        "tTp4Kg\r\n"
        "a=aesiv:AAAAAAAAAAAAAAAAAAAAAA\r\n",
        m_ClientSessionId.GetChars(), 
        m_LocalIpAddress.ToString().GetChars(), 
        m_RemoteIpAddress.ToString().GetChars(),
        (m_Version==0)?4096:352);

    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("ANNOUNCE", 
                                    m_RtspRecordUrl, 
                                    NULL,
                                    sdp,
                                    "application/sdp", 
                                    response);
    if (NPT_FAILED(result)) return result;
    DiscardResponseBody(response);
    delete response;

    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::Setup
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Setup()
{
    if (!m_Connected) return BLT_ERROR_INVALID_STATE;

    NPT_Result result;
    
    NPT_String extra_headers;
    delete m_AudioBuffer;
    if (m_Version == 0) {
        m_AudioBuffer         = new unsigned char[BLT_RAOP_AUDIO_BUFFER_SIZE_V1];
        m_AudioBufferSize     = BLT_RAOP_AUDIO_BUFFER_SIZE_V1;
        m_AudioBufferFullness = 0;
        extra_headers = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1;mode=record\r\n";
    } else if (m_Version == 1) {
        m_AudioBuffer         = new unsigned char[BLT_RAOP_AUDIO_BUFFER_SIZE_V2];
        m_AudioBufferSize     = BLT_RAOP_AUDIO_BUFFER_SIZE_V2;
        m_AudioBufferFullness = 0;
        
        // create UDP sockets unless we already have them
        if (m_RtpSocket == NULL) {
            m_RtpPort = 6000;
            result = CreateUdpSocket(m_RtpPort, m_RtpSocket);
            if (NPT_FAILED(result)) return result;
        } 
        
        if (m_ControlSocket == NULL) {
            m_ControlPort = m_RtpPort+1;
            result = CreateUdpSocket(m_ControlPort, m_ControlSocket);
            if (NPT_FAILED(result)) return result;
        }

        if (m_TimingSocket == NULL) {
            m_TimingPort = m_ControlPort+1;
            result = CreateUdpSocket(m_TimingPort, m_TimingSocket);
            if (NPT_FAILED(result)) return result;
        }
        
        extra_headers = NPT_String::Format("Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=%d;timing_port=%d\r\n", 
                                           m_ControlPort,
                                           m_TimingPort);
    } else {
        return BLT_ERROR_INTERNAL;
    }
    
    NPT_HttpResponse* response = NULL;
    result = SendRequest("SETUP", 
                         m_RtspRecordUrl, 
                         extra_headers,
                         NULL,
                         NULL, 
                         response);
    if (NPT_FAILED(result)) return result;
    
    // get the server session id
    NPT_HttpHeaders& rtsp_headers = response->GetHeaders();
    const NPT_String* session = rtsp_headers.GetHeaderValue("Session");
    if (session == NULL) {
        ATX_LOG_WARNING("no Session header found in response");
        delete response;
        return BLT_ERROR_PROTOCOL_FAILURE;
    }
    m_ServerSessionId = *session;
    ATX_LOG_FINE_1("session=%s", m_ServerSessionId.GetChars());
    
    // get the server port
    const NPT_String* transport = rtsp_headers.GetHeaderValue("Transport");
    if (transport == NULL) {
        ATX_LOG_WARNING("no Session header found in response");
        delete response;
        return BLT_ERROR_PROTOCOL_FAILURE;
    }
    int server_port_position = transport->Find(";server_port=");
    if (server_port_position < 0) {
        ATX_LOG_WARNING("server_port not found in response");
        delete response;
        return BLT_ERROR_PROTOCOL_FAILURE;
    }
    NPT_ParseInteger(transport->GetChars()+server_port_position+13, m_ServerAudioPort, true);
    ATX_LOG_FINE_1("server_port=%d", m_ServerAudioPort);

    if (m_Version == 1) {
        int control_port_position = transport->Find(";control_port=");
        if (control_port_position > 0) {
            NPT_ParseInteger(transport->GetChars()+control_port_position+14, m_ServerControlPort, true);
            ATX_LOG_FINE_1("control_port=%d", m_ServerControlPort);
            NPT_SocketAddress control_address(m_RemoteIpAddress, m_ServerControlPort);
            m_ControlSocket->Connect(control_address);
        }
        int timing_port_position = transport->Find(";timing_port=");
        if (timing_port_position > 0) {
            NPT_ParseInteger(transport->GetChars()+timing_port_position+13, m_ServerTimingPort, true);
            ATX_LOG_FINE_1("timing_port=%d", m_ServerTimingPort);
            //NPT_SocketAddress timing_address(m_RemoteIpAddress, m_ServerTimingPort);
            //m_TimingSocket->Connect(timing_address);
        }
    }
    
    // setup the timing response thread
    if (m_Version == 1 && m_TimingThread == NULL) {
        // create a thread to response to timing requests
        m_TimingThread = new RaopTimingThread(*this);
        m_TimingThread->Start();
    }
        
    DiscardResponseBody(response);
    delete response;    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::GetParameter
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::GetParameter(const char* parameter)
{
    if (!m_Connected) return BLT_ERROR_INVALID_STATE;

    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("GET_PARAMETER", 
                                    m_RtspRecordUrl, 
                                    NULL,
                                    parameter,
                                    "text/parameters", 
                                    response);
    if (NPT_FAILED(result)) return result;
            
    if (response->GetStatusCode() == 200 && response->GetEntity()) {
        NPT_DataBuffer response_body;
        response->GetEntity()->Load(response_body);

        NPT_String parameters((const char*)response_body.GetData(), response_body.GetDataSize());
        ATX_LOG_FINE_1("parameters: %s", parameters.GetChars());
    } else {
        ATX_LOG_WARNING_1("GET_PARAMETER response is not 200 (%d)", response->GetStatusCode());
    }
    delete response;    
    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::SetParameter
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::SetParameter(const char* parameter)
{
    if (!m_Connected) return BLT_ERROR_INVALID_STATE;

    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("SET_PARAMETER", 
                                    m_RtspRecordUrl, 
                                    NULL,
                                    parameter,
                                    "text/parameters", 
                                    response);
    if (NPT_FAILED(result)) return result;
            
    DiscardResponseBody(response);
    delete response;    
    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::Record
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Record()
{
    if (!m_Connected) return BLT_ERROR_INVALID_STATE;

    // reset RTP info
    m_RtpSequence = 0;
    m_RtpTime= BLT_RAOP_RTP_TIME_ORIGIN;
    m_RtpMarker = true;
    
    // send request
    NPT_String extra_headers = "Range: ntp=0-\r\n"
                               "RTP-Info: seq=0;rtptime=" BLT_RAOP_RTP_TIME_ORIGIN_STR "\r\n";
    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("RECORD", 
                                    m_RtspRecordUrl, 
                                    extra_headers,
                                    NULL,
                                    NULL, 
                                    response);
    if (NPT_FAILED(result)) return result;
        
    // get the audio latency
    const NPT_String* latency = response->GetHeaders().GetHeaderValue("Audio-Latency");
    if (latency) {
        latency->ToInteger(m_AudioLatency, true);
    }
    ATX_LOG_FINE_1("audio latency = %d", m_AudioLatency);
    
    // cleanup any previous audio socket
    m_AudioOutputStream = NULL;
    
    // connect to the audio port
    NPT_SocketAddress server_addr(m_RemoteIpAddress, m_ServerAudioPort);
    if (m_Version == 0) {
        NPT_TcpClientSocket audio_socket;
        result = audio_socket.Connect(server_addr, BLT_RAOP_DEFAULT_CONNECT_TIMEOUT);
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("failed to connect to audio port (%d)", result);
            return result;
        }
        audio_socket.SetReadTimeout(BLT_RAOP_DEFAULT_IO_TIMEOUT);
        audio_socket.SetWriteTimeout(BLT_RAOP_DEFAULT_IO_TIMEOUT);
        audio_socket.GetOutputStream(m_AudioOutputStream);
    } else if (m_Version == 1) {
        result = m_RtpSocket->Connect(server_addr);
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("failed to connect to audio port (%d)", result);
            return result;
        }

        // connect to the control socket
        server_addr.SetPort(m_ControlPort);
        result = m_ControlSocket->Connect(server_addr);
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("failed to connect to control port (%d)", result);
            return result;
        }
    } else {
        return BLT_ERROR_INTERNAL;
    }
        
    DiscardResponseBody(response);
    delete response;    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::Flush
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Flush()
{
    if (!m_Connected) return BLT_SUCCESS;
    
    NPT_String extra_headers = NPT_String::Format("RTP-Info: seq=%d;rtptime=%d\r\n",
                                                  m_RtpSequence,
                                                  BLT_RAOP_RTP_TIME_ORIGIN/*m_RtpTime*/);
    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("FLUSH", 
                                    m_RtspRecordUrl, 
                                    extra_headers,
                                    NULL,
                                    NULL, 
                                    response);
    if (NPT_FAILED(result)) return result;
            
    DiscardResponseBody(response);
    delete response;    
    
    // flush internal buffers
    m_AudioBufferFullness = 0;
    m_RtpTime = BLT_RAOP_RTP_TIME_ORIGIN;
    m_RtpSequence = 0;
    m_AudioResampleBuffer[0] = 0;
    m_AudioResampleBuffer[1] = 0;
    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::Teardown
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::Teardown()
{
    if (!m_Connected) return BLT_SUCCESS;

    NPT_HttpResponse* response = NULL;
    NPT_Result result = SendRequest("TEARDOWN", 
                                    m_RtspRecordUrl, 
                                    NULL,
                                    NULL,
                                    NULL, 
                                    response);
    if (NPT_FAILED(result)) return result;
            
    DiscardResponseBody(response);
    delete response;    
    
    // partial reset of the state
    m_Setup = false;
    m_AudioBufferFullness = 0;
    m_RtpTime = BLT_RAOP_RTP_TIME_ORIGIN;
    m_RtpSequence = 0;
    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::Reset
+---------------------------------------------------------------------*/
void
RaopOutput::Reset()
{
    m_Connected           = false;
    m_Setup               = false;
    m_ControlInputStream  = NULL;
    m_ControlOutputStream = NULL;
    m_AudioOutputStream   = NULL;
    m_AudioBufferFullness = 0;
    m_ConnectionSequence  = 1;
    m_RtpSequence         = 0;
    m_RtpTime             = BLT_RAOP_RTP_TIME_ORIGIN;
}

/*----------------------------------------------------------------------
|    RaopOutput::Encrypt
+---------------------------------------------------------------------*/
void
RaopOutput::Encrypt(const unsigned char* in, unsigned char* out, unsigned int size)
{
    NPT_UInt8 chain[16];
    NPT_SetMemory(chain, 0, 16);

    // process all blocks
    unsigned int block_count = size/16;
    for (unsigned int x=0; x<block_count; x++) {
        // xor with the chaining block
        for (unsigned int y=0; y<16; y++) {
            chain[y] ^= in[y];
        }
        
        // encrypt the block
        m_Cipher->ProcessBlock(chain, out);
        
        // chain and move forward to the next block
        NPT_CopyMemory(chain, out, 16);
        in  += 16;
        out += 16;
    }
    
    // copy any remaining partial block data unencrypted
    if (size%16) {
        NPT_CopyMemory(out, in, size%16);
    }
}
            
/*----------------------------------------------------------------------
|    RaopOutput::SendAudioBuffer
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::SendAudioBuffer()
{
    unsigned int sample_count = m_AudioBufferFullness/4;
    
    // compute the ALAC bitstream
    unsigned int alac_size = sample_count*4+3+4;
    RaopBitWriter alac(alac_size);
    alac.Write(1,  3); // channel count (1=stereo)
    alac.Write(0, 16); // unknown
    alac.Write(1,  1); // has_size
    alac.Write(0,  2); // unknown
    alac.Write(1,  1); // uncompressed
    alac.Write(sample_count, 32); // sample count
    unsigned char* samples = m_AudioBuffer;
    for (unsigned int i=0; i<m_AudioBufferFullness; i += 2, samples += 2) {
        unsigned int sample = samples[0]<<8 | samples[1];
        alac.Write(sample, 16);
    }
    
    // auto-reconnect if needed
    NPT_Result result;
    if (!m_Connected || !m_Setup) {
        result = Connect();
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING("failed to auto-reconnect");
            return result;
        }
    }

    if (m_Version == 0) {
        // compute the header
        NPT_UInt8 header[16] = {
            0x24, 0x00, 0x00, 0x00,
            0xf0, 0xff, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        };
        NPT_BytesFromInt16Be(&header[2], 12+alac_size);

        // create the output audio payload
        NPT_DataBuffer payload;
        payload.SetDataSize(16+alac_size);
        NPT_CopyMemory(payload.UseData(), header, 16);
        if (m_UseEncryption) {
            Encrypt(alac.m_Data, payload.UseData()+16, alac_size);
        }
    
        ATX_LOG_FINER("sending audio buffer over TCP");
        result = m_AudioOutputStream->WriteFully(payload.GetData(), payload.GetDataSize());
        if (NPT_FAILED(result)) {
            ATX_LOG_FINER_1("WriteFully failed (%d)", result);
            if (result == NPT_ERROR_CONNECTION_RESET || result == NPT_ERROR_CONNECTION_ABORTED) {
                ATX_LOG_WARNING("audio connection reset, reconnecting");
                Reset();
            }
        }
    } else {
        // compute the header
        NPT_UInt8 rtp_header[12];
        rtp_header[0] = 0x80;
        rtp_header[1] = BLT_RAOP_RTP_PACKET_TYPE_AUDIO | (m_RtpMarker?BLT_RAOP_RTP_PACKET_FLAG_MARKER_BIT:0);
        NPT_BytesFromInt16Be(&rtp_header[2], m_RtpSequence);
        NPT_BytesFromInt32Be(&rtp_header[4], m_RtpTime);
        NPT_SetMemory(&rtp_header[8], 0, 4);
                
        // send SYNC packets at regular intervals
        if (((m_RtpSequence)%BLT_RAOP_SYNC_PACKET_INTERVAL) == 0) {
            bool first_sync = m_RtpTime == BLT_RAOP_RTP_TIME_ORIGIN;
            NPT_DataBuffer sync_packet;
            sync_packet.SetDataSize(20);
            unsigned char* payload = sync_packet.UseData();
            payload[0] = first_sync?0x90:0x80;
            payload[1] = BLT_RAOP_RTP_PACKET_TYPE_SYNC | BLT_RAOP_RTP_PACKET_FLAG_MARKER_BIT;
            payload[2] = 0;
            payload[3] = 7;
            NPT_BytesFromInt32Be(&payload[4], m_RtpTime-BLT_RAOP_DEFAULT_AUDIO_LATENCY);
            NPT_BytesFromInt32Be(&payload[16], m_RtpTime);

            NPT_TimeStamp now;
            NPT_System::GetCurrentTimeStamp(now);
            BLT_TimeStamp_ToNtpTime(now, &payload[8]);
            
            ATX_LOG_FINE("sending sync packet");
            m_ControlSocket->Send(sync_packet);
        }

        // wait until it is time to send the buffer
        NPT_TimeStamp now;
        NPT_System::GetCurrentTimeStamp(now);
        if (m_RtpTime == BLT_RAOP_RTP_TIME_ORIGIN) {
            m_StartTime = now;
        } else {
            double elapsed = (double)(now.ToNanos()-m_StartTime.ToNanos())/1000000000.0;
            double target = (double)(m_RtpTime-BLT_RAOP_RTP_TIME_ORIGIN)/44100.0;
            double delta = target-elapsed;
            if (delta > 0.001) {
                if (delta > BLT_RAOP_MAX_DELAY) {
                    ATX_LOG_WARNING("unexpected large delay, recalibrating");
                    m_StartTime = now;
                }
                NPT_System::Sleep(delta);
            }
        }

        // update RTP state
        if (m_RtpMarker) m_RtpMarker = false;
                
        // create the output audio payload
        NPT_DataBuffer payload;
        payload.SetDataSize(12+alac_size);
        NPT_CopyMemory(payload.UseData(), rtp_header, 12);
        if (m_UseEncryption) {
            Encrypt(alac.m_Data, payload.UseData()+12, alac_size);
        }
    
        ATX_LOG_FINER("sending audio buffer over UDP");
        result = m_RtpSocket->Send(payload);
        if (NPT_FAILED(result)) {
            ATX_LOG_FINER_1("Send failed (%d)", result);
        }        
    }
    
    // update RTP counters
    ++m_RtpSequence;
    m_RtpTime += sample_count;
    
    return result;
}

/*----------------------------------------------------------------------
|    RaopOutput::AddAudio
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::AddAudio(const void*             audio, 
                     unsigned int            audio_size, 
                     const BLT_PcmMediaType* media_type)
{
    while (audio_size >= 2) {
        unsigned int chunk = m_AudioBufferSize-m_AudioBufferFullness;
        if (chunk > audio_size) {
            chunk = audio_size;
        }
        if (media_type->sample_format == BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE) {
            NPT_CopyMemory(&m_AudioBuffer[m_AudioBufferFullness], audio, chunk);
        } else {
            // swap bytes
            const unsigned char* in  = (const unsigned char*)audio;
            unsigned char*       out = (unsigned char*)&m_AudioBuffer[m_AudioBufferFullness];
            for (unsigned int i=0; i<chunk/2; i++, in += 2, out += 2) {
                out[1] = in[0];
                out[0] = in[1];
            }
        }
        m_AudioBufferFullness += chunk;
        audio_size            -= chunk;
        audio                  = (const void*)((const char*)audio+chunk);
        if (m_AudioBufferFullness == m_AudioBufferSize) {
            NPT_Result result = SendAudioBuffer();
            m_AudioBufferFullness = 0;
            if (NPT_FAILED(result)) return result;
        }
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::DrainAudio
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::DrainAudio()
{
    if (m_AudioBufferFullness) {
        NPT_SetMemory(&m_AudioBuffer[m_AudioBufferFullness], 0, 
                      m_AudioBufferSize-m_AudioBufferFullness);
        m_AudioBufferFullness = m_AudioBufferSize;
        NPT_Result result = SendAudioBuffer();
        m_AudioBufferFullness = 0;
        if (NPT_FAILED(result)) return result;
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopOutput::SetVolume
+---------------------------------------------------------------------*/
NPT_Result
RaopOutput::SetVolume(float volume)
{   
    m_Volume = volume;
    if (m_ControlOutputStream.IsNull()) {
        m_VolumePending = true;
        return BLT_SUCCESS;
    }
    m_VolumePending = false;
    
    // convert the scale
    if (volume == 0.0f) {
        volume = -144.0f;
    } else if (volume >= 1.0f) {
        volume = 0.0f;
    } else {
        volume = (30.0f*volume)-30.0f;
    }
    char param[256];
    NPT_FormatString(param, 256, "volume: %f\r\n", volume);
    return SetParameter(param);
}

/*----------------------------------------------------------------------
|    RaopParseRtpHeader
+---------------------------------------------------------------------*/
static NPT_Result
RaopParseRtpHeader(const unsigned char* header, 
                   unsigned int&        packet_type,
                   unsigned int&        packet_flags)
{
/*
   The RTP header has the following format:

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           timestamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           synchronization source (SSRC) identifier            |
   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
   |            contributing source (CSRC) identifiers             |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
    packet_flags = header[1]>>7;
    packet_type  = header[1]&0x7F;
    
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|    RaopTimingThread::Run
+---------------------------------------------------------------------*/
void
RaopTimingThread::Run()
{
    NPT_DataBuffer request_buffer(32);
    NPT_DataBuffer response_buffer;
    response_buffer.SetDataSize(32);
    
    for (;;) {
        ATX_LOG_FINE("waiting for request on timing port...");
        NPT_SocketAddress remote_address;
        NPT_Result result = m_Output.m_TimingSocket->Receive(request_buffer, &remote_address);
        if (NPT_FAILED(result)) {
            ATX_LOG_WARNING_1("failed to read datagram (%d)", result);
            return;
        }
        if (request_buffer.GetDataSize() < 8) {
            ATX_LOG_WARNING_1("packet too small (%d)", request_buffer.GetDataSize());
            continue;
        }
        const unsigned char* request = request_buffer.GetData();
        unsigned int packet_type  = 0;
        unsigned int packet_flags = 0;
        result = RaopParseRtpHeader(request, packet_type, packet_flags);
        if (NPT_FAILED(result)) continue;
        
        switch (packet_type) {
            case BLT_RAOP_RTP_PACKET_TYPE_TIMING_REQUEST: {
                ATX_LOG_FINE("timing request");
                unsigned char* response = response_buffer.UseData();
                response[0] = 0x80;
                response[1] = BLT_RAOP_RTP_PACKET_TYPE_TIMING_RESPONSE | 
                              BLT_RAOP_RTP_PACKET_FLAG_MARKER_BIT;
                response[2] = 0x00;
                response[3] = 0x07; // sequence number (fixed)
                response[4] = 0x00;
                response[5] = 0x00;
                response[6] = 0x00;
                response[7] = 0x00;
                NPT_CopyMemory(&response[8], &request[24], 8);
                NPT_TimeStamp now;
                NPT_System::GetCurrentTimeStamp(now);
                BLT_TimeStamp_ToNtpTime(now, &response[16]);
                NPT_CopyMemory(&response[24], &response[16], 8);
                m_Output.m_TimingSocket->Send(response_buffer, &remote_address);
                break;
            }
                
            case BLT_RAOP_RTP_PACKET_TYPE_TERMINATE:
                ATX_LOG_FINE("terminating timing thread");
                return;
                
            default:
                ATX_LOG_FINE("unknown request");
                break;
        }
    }
}

/*----------------------------------------------------------------------
|    RaopTimingThread::~RaopTimingThread
+---------------------------------------------------------------------*/
RaopTimingThread::~RaopTimingThread()
{
    // send ourselves a termination packet
    NPT_DataBuffer terminate_packet;
    terminate_packet.SetDataSize(8);
    unsigned char* payload = terminate_packet.UseData();
    NPT_SetMemory(payload, 0, 8);
    payload[0] = 0x80;
    payload[1] = BLT_RAOP_RTP_PACKET_TYPE_TERMINATE;
    
    // sending kill packet to timing socket
    NPT_UdpSocket kill_socket;
    NPT_SocketInfo socket_info;
    m_Output.m_TimingSocket->GetInfo(socket_info);
    NPT_IpAddress localhost;
    localhost.ResolveName("localhost");
    socket_info.local_address.SetIpAddress(localhost);
    kill_socket.Send(terminate_packet, &socket_info.local_address);
    
    // wait for thread to terminate
    ATX_LOG_FINE("waiting for thread to terminate");
    NPT_Result result = Wait(BLT_RAOP_MAX_THREAD_WAIT_TIMEOUT);
    if (result == NPT_ERROR_TIMEOUT) {
        ATX_LOG_WARNING("timed out waiting for thread");
    }
    ATX_LOG_FINE("thread terminated");
}

/*----------------------------------------------------------------------
|    RaopOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
RaopOutput_Create(BLT_Module*              module,
                  BLT_Core*                core, 
                  BLT_ModuleParametersType parameters_type,
                  const void*              parameters, 
                  BLT_MediaNode**          object)
{
    _RaopOutput*              self;
    BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;
    
    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* parse the name */
    NPT_String name;
    unsigned int version = 0;
    if (ATX_StringsEqualN(constructor->name, "raop://",  7)) {
        name = constructor->name+7;
        version = 1;
    } else if (ATX_StringsEqualN(constructor->name, "raopt://", 8)) {
        name = constructor->name+8;
        version = 0;
    } else {
        return BLT_ERROR_INTERNAL;
    }
    NPT_List<NPT_String> parts = name.Split(":");
    if (parts.GetItemCount() != 2) {
        ATX_LOG_WARNING("invalid syntax");
        return BLT_ERROR_INVALID_PARAMETERS;
    }
    NPT_String hostname = *parts.GetItem(0);
    unsigned int port = 0;
    if (NPT_FAILED((*parts.GetItem(1)).ToInteger(port))) {
        ATX_LOG_WARNING("invalid port syntax");
        return BLT_ERROR_INVALID_PARAMETERS;
    }
    
    NPT_String password;
    int pos = hostname.Find('@');
    if (pos >= 0) {
        password.Assign(hostname.GetChars(), pos);
        hostname = hostname.GetChars()+pos+1;
    }
    
    /* allocate memory for the object */
    self = (_RaopOutput*)ATX_AllocateZeroMemory(sizeof(_RaopOutput));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }
    self->object = new RaopOutput(version, hostname, port, password);
    
    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, _RaopOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, _RaopOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (self, _RaopOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE   (self, _RaopOutput, BLT_OutputNode);
    ATX_SET_INTERFACE   (self, _RaopOutput, BLT_MediaPort);
    ATX_SET_INTERFACE   (self, _RaopOutput, BLT_VolumeControl);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   RaopOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
RaopOutputModule_Probe(BLT_Module*              self, 
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

            /* the input protocol should be PACKET and the */
            /* output protocol should be NONE              */
            if ((constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_NONE)) {
                return BLT_FAILURE;
            }

            /* the input type should be unknown, or audio/pcm */
            if (!(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_AUDIO_PCM) &&
                !(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
                return BLT_FAILURE;
            }

            /* the name should be 'raop://[password@]hostname:port' */
            if (constructor->name == NULL ||
                (!ATX_StringsEqualN(constructor->name, "raop://",  7) &&
                 !ATX_StringsEqualN(constructor->name, "raopt://", 8))) {
                return BLT_FAILURE;
            }

            /* always an exact match, since we only respond to our name */
            *match = BLT_MODULE_PROBE_MATCH_EXACT;

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
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(RaopOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(RaopOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(RaopOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(RaopOutputModule, RaopOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(RaopOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    RaopOutputModule_CreateInstance,
    RaopOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define RaopOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(RaopOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_STANDARD_GET_MODULE(RaopOutputModule,
                                         "RAOP Output",
                                         "com.axiosys.output.raop",
                                         "1.0.0",
                                         BLT_MODULE_AXIOMATIC_COPYRIGHT)
