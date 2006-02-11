/*****************************************************************
|
|      File: BltDecoderServer.h
|
|      BlueTune - Async Layer
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_DECODER_SERVER_H_
#define _BLT_DECODER_SERVER_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "BltDecoder.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
const BLT_Size BLT_DECODER_SERVER_DEFAULT_POSITION_UPDATE_RANGE = 400;

/*----------------------------------------------------------------------
|       BLT_DecoderServer_MessageHandler
+---------------------------------------------------------------------*/
class BLT_DecoderServer_MessageHandler
{
public:
    // methods
    virtual void OnSetInputCommand(BLT_CString /*name*/, BLT_CString /*type*/) {}
    virtual void OnSetOutputCommand(BLT_CString /*name*/, BLT_CString /*type*/) {}
    virtual void OnPlayCommand()  {}
    virtual void OnStopCommand()  {}
    virtual void OnPauseCommand() {}
    virtual void OnPingCommand(const void* /*cookie*/) {}
    virtual void OnSeekToTimeCommand(BLT_Cardinal /*time*/) {}
    virtual void OnSeekToPositionCommand(BLT_Size /*offset*/, BLT_Size /*range*/) {}
    virtual void OnRegisterModuleCommand(const BLT_Module* /*module*/) {}
    virtual void OnAddNodeCommand(BLT_CString /*name*/) {}
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_Message
+---------------------------------------------------------------------*/
class BLT_DecoderServer_Message : public NPT_Message
{
public:
    // types
    typedef enum {
        COMMAND_ID_SET_INPUT,
        COMMAND_ID_SET_OUTPUT,
        COMMAND_ID_PLAY,
        COMMAND_ID_STOP,
        COMMAND_ID_PAUSE,
        COMMAND_ID_PING,
        COMMAND_ID_SEEK_TO_TIME,
        COMMAND_ID_SEEK_TO_POSITION,
        COMMAND_ID_REGISTER_MODULE,
        COMMAND_ID_ADD_NODE
    } CommandId;

    // functions
    static NPT_Message::Type MessageType;
    NPT_Message::Type GetType() {
        return MessageType;
    }

    // methods
    BLT_DecoderServer_Message(CommandId id) : m_Id(id) {}
    virtual NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) = 0;
    virtual NPT_Result Dispatch(NPT_MessageHandler* handler) {
        BLT_DecoderServer_MessageHandler* specific =
            dynamic_cast<BLT_DecoderServer_MessageHandler*>(handler);
        if (specific) {
            return Deliver(specific);
        } else {
            return DefaultDeliver(handler);
        }
    }

 private:
    // members
    CommandId m_Id;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_SetInputCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_SetInputCommandMessage :
    public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_SetInputCommandMessage(BLT_CString name, 
                                             BLT_CString type) :
        BLT_DecoderServer_Message(COMMAND_ID_SET_INPUT),
        m_Name(BLT_SAFE_STRING(name)), m_Type(BLT_SAFE_STRING(type)) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnSetInputCommand(m_Name.GetChars(), m_Type.GetChars());
        return NPT_SUCCESS;
    }

private:
    // members
    BLT_StringObject m_Name;
    BLT_StringObject m_Type;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_SetOutputCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_SetOutputCommandMessage :
    public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_SetOutputCommandMessage(BLT_CString name, 
                                              BLT_CString type) :
        BLT_DecoderServer_Message(COMMAND_ID_SET_OUTPUT),
        m_Name(BLT_SAFE_STRING(name)), m_Type(BLT_SAFE_STRING(type)) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnSetOutputCommand(m_Name.GetChars(), m_Type.GetChars());
        return NPT_SUCCESS;
    }

private:
    // members
    BLT_StringObject m_Name;
    BLT_StringObject m_Type;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_PlayCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_PlayCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_PlayCommandMessage() : 
        BLT_DecoderServer_Message(COMMAND_ID_PLAY) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnPlayCommand();
        return NPT_SUCCESS;
    }
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_StopCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_StopCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_StopCommandMessage() : 
        BLT_DecoderServer_Message(COMMAND_ID_STOP) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnStopCommand();
        return NPT_SUCCESS;
    }
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_PauseCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_PauseCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_PauseCommandMessage() : 
        BLT_DecoderServer_Message(COMMAND_ID_PAUSE) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnPauseCommand();
        return NPT_SUCCESS;
    }
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_PingCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_PingCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_PingCommandMessage(const void* cookie) : 
        BLT_DecoderServer_Message(COMMAND_ID_PING), m_Cookie(cookie) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnPingCommand(m_Cookie);
        return NPT_SUCCESS;
    }

