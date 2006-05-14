/*****************************************************************
|
|   File: BltDecoderServer.c
|
|   BlueTune - Decoder Server
|
|   (c) 2002-2005 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Async Layer
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltDebug.h"
#include "BltDecoder.h"
#include "BltDecoderServer.h"
#include "BltDecoderClient.h"

/*----------------------------------------------------------------------
|   BLT_DecoderServer_Message::MessageType
+---------------------------------------------------------------------*/
NPT_Message::Type 
BLT_DecoderServer_Message::MessageType = "BLT_DecoderServer Message";

/*----------------------------------------------------------------------
|    forward references
+---------------------------------------------------------------------*/
BLT_VOID_METHOD 
BLT_DecoderServer_OnEvent(BLT_EventListener* self,
                          ATX_Object*        source,
                          BLT_EventType      type,
                          const BLT_Event*   event);

/*----------------------------------------------------------------------
|    BLT_EventListener interface
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLT_DecoderServer)
    ATX_GET_INTERFACE_ACCEPT(BLT_DecoderServer, BLT_EventListener)
ATX_END_GET_INTERFACE_IMPLEMENTATION

ATX_BEGIN_INTERFACE_MAP(BLT_DecoderServer, BLT_EventListener)
    BLT_DecoderServer_OnEvent
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_DecoderServer::BLT_DecoderServer
+---------------------------------------------------------------------*/
BLT_DecoderServer::BLT_DecoderServer(NPT_MessageReceiver* client) :
    m_Decoder(NULL),
    m_Client(client),
    m_TimeStampUpdateQuantum(1000),
    m_PositionUpdateRange(BLT_DECODER_SERVER_DEFAULT_POSITION_UPDATE_RANGE),
    m_State(STATE_STOPPED)
{
    // create a queue to receive messages
    m_MessageQueue = new NPT_SimpleMessageQueue();

    // attach the queue as the receiving queue
    SetQueue(m_MessageQueue);
    
    // register ourselves as the message handler
    SetHandler(this);

    // reset some fields
    m_DecoderStatus.position.range  = m_PositionUpdateRange;
    m_DecoderStatus.position.offset = 0;

    // setup our listener interface
    ATX_SET_INTERFACE(this, BLT_DecoderServer, BLT_EventListener);

    // start the thread
    Start();
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::~BLT_DecoderServer
+---------------------------------------------------------------------*/
BLT_DecoderServer::~BLT_DecoderServer()
{
    BLT_Debug("BLT_DecoderServer::~BLT_DecoderServer\n");

    // send a message to the thread to make it terminate
    PostMessage(new NPT_TerminateMessage);
    
    // wait for the thread to terminate
    Wait();

    // delete the message queue
    delete m_MessageQueue;
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::Run
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::Run()
{
    BLT_Result result;

    // create the decoder
    result = BLT_Decoder_Create(&m_Decoder);
    if (BLT_FAILED(result)) return;
        
    // register as the event handler
    BLT_Decoder_SetEventListener(m_Decoder, &ATX_BASE(this, BLT_EventListener));

    // register builtins 
    result = BLT_Decoder_RegisterBuiltins(m_Decoder);
    if (BLT_FAILED(result)) return;

    // set default output, default type
    result = BLT_Decoder_SetOutput(m_Decoder, 
                                   BLT_DECODER_DEFAULT_OUTPUT_NAME, 
                                   NULL);
    if (BLT_FAILED(result)) return;
    
    // notify the client of the initial state
    m_Client->PostMessage(
        new BLT_DecoderClient_DecoderStateNotificationMessage(STATE_STOPPED));

    // initial status
    BLT_Decoder_GetStatus(m_Decoder, &m_DecoderStatus);
    m_DecoderStatus.position.range  = m_PositionUpdateRange;
    NotifyTimeCode();
    NotifyPosition();

    // decoding loop
    do {
        do {
            result = m_MessageQueue->PumpMessage(false);
        } while (BLT_SUCCEEDED(result));
        
        if (result != NPT_ERROR_LIST_EMPTY) {
            break;
        }

        if (m_State == STATE_PLAYING) {
            result = BLT_Decoder_PumpPacket(m_Decoder);
            if (BLT_FAILED(result)) {
                BLT_Debug("BLT_DecoderServer::Run - stopped on %d\n", result);
                SetState(STATE_EOS);
                result = BLT_SUCCESS;
            } else {
                UpdateStatus();
            }
        } else {
            BLT_Debug("BLT_DecoderServer::Run - waiting for message\n");
            result = m_MessageQueue->PumpMessage(true);
            BLT_Debug("BLT_DecoderServer::Run - got message\n");
        }
    } while (BLT_SUCCEEDED(result));

    BLT_Debug("BLT_DecoderServer::Run - Received Terminate Message\n");

    // unregister as an event listener
    BLT_Decoder_SetEventListener(m_Decoder, NULL);

    // destroy the decoder
    if (m_Decoder != NULL) {
        BLT_Decoder_Destroy(m_Decoder);
    }  
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::SendReply
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderServer::SendReply(BLT_DecoderServer_Message::CommandId id, 
                             BLT_Result                           result)
{
    BLT_DecoderClient_Message* reply;

    if (BLT_SUCCEEDED(result)) {
        reply = new BLT_DecoderClient_AckNotificationMessage(id);
    } else {
        reply = new BLT_DecoderClient_NackNotificationMessage(id, result);
    }

    return m_Client->PostMessage(reply);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::NotifyTimeCode
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::NotifyTimeCode()
{
    BLT_TimeCode time_code;
    BLT_Cardinal seconds = m_DecoderStatus.time_stamp.seconds;
    time_code.h = (BLT_UInt8)(seconds/(60*60));
    seconds -= time_code.h*(60*60);
    time_code.m = (BLT_UInt8)(seconds/60);
    seconds -= time_code.m*60;
    time_code.s = (BLT_UInt8)seconds;
    time_code.f = (BLT_UInt8)(m_DecoderStatus.time_stamp.nanoseconds/10000000);
    return m_Client->PostMessage(
        new BLT_DecoderClient_StreamTimeCodeNotificationMessage(time_code));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::NotifyPosition
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::NotifyPosition()
{
    return m_Client->PostMessage(
        new BLT_DecoderClient_StreamPositionNotificationMessage(
            m_DecoderStatus.position));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::UpdateStatus
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::UpdateStatus()
{
    BLT_DecoderStatus status;
    BLT_Result        result;

    // get the decoder status
    result = BLT_Decoder_GetStatus(m_Decoder, &status);
    if (BLT_FAILED(result)) return result;

    // notify if the time has changed by more than the update threshold
    ATX_Int64 previous;
    BLT_TimeStamp_ToInt64(m_DecoderStatus.time_stamp, previous);
    ATX_Int64_Div_Int32(previous, 1000000);
    ATX_Int64 current;
    BLT_TimeStamp_ToInt64(status.time_stamp, current);
    ATX_Int64_Div_Int32(current, 1000000);
    if (m_TimeStampUpdateQuantum) {
        // make the new time stamp a multiple of the update quantum
        ATX_Int64_Div_Int32(current, m_TimeStampUpdateQuantum);
        ATX_Int64_Mul_Int32(current, m_TimeStampUpdateQuantum);
    }
    BLT_Int32 c32 = ATX_Int64_Get_Int32(current);
    if (ATX_Int64_Get_Int32(previous) != c32) {
        BLT_TimeStamp_Set(m_DecoderStatus.time_stamp, 
                          c32/1000,
                          1000000*(c32 - 1000*(c32/1000)));
        //BLT_Debug("********* timestamp : %d.%09d\n", 
        //          m_DecoderStatus.time_stamp.seconds,
        //          m_DecoderStatus.time_stamp.nanoseconds);
        NotifyTimeCode();
    }

    // convert the stream position into a decoder position 
    if (m_PositionUpdateRange != 0) {
        unsigned long ratio = status.position.range/m_PositionUpdateRange;
        unsigned long offset;
        if (ratio == 0) {
 	    offset = 0;
	} else {
	    offset = status.position.offset/ratio;
	}
	if (offset != m_DecoderStatus.position.offset) {
	    m_DecoderStatus.position.offset = offset;
	    NotifyPosition();
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::SetState
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::SetState(State state)
{
    // shortcut
    if (state == m_State) return BLT_SUCCESS;

    BLT_Debug("BLT_DecoderServer::SetState - from %d to %d\n", 
              m_State, state);

    m_State = state;

    // notify the client
    m_Client->PostMessage(
        new BLT_DecoderClient_DecoderStateNotificationMessage(state));

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::SetInput
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderServer::SetInput(BLT_CString name, BLT_CString type)
{
    return PostMessage(
        new BLT_DecoderServer_SetInputCommandMessage(name, type));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnSetInputCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnSetInputCommand(BLT_CString name, BLT_CString type)
{
    BLT_Result result;

    BLT_Debug("BLT_DecoderServer::OnSetInputCommand (%s / %s)\n",
              BLT_SAFE_STRING(name), BLT_SAFE_STRING(type));
    result = BLT_Decoder_SetInput(m_Decoder, name, type);
    UpdateStatus();
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_SET_INPUT, result);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::SetOutput
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderServer::SetOutput(BLT_CString name, BLT_CString type)
{
    return PostMessage(
        new BLT_DecoderServer_SetOutputCommandMessage(name, type));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnSetOutputCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnSetOutputCommand(BLT_CString name, BLT_CString type)
{
    BLT_Result result;

    BLT_Debug("BLT_DecoderServer::OnSetOutputCommand\n");
    result = BLT_Decoder_SetOutput(m_Decoder, name, type);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_SET_OUTPUT, result);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::Play
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderServer::Play()
{
    return PostMessage(new BLT_DecoderServer_PlayCommandMessage);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnPlayCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnPlayCommand()
{
    BLT_Debug("BLT_DecoderServer::OnPlayCommand\n");

    SetState(STATE_PLAYING);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_PLAY, BLT_SUCCESS);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::Stop
+---------------------------------------------------------------------*/
BLT_Result BLT_DecoderServer::Stop()
{
    return PostMessage(new BLT_DecoderServer_StopCommandMessage);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnStopCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnStopCommand()
{
    BLT_Debug("BLT_DecoderServer::OnStopCommand\n");
    BLT_Decoder_Stop(m_Decoder);
    SetState(STATE_STOPPED);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_STOP, BLT_SUCCESS);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::Pause
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderServer::Pause()
{
    return PostMessage(new BLT_DecoderServer_PauseCommandMessage);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnPauseCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnPauseCommand()
{
    BLT_Debug("BLT_DecoderServer::OnPauseCommand\n");
    BLT_Decoder_Pause(m_Decoder);
    SetState(STATE_PAUSED);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_PAUSE, BLT_SUCCESS);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::Ping
+---------------------------------------------------------------------*/
BLT_Result 
BLT_DecoderServer::Ping(const void* cookie)
{
    return PostMessage(new BLT_DecoderServer_PingCommandMessage(cookie));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnPingCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnPingCommand(const void* cookie)
{
    BLT_Debug("BLT_DecoderServer::OnPingCommand\n");
    BLT_DecoderClient_Message* pong;
    pong = new BLT_DecoderClient_PongNotificationMessage(cookie);
    m_Client->PostMessage(pong);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_PING, BLT_SUCCESS);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::SeekToTime
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::SeekToTime(BLT_Cardinal time)
{
    return PostMessage(new BLT_DecoderServer_SeekToTimeCommandMessage(time));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnSeekToTimeCommand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnSeekToTimeCommand(BLT_Cardinal time)
{
    BLT_Result result;
    BLT_Debug("BLT_DecoderServer::OnSeekToTimeCommand "
              "[%02d]\n", time);
    result = BLT_Decoder_SeekToTime(m_Decoder, time);
    if (BLT_SUCCEEDED(result)) {
        UpdateStatus();
    }
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_SEEK_TO_TIME, result);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::SeekToPosition
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::SeekToPosition(BLT_Size offset, BLT_Size range)
{
    return PostMessage(
        new BLT_DecoderServer_SeekToPositionCommandMessage(offset, range));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnSeekToPositionCommnand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnSeekToPositionCommand(BLT_Size offset, BLT_Size range)
{
    BLT_Result result;
    BLT_Debug("BLT_DecoderServer::OnSeekToPositionCommand "
              "[%d:%d]\n", offset, range);
    result = BLT_Decoder_SeekToPosition(m_Decoder, offset, range);
    if (BLT_SUCCEEDED(result)) {
        UpdateStatus();
    }
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_SEEK_TO_POSITION, result);
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::RegisterModule
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::RegisterModule(BLT_Module* module)
{
    return PostMessage(
        new BLT_DecoderServer_RegisterModuleCommandMessage(module));
}

/*----------------------------------------------------------------------
|    BLT_DecoderServer::OnRegisterModuleCommnand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnRegisterModuleCommand(BLT_Module* module)
{
    BLT_Result result;
    BLT_Debug("BLT_DecoderServer::OnRegisterModuleCommand\n");
    result = BLT_Decoder_RegisterModule(m_Decoder, module);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_REGISTER_MODULE, result);
}

/*----------------------------------------------------------------------
|   BLT_DecoderServer::AddNode
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::AddNode(BLT_CString name)
{
    return PostMessage(
        new BLT_DecoderServer_AddNodeCommandMessage(name));
}

/*----------------------------------------------------------------------
|   BLT_DecoderServer::OnAddNodeCommnand
+---------------------------------------------------------------------*/
void
BLT_DecoderServer::OnAddNodeCommand(BLT_CString name)
{
    BLT_Result result;
    BLT_Debug("BLT_DecoderServer::OnAddNodeCommand [%s]\n", name);
    result = BLT_Decoder_AddNodeByName(m_Decoder, NULL, name);
    SendReply(BLT_DecoderServer_Message::COMMAND_ID_ADD_NODE, result);
}

/*----------------------------------------------------------------------
|   BLT_DecoderServer::OnEvent
+---------------------------------------------------------------------*/
BLT_Result
BLT_DecoderServer::OnEvent(const ATX_Object* /*source*/,
                           BLT_EventType     type,
                           const BLT_Event*  event)
{
    switch (type) {
      case BLT_EVENT_TYPE_STREAM_INFO: {
          BLT_StreamInfoEvent* e = (BLT_StreamInfoEvent*)event;
          return m_Client->PostMessage(
              new BLT_DecoderClient_StreamInfoNotificationMessage(
                  e->update_mask,
                  e->info));
      }

      default:
        break;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_DecoderServer_OnEvent
+---------------------------------------------------------------------*/
BLT_VOID_METHOD 
BLT_DecoderServer_OnEvent(BLT_EventListener* _self,
                          ATX_Object*        source,
                          BLT_EventType      type,
                          const BLT_Event*   event)
{
    BLT_DecoderServer* server = ATX_SELF(BLT_DecoderServer, BLT_EventListener);
    server->OnEvent(source, type, event);
}
