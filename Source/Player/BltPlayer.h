/*****************************************************************
|
|      File: BltPlayer.h
|
|      BlueTune - Async Layer
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_PLAYER_H_
#define _BLT_PLAYER_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "BltDecoder.h"
#include "BltDecoderClient.h"
#include "BltDecoderServer.h"

/*----------------------------------------------------------------------
|       BLT_Player
+---------------------------------------------------------------------*/
class BLT_Player : public BLT_DecoderClient
{
 public:
    // methods
                       BLT_Player(NPT_MessageQueue* queue = NULL);
    virtual           ~BLT_Player();
    virtual BLT_Result PumpMessage(bool blocking = true);
    virtual BLT_Result Terminate();
    virtual BLT_Result SetInput(BLT_CString name, BLT_CString type = NULL);
    virtual BLT_Result SetOutput(BLT_CString name, BLT_CString type = NULL);
    virtual BLT_Result Play();
    virtual BLT_Result Stop();
    virtual BLT_Result Pause();
    virtual BLT_Result SeekToTime(BLT_Cardinal time);
    virtual BLT_Result SeekToTimeStamp(BLT_UInt8 h, 
                                       BLT_UInt8 m, 
                                       BLT_UInt8 s, 
                                       BLT_UInt8 f);
    virtual BLT_Result SeekToPosition(BLT_Size offset, BLT_Size range);
    virtual BLT_Result RegisterModule(BLT_Module* module);
    virtual BLT_Result AddNode(BLT_CString name);

private:
    // members
    BLT_DecoderServer* m_Server;
};

#endif /* _BLT_PLAYER_H_ */
