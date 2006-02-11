/*****************************************************************
|
|      File: BltServer.cpp
|
|      BlueTune - Server Player
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file 
 * Main code for BtServer
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "Atomix.h"
#include "Neptune.h"
#include "BlueTune.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
class BtServer : public NPT_Thread,
                 public BLT_Player
{
public:
    // methods
    BtServer();
    virtual ~BtServer();

    // NPT_Thread methods
    void Run();

    // BLT_DecoderClient_MessageHandler methods
    void OnAckNotification(BLT_DecoderServer_Message::CommandId id);
    void OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                            BLT_Result                           result);
    void OnDecoderStateNotification(BLT_DecoderServer::State state);
    void OnStreamTimeCodeNotification(BLT_TimeCode time_code);
    void OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info);

    // command methods
    void DoSeekToTimeStamp(const char* time);

private:
    // members
    NPT_FileByteStream* m_CommandStream;
};

/*----------------------------------------------------------------------
|    BtServer::BtServer
+---------------------------------------------------------------------*/
BtServer::BtServer()
{
    // create the command stream
    m_CommandStream = 
        new NPT_FileByteStream("-stdin", 
                               NPT_FILE_BYTE_STREAM_MODE_READ |
                               NPT_FILE_BYTE_STREAM_MODE_UNBUFFERED);

    // start our thread
    BLT_Debug(">>> BtServer: starting input thread\n");
    Start();
}

/*----------------------------------------------------------------------
|    BtServer::~BtServer
+---------------------------------------------------------------------*/
BtServer::~BtServer()
{
    BLT_Debug(">>> BtServer::~BtServer\n");
    if (m_CommandStream != NULL) {
        m_CommandStream->Release();
    }
}

/*----------------------------------------------------------------------
|    BtServer::DoSeekToTimeStamp
+---------------------------------------------------------------------*/
void
BtServer::DoSeekToTimeStamp(const char* time)
{
    BLT_UInt8 h;
    BLT_UInt8 m;
    BLT_UInt8 s;
    BLT_UInt8 f;
    BLT_Size  length = ATX_StringLength(time);

    if (length != 12 && length != 9) return;

    if (time[1] >= '0' && time[1] <= '9' && 
        time[2] >= '0' && time[2] <= '9' &&
        time[3] == ':') {
        h = (time[1]-'0')*10 + (time[2]-'0');
    } else {
        return;
    }
    if (time[4] >= '0' && time[4] <= '9' && 
        time[5] >= '0' && time[5] <= '9' &&
        time[6] == ':') {
        m = (time[4]-'0')*10 + (time[5]-'0');
    } else {
        return;
    }
    if (time[7] >= '0' && time[7] <= '9' && 
        time[8] >= '0' && time[8] <= '9') {
        s = (time[7]-'0')*10 + (time[8]-'0');
    } else {
        return;
    }
    if (length == 12) {
        if (time[10] >= '0' && time[10] <= '9' && 
            time[11] >= '0' && time[11] <= '9' &&
            time[ 9] == ':') {
            f = (time[10]-'0')*10 + (time[11]-'0');
        } else {
            return;
        }
    } else {
        f = 0;
    }

    SeekToTimeStamp(h, m, s, f);
}

/*----------------------------------------------------------------------
|    BtServer::Run
+---------------------------------------------------------------------*/
void
BtServer::Run()
{
    char       buffer[1024];
    BLT_Result result;

    BLT_Debug(">>> BtServer: running\n");

    do {
        NPT_Size bytes_read;
        result = m_CommandStream->ReadLine(buffer, 
                                           sizeof(buffer), 
                                           &bytes_read);
        if (NPT_SUCCEEDED(result)) {
            BLT_Debug("got %d bytes (%s)\n", bytes_read, buffer);    
            if (NPT_StringsEqualN(buffer, "set-input ", 10)) {
                SetInput(&buffer[10]);
            } else if (NPT_StringsEqual(buffer, "play")) {
                Play();
            } else if (NPT_StringsEqual(buffer, "stop")) {
                Stop();
            } else if (NPT_StringsEqualN(buffer, "seek-to-timestamp", 17)) {
                DoSeekToTimeStamp(buffer+17);
            }
        } else {
            BLT_Debug("end: %d\n", result);
        }
    } while (BLT_SUCCEEDED(result));

    // tell the decoder server to exit
    Exit();

    BLT_Debug(">>> BtServer: done\n");
}

