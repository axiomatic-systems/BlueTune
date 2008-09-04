/*****************************************************************
|
|   BlueTune - Async Layer
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltModule.h"
#include "BltCore.h"
#include "BltStreamPriv.h"
#include "BltMediaNode.h"
#include "BltRegistryPriv.h"
#include "BltMediaPacketPriv.h"
#include "BltPlayer.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.player")

/*----------------------------------------------------------------------
|    BLT_Player::BLT_Player
+---------------------------------------------------------------------*/
BLT_Player::BLT_Player(NPT_MessageQueue* queue) :
    BLT_DecoderClient(queue),
    m_Listener(NULL)
{
    // create a decoder server
    m_Server = new BLT_DecoderServer(this);
}

/*----------------------------------------------------------------------
|    BLT_Player::~BLT_Player
+---------------------------------------------------------------------*/
BLT_Player::~BLT_Player()
{
    ATX_LOG_FINE("BLT_Player::~BLT_Player");

    Shutdown();
}

/*----------------------------------------------------------------------
|    BLT_Player::SetEventListener
+---------------------------------------------------------------------*/
void
BLT_Player::SetEventListener(EventListener* listener)
{
    m_Listener = listener;
}

/*----------------------------------------------------------------------
|    BLT_Player::PumpMessage
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::PumpMessage(BLT_UInt32 timeout)
{
    return m_MessageQueue->PumpMessage(timeout);
}

/*----------------------------------------------------------------------
|    BLT_Player::Interrupt
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::Interrupt()
{
    ATX_LOG_FINE("BLT_Player::Interrupt");

    // send ourself a termination message
    PostMessage(new NPT_TerminateMessage);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Player::Shutdown
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::Shutdown()
{
    ATX_LOG_FINE("BLT_Player::Shutdown");

    delete m_Server;
    m_Server = NULL;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Player::SetInput
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::SetInput(BLT_CString name, BLT_CString type)
{
    ATX_LOG_FINE_2("BLT_Player::SetInput - name=%s, type=%d", BLT_SAFE_STRING(name), type);
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->SetInput(name, type);
}

/*----------------------------------------------------------------------
|    BLT_Player::SetOutput
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SetOutput(BLT_CString name, BLT_CString type)
{
    ATX_LOG_FINE_2(" BLT_Player::SetOutput - name=%s, type=%d", BLT_SAFE_STRING(name), type);
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->SetOutput(name, type);
}

/*----------------------------------------------------------------------
|    BLT_Player::Play
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::Play()
{
    ATX_LOG_FINE("BLT_Player::Play");
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->Play();
}

/*----------------------------------------------------------------------
|    BLT_Player::Stop
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::Stop()
{
    ATX_LOG_FINE("BLT_Player::Stop");
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->Stop();
}

/*----------------------------------------------------------------------
|    BLT_Player::Pause
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::Pause()
{
    ATX_LOG_FINE("BLT_Player::Pause");
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->Pause();
}

/*----------------------------------------------------------------------
|    BLT_Player::SeekToTime
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SeekToTime(BLT_UInt64 time)
{
    ATX_LOG_FINE_1("BLT_Player::SeekToTime - time=%d", (int)time);
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->SeekToTime(time);
}

/*----------------------------------------------------------------------
|    BLT_Player::SeekToTimeStamp
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SeekToTimeStamp(BLT_UInt8 h, 
                            BLT_UInt8 m, 
                            BLT_UInt8 s, 
                            BLT_UInt8 f)
{
    ATX_LOG_FINE_4("BLT_Player::SeekToTimeStamp, %d:%d:%d:%d", h,m,s,f);
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->SeekToTime(1000*(h*60*60+m*60+s)+10*f);
}

/*----------------------------------------------------------------------
|    BLT_Player::SeekToPosition
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SeekToPosition(BLT_UInt64 offset, BLT_UInt64 range)
{
    ATX_LOG_FINE_2("BLT_Player::SeekToPosition, offset=%d, range=%d", offset, range);
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->SeekToPosition(offset, range);
}

/*----------------------------------------------------------------------
|    BLT_Player::RegisterModule
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::Ping(const void* cookie)
{
    ATX_LOG_FINE_1("BLT_Player::Ping, cookie=%lx", NPT_POINTER_TO_LONG(cookie));
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->Ping(cookie);
}

/*----------------------------------------------------------------------
|    BLT_Player::RegisterModule
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::RegisterModule(BLT_Module* module)
{
    ATX_LOG_FINE("BLT_Player::RegisterModule");
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->RegisterModule(module);
}

/*----------------------------------------------------------------------
|    BLT_Player::AddNode
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::AddNode(BLT_CString name)
{
    ATX_LOG_FINE_1("BLT_Player::AddNode - name=%s", BLT_SAFE_STRING(name));
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->AddNode(name);
}

/*----------------------------------------------------------------------
|    BLT_Player::SetProperty
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::SetProperty(BLT_PropertyScope        scope,
                        const char*              target,
                        const char*              name,
                        const ATX_PropertyValue* value)
{
    ATX_LOG_FINE_1("BLT_Player::SetProperty - name=%s", BLT_SAFE_STRING(name));
    if (m_Server == NULL) return BLT_ERROR_INVALID_STATE;
    return m_Server->SetProperty(scope, target, name, value);
}

/*----------------------------------------------------------------------
|    BLT_PlayerAdapter
+---------------------------------------------------------------------*/
class BLT_PlayerAdapter : public BLT_Player 
{
public:
    static BLT_Player_CommandId MapCommandId(BLT_DecoderServer_Message::CommandId id) {
        switch (id) {
          case BLT_DecoderServer_Message::COMMAND_ID_SET_INPUT:
            return BLT_PLAYER_COMMAND_ID_SET_INPUT;
          case BLT_DecoderServer_Message::COMMAND_ID_SET_OUTPUT:
            return BLT_PLAYER_COMMAND_ID_SET_OUTPUT;
          case BLT_DecoderServer_Message::COMMAND_ID_PLAY:
            return BLT_PLAYER_COMMAND_ID_PLAY;
          case BLT_DecoderServer_Message::COMMAND_ID_STOP:
            return BLT_PLAYER_COMMAND_ID_PLAY;
          case BLT_DecoderServer_Message::COMMAND_ID_PAUSE:
            return BLT_PLAYER_COMMAND_ID_PAUSE;
          case BLT_DecoderServer_Message::COMMAND_ID_PING:
            return BLT_PLAYER_COMMAND_ID_PING;
          case BLT_DecoderServer_Message::COMMAND_ID_SEEK_TO_TIME:
            return BLT_PLAYER_COMMAND_ID_SEEK_TO_TIME;
          case BLT_DecoderServer_Message::COMMAND_ID_SEEK_TO_POSITION:
            return BLT_PLAYER_COMMAND_ID_SEEK_TO_POSITION;
          case BLT_DecoderServer_Message::COMMAND_ID_REGISTER_MODULE:
            return BLT_PLAYER_COMMAND_ID_REGISTER_MODULE;
          case BLT_DecoderServer_Message::COMMAND_ID_ADD_NODE:
            return BLT_PLAYER_COMMAND_ID_ADD_NODE;
          case BLT_DecoderServer_Message::COMMAND_ID_SET_PROPERTY:
            return BLT_PLAYER_COMMAND_ID_SET_PROPERTY;
        }
        return (BLT_Player_CommandId)(-1);
    }
    
