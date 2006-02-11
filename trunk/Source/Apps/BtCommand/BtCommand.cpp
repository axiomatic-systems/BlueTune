/*****************************************************************
|
|      File: BltCommand.cpp
|
|      BlueTune - Command Player
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file 
 * Main code for BtCommand
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "Atomix.h"
#include "NptUtils.h"
#include "Neptune.h"
#include "BlueTune.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
class BtCommand : public NPT_Thread,
                  public BLT_Player
{
public:
    // methods
    BtCommand();
    virtual ~BtCommand();

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
};

/*----------------------------------------------------------------------
|    BtCommand::BtCommand
+---------------------------------------------------------------------*/
BtCommand::BtCommand()
{
    // start our thread
    BLT_Debug(">>> BtCommand: starting input thread\n");
    Start();
}

/*----------------------------------------------------------------------
|    BtCommand::~BtCommand
+---------------------------------------------------------------------*/
BtCommand::~BtCommand()
{
    BLT_Debug(">>> BtCommand::~BtCommand\n");
}

/*----------------------------------------------------------------------
|    BtCommand::DoSeekToTimeStamp
+---------------------------------------------------------------------*/
void
BtCommand::DoSeekToTimeStamp(const char* time)
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
|    BtCommand::Run
+---------------------------------------------------------------------*/
void
BtCommand::Run()
{
    char       buffer[1024];
    bool       done = false;
    BLT_Result result;

    BLT_Debug(">>> BtCommand: running\n");

    // create the command stream
    NPT_File standard_in(NPT_FILE_STANDARD_INPUT);
    result = standard_in.Open(NPT_FILE_OPEN_MODE_READ |
                              NPT_FILE_OPEN_MODE_UNBUFFERED);
    if (NPT_FAILED(result)) return;

    // get the command stream
    NPT_InputStreamReference input_stream;
    standard_in.GetInputStream(input_stream);
    NPT_BufferedInputStream input(input_stream, 0);

    do {
        NPT_Size bytes_read;
        result = input.ReadLine(buffer, 
                                sizeof(buffer), 
                                &bytes_read);
        if (NPT_SUCCEEDED(result)) {
            if (NPT_StringsEqualN(buffer, "set-input ", 10)) {
                SetInput(&buffer[10]);
            } else if (NPT_StringsEqualN(buffer, "add-node ", 9)) {
                AddNode(&buffer[9]);
            } else if (NPT_StringsEqual(buffer, "play")) {
                Play();
            } else if (NPT_StringsEqual(buffer, "stop")) {
                Stop();
            } else if (NPT_StringsEqual(buffer, "pause")) {
                Pause();
            } else if (NPT_StringsEqualN(buffer, "seek-to-timestamp", 17)) {
                DoSeekToTimeStamp(buffer+17);
            } else if (NPT_StringsEqualN(buffer, "exit", 4)) {
                done = BLT_TRUE;
            } else {
                BLT_Debug("ERROR: invalid command\n");
            }
        } else {
            BLT_Debug("end: %d\n", result);
        }
    } while (BLT_SUCCEEDED(result) && !done);

    // terminate so that we can exit our message pump loop
    Terminate();

    BLT_Debug(">>> BtCommand: done\n");
}

/*----------------------------------------------------------------------
|    BtCommand::OnAckNotification
+---------------------------------------------------------------------*/
void
BtCommand::OnAckNotification(BLT_DecoderServer_Message::CommandId id)
{
    BLT_Debug("BLT_Player::OnAckNotification (id=%d)\n", id);
}

/*----------------------------------------------------------------------
|    BtCommand::OnAckNotification
+---------------------------------------------------------------------*/
void
BtCommand::OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                             BLT_Result                           result)
{
    BLT_Debug("BLT_Player::OnNackNotification (id=%d, result=%d)\n", 
              id, result);    
}

/*----------------------------------------------------------------------
|    BtCommand::OnDecoderStateNotification
+---------------------------------------------------------------------*/
void
BtCommand::OnDecoderStateNotification(BLT_DecoderServer::State state)
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
|    BtCommand::OnStreamTimeCodeNotification
+---------------------------------------------------------------------*/
void 
BtCommand::OnStreamTimeCodeNotification(BLT_TimeCode time_code)
{
    char time[32];
    sprintf(time, "%02d:%02d:%02d",
            time_code.h,
            time_code.m,
            time_code.s);
    BLT_Debug("BtCommand::OnStreamTimeCodeNotification - %s\n", time);
}

/*----------------------------------------------------------------------
|    BtCommand::OnStreamInfoNotification
+---------------------------------------------------------------------*/
void 
BtCommand::OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info)
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
        BLT_Debug("Stream Size = %ld\n", info.size);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
        BLT_Debug("Data Type = %s\n", info.data_type);
    }
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int /*argc*/, char** /*argv*/)
{
    // create the player
    BtCommand* player = new BtCommand();

    // pump notification messages
    while (player->PumpMessage() == NPT_SUCCESS) {/* */}
    BLT_Debug("BtCommand::main Received Terminate Message\n");

    // delete the player
    delete player;

    return 0;
}