/*----------------------------------------------------------------------
|    BtServer::OnAckNotification
+---------------------------------------------------------------------*/
void
BtServer::OnAckNotification(BLT_DecoderServer_Message::CommandId id)
{
    BLT_Debug("BLT_Player::OnAckNotification (id=%d)\n", id);
}

/*----------------------------------------------------------------------
|    BtServer::OnAckNotification
+---------------------------------------------------------------------*/
void
BtServer::OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                             BLT_Result                           result)
{
    BLT_Debug("BLT_Player::OnNackNotification (id=%d, result=%d)\n", 
              id, result);    
}

/*----------------------------------------------------------------------
|    BtServer::OnDecoderStateNotification
+---------------------------------------------------------------------*/
void
BtServer::OnDecoderStateNotification(BLT_DecoderServer::State state)
{
    BLT_Debug("BLT_Player::OnDecoderStateNotification state=");

    switch (state) {
      case BLT_DecoderServer::STATE_STOPPED:
        BLT_Debug("[STOPPED]\n");
        break;

      case BLT_DecoderServer::STATE_PLAYING:
        BLT_Debug("[PLAYING]\n");
        break;

      case BLT_DecoderServer::STATE_PAUSED:
        BLT_Debug("[PAUSED]\n");
        break;

      case BLT_DecoderServer::STATE_EOS:
        BLT_Debug("[END OF STREAM]\n");
        break;

      default:
        BLT_Debug("[UNKNOWN]\n");
        break;
    }
}

/*----------------------------------------------------------------------
|    BtServer::OnStreamTimeCodeNotification
+---------------------------------------------------------------------*/
void 
BtServer::OnStreamTimeCodeNotification(BLT_TimeCode time_code)
{
    char time[32];
    sprintf(time, "%02d:%02d:%02d",
            time_code.h,
            time_code.m,
            time_code.s);
    BLT_Debug("BtServer::OnStreamTimeCodeNotification - %s\n", time);
}

/*----------------------------------------------------------------------
|    BtServer::OnStreamInfoNotification
+---------------------------------------------------------------------*/
void 
BtServer::OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info)
{       
    if (update_mask & BLT_STREAM_INFO_MASK_NOMINAL_BITRATE) {
        BLT_Debug("Nominal Bitrate = %ld\n", info.nominal_bitrate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_AVERAGE_BITRATE) {
        BLT_Debug("Average Bitrate = %ld\n", info.average_bitrate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_INSTANT_BITRATE) {
        BLT_Debug("Instant Bitrate = %ld\n", info.instant_bitrate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_SAMPLE_RATE) {
        BLT_Debug("Sample Rate = %ld\n", info.sample_rate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_CHANNEL_COUNT) {
        BLT_Debug("Channels = %d\n", info.channel_count);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_SIZE) {
        BLT_Debug("Stream Size = %ld", info.size);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
        BLT_Debug("Data Type = %s\n", info.data_type);
    }
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
    BtServer server;
    BLT_COMPILER_UNUSED(argc);
    BLT_COMPILER_UNUSED(argv);

    // pump notification messages
    while (server.PumpMessage() == NPT_SUCCESS) {
        BLT_Debug("BtServer::main Got message\n");
    }
    BLT_Debug("BtServer::main Received Terminate Message\n");
    
    // wait for the server to exit
    server.Wait();

    return 0;
}