    static BLT_Player_DecoderState MapDecoderState(BLT_DecoderServer::State state) {
        switch (state) {
          case BLT_DecoderServer::STATE_PLAYING: return BLT_PLAYER_DECODER_STATE_PLAYING;
          case BLT_DecoderServer::STATE_STOPPED: return BLT_PLAYER_DECODER_STATE_STOPPED;
          case BLT_DecoderServer::STATE_PAUSED:  return BLT_PLAYER_DECODER_STATE_PAUSED;
          case BLT_DecoderServer::STATE_EOS:     return BLT_PLAYER_DECODER_STATE_EOS;
          default: return BLT_PLAYER_DECODER_STATE_PLAYING;
        }
    }
    
    BLT_PlayerAdapter(BLT_Player_EventListener listener) : m_CListener(listener) {
        if (listener.handler == NULL) SetEventListener(NULL);
    }
    virtual void OnAckNotification(BLT_DecoderServer_Message::CommandId id) {
        BLT_Player_AckEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_ACK}, 
            MapCommandId(id)
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                                    BLT_Result                           result) {
        BLT_Player_NackEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_NACK}, 
            MapCommandId(id),
            result
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnPongNotification(const void* cookie) {
        BLT_Player_PongNotificationEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_PONG_NOTIFICATION}, 
            cookie
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnDecoderStateNotification(BLT_DecoderServer::State state) {
        BLT_Player_DecoderStateNotificationEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_DECODER_STATE_NOTIFICATION}, 
            MapDecoderState(state)
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnStreamTimeCodeNotification(BLT_TimeCode timecode) {
        BLT_Player_StreamTimeCodeNotificationEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_STREAM_TIMECODE_NOTIFICATION}, 
            timecode
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnStreamPositionNotification(BLT_StreamPosition& position) {
        BLT_Player_StreamPositionNotificationEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_STREAM_POSITION_NOTIFICATION}, 
            position
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnStreamInfoNotification(BLT_Mask        update_mask, 
                                          BLT_StreamInfo& info) {
        BLT_Player_StreamInfoNotificationEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_STREAM_INFO_NOTIFICATION}, 
            update_mask,
            info
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    virtual void OnPropertyNotification(BLT_PropertyScope        scope,
                                        const char*              source,
                                        const char*              name,
                                        const ATX_PropertyValue* value) {
        BLT_Player_PropertyNotificationEvent event = { 
            {BLT_PLAYER_EVENT_TYPE_PROPERTY_NOTIFICATION},
            scope, 
            source,
            name,
            value
        };
        m_CListener.handler(m_CListener.instance, &event.base);
    }
    
private:
    BLT_Player_EventListener m_CListener;
};

/*----------------------------------------------------------------------
|    BLT_Player_Create
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player_Create(BLT_Player_EventListener listener, BLT_Player** player)
{
    *player = new BLT_PlayerAdapter(listener);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Player_Destroy
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player_Destroy(BLT_Player* self)
{
    delete self;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Player_PumpMessage
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player_PumpMessage(BLT_Player* self, BLT_UInt32 timeout)
{
    return self->PumpMessage(timeout);
}

/*----------------------------------------------------------------------
|    BLT_Player_SetInput
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player_SetInput(BLT_Player* self, BLT_CString name, BLT_CString mime_type)
{
    return self->SetInput(name, mime_type);
}

/*----------------------------------------------------------------------
|    BLT_Player_Play
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player_Play(BLT_Player* self)
{
    return self->Play();
}