private:
    // members
    const void* m_Cookie;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_SeekToTimeCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_SeekToTimeCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_SeekToTimeCommandMessage(BLT_Cardinal time) :
        BLT_DecoderServer_Message(COMMAND_ID_SEEK_TO_TIME),
        m_Time(time) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnSeekToTimeCommand(m_Time);
        return NPT_SUCCESS;
    }

 private:
    // members
    BLT_Cardinal m_Time;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_SeekToPositionCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_SeekToPositionCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_SeekToPositionCommandMessage(BLT_Size offset,
                                                   BLT_Size range) :
        BLT_DecoderServer_Message(COMMAND_ID_SEEK_TO_POSITION),
        m_Offset(offset), m_Range(range) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnSeekToPositionCommand(m_Offset, m_Range);
        return NPT_SUCCESS;
    }

 private:
    // members
    BLT_Size m_Offset;
    BLT_Size m_Range;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_RegisterModuleCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_RegisterModuleCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_RegisterModuleCommandMessage(BLT_Module* module) :
        BLT_DecoderServer_Message(COMMAND_ID_REGISTER_MODULE),
        m_Module(*module) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnRegisterModuleCommand(&m_Module);
        return NPT_SUCCESS;
    }

 private:
    // members
    BLT_Module m_Module;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer_AddNodeCommandMessage
+---------------------------------------------------------------------*/
class BLT_DecoderServer_AddNodeCommandMessage : public BLT_DecoderServer_Message
{
public:
    // methods
    BLT_DecoderServer_AddNodeCommandMessage(BLT_CString name) :
        BLT_DecoderServer_Message(COMMAND_ID_ADD_NODE),
        m_NodeName(name) {}
    NPT_Result Deliver(BLT_DecoderServer_MessageHandler* handler) {
        handler->OnAddNodeCommand(m_NodeName);
        return NPT_SUCCESS;
    }

 private:
    // members
    BLT_StringObject m_NodeName;
};

/*----------------------------------------------------------------------
|       BLT_DecoderServer
+---------------------------------------------------------------------*/
class BLT_DecoderServer : public NPT_Thread,
                          public NPT_MessageReceiver,
                          public NPT_MessageHandler,
                          public BLT_DecoderServer_MessageHandler
{
 public:
    // types
    typedef enum {
        STATE_STOPPED,
        STATE_PLAYING,
        STATE_PAUSED,
        STATE_EOS
    } State;

    // methods
    BLT_DecoderServer(NPT_MessageReceiver* client);
    virtual ~BLT_DecoderServer();
    virtual BLT_Result SetInput(BLT_CString name, BLT_CString type = NULL);
    virtual BLT_Result SetOutput(BLT_CString name, BLT_CString type = NULL);
    virtual BLT_Result Play();
    virtual BLT_Result Stop();
    virtual BLT_Result Pause();
    virtual BLT_Result Ping(const void* cookie);
    virtual BLT_Result SeekToTime(BLT_Cardinal time);
    virtual BLT_Result SeekToPosition(BLT_Size offset, BLT_Size range);
    virtual BLT_Result RegisterModule(BLT_Module* module);
    virtual BLT_Result AddNode(BLT_CString name);

    // NPT_Runnable methods
    void Run();

    // BLT_DecoderServer_MessageHandler methods
    void OnSetInputCommand(BLT_CString name, BLT_CString type);
    void OnSetOutputCommand(BLT_CString name, BLT_CString type);
    void OnPlayCommand();
    void OnStopCommand();
    void OnPauseCommand();
    void OnPingCommand(const void* cookie);
    void OnSeekToTimeCommand(BLT_Cardinal time);
    void OnSeekToPositionCommand(BLT_Size offset, BLT_Size range);
    void OnRegisterModuleCommand(const BLT_Module* module);
    void OnAddNodeCommand(BLT_CString name);

    // BLT_EventListener methods
    virtual BLT_Result OnEvent(const ATX_Object* source,
                               BLT_EventType     type,
                               const BLT_Event*  event);

 private:
    // methods
    BLT_Result SendReply(BLT_DecoderServer_Message::CommandId id, 
                         BLT_Result                           result);
    BLT_Result SetState(State state);
    BLT_Result UpdateStatus();
    BLT_Result NotifyPosition();
    BLT_Result NotifyTimeCode();

    // members
    BLT_Decoder*         m_Decoder;
    NPT_MessageReceiver* m_Client;
    NPT_MessageQueue*    m_MessageQueue;
    BLT_Cardinal         m_TimeStampUpdateQuantum;
    BLT_Size             m_PositionUpdateRange;
    BLT_DecoderStatus    m_DecoderStatus;
    State                m_State;
};

#endif /* _BLT_DECODER_SERVER_H_ */
