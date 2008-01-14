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
    BLT_DecoderClient(queue)
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
BLT_Player::PumpMessage(bool blocking)
{
    return m_MessageQueue->PumpMessage(blocking);
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
BLT_Player::SeekToTime(BLT_Cardinal time)
{
    ATX_LOG_FINE_1("BLT_Player::SeekToTime - time=%d", time);
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
BLT_Player::SeekToPosition(BLT_Size offset, BLT_Size range)
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
