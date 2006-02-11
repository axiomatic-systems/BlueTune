/*****************************************************************
|
|      File: BltPlayer.c
|
|      BlueTune - Async Layer
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltDebug.h"
#include "BltModule.h"
#include "BltCore.h"
#include "BltCorePriv.h"
#include "BltStreamPriv.h"
#include "BltMediaNode.h"
#include "BltRegistryPriv.h"
#include "BltMediaPacketPriv.h"
#include "BltPlayer.h"

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
    // delete the server (the server thread will terminate by itself)
    BLT_Debug("BLT_Player::~BLT_Player\n");
    delete m_Server;
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
|    BLT_Player::Terminate
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::Terminate()
{
    BLT_Debug("BLT_Player::Terminate\n");

    // send ourself a termination message
    PostMessage(new NPT_TerminateMessage);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_Player::SetInput
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::SetInput(BLT_CString name, BLT_CString type)
{
    BLT_Debug("BLT_Player::SetInput\n");
    return m_Server->SetInput(name, type);
}

/*----------------------------------------------------------------------
|    BLT_Player::SetOutput
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SetOutput(BLT_CString name, BLT_CString type)
{
    BLT_Debug(" BLT_Player::SetOutput\n");
    return m_Server->SetOutput(name, type);
}

/*----------------------------------------------------------------------
|    BLT_Player::Play
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::Play()
{
    BLT_Debug("BLT_Player::Play\n");
    return m_Server->Play();
}

/*----------------------------------------------------------------------
|    BLT_Player::Stop
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::Stop()
{
    BLT_Debug("BLT_Player::Stop\n");
    return m_Server->Stop();
}

/*----------------------------------------------------------------------
|    BLT_Player::Pause
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Player::Pause()
{
    BLT_Debug("BLT_Player::Pause\n");
    return m_Server->Pause();
}

/*----------------------------------------------------------------------
|    BLT_Player::SeekToTime
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SeekToTime(BLT_Cardinal time)
{
    BLT_Debug("BLT_Player::SeekToTime\n");
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
    BLT_Debug("BLT_Player::SeekToTimeStamp\n");
    return m_Server->SeekToTime(1000*(h*60*60+m*60+s)+10*f);
}

/*----------------------------------------------------------------------
|    BLT_Player::SeekToPosition
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::SeekToPosition(BLT_Size offset, BLT_Size range)
{
    BLT_Debug("BLT_Player::SeekToPosition\n");
    return m_Server->SeekToPosition(offset, range);
}

/*----------------------------------------------------------------------
|    BLT_Player::RegisterModule
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::RegisterModule(BLT_Module* module)
{
    BLT_Debug("BLT_Player::RegisterModule\n");
    return m_Server->RegisterModule(module);
}

/*----------------------------------------------------------------------
|    BLT_Player::AddNode
+---------------------------------------------------------------------*/
BLT_Result
BLT_Player::AddNode(BLT_CString name)
{
    BLT_Debug("BLT_Player::AddNode\n");
    return m_Server->AddNode(name);
}
