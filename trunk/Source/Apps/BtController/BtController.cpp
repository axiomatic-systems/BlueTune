/*****************************************************************
|
|   BlueTune - Controller/Player
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "Atomix.h"
#include "NptUtils.h"
#include "Neptune.h"
#include "BlueTune.h"
#include "BtStreamController.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
class BtController : public BLT_Player
{
public:
    // methods
    BtController();
    virtual ~BtController();

    // BLT_DecoderClient_MessageHandler methods
    void OnAckNotification(BLT_DecoderServer_Message::CommandId id);
    void OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                            BLT_Result                           result);
    void OnDecoderStateNotification(BLT_DecoderServer::State state);
    void OnStreamTimeCodeNotification(BLT_TimeCode time_code);
    void OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info);
    void OnPropertyNotification(BLT_PropertyScope   scope,
                                const char*         source,
                                const ATX_Property& property);
    
    // command methods
    void DoSeekToTimeStamp(const char* time);

private:
    // members
    BtStreamController* m_ConsoleController;
};

/*----------------------------------------------------------------------
|    BtController::BtController
+---------------------------------------------------------------------*/
BtController::BtController()
{
    // create the command stream
    NPT_File standard_in(NPT_FILE_STANDARD_INPUT);
    NPT_Result result = standard_in.Open(NPT_FILE_OPEN_MODE_READ |
                                         NPT_FILE_OPEN_MODE_UNBUFFERED);
    if (NPT_FAILED(result)) return;

    // get the command stream
    NPT_InputStreamReference input_stream;
    standard_in.GetInputStream(input_stream);

    // create the controller
    m_ConsoleController = new BtStreamController(input_stream, *this);
    m_ConsoleController->Start();
}

/*----------------------------------------------------------------------
|    BtController::~BtController
+---------------------------------------------------------------------*/
BtController::~BtController()
{
    delete m_ConsoleController;
}

/*----------------------------------------------------------------------
|    BtController::OnAckNotification
+---------------------------------------------------------------------*/
void
BtController::OnAckNotification(BLT_DecoderServer_Message::CommandId id)
{
    ATX_Debug("BLT_Player::OnAckNotification (id=%d)\n", id);
}

/*----------------------------------------------------------------------
|    BtController::OnAckNotification
+---------------------------------------------------------------------*/
void
BtController::OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                             BLT_Result                           result)
{
    ATX_Debug("BLT_Player::OnNackNotification (id=%d, result=%d)\n", 
              id, result);    
}

/*----------------------------------------------------------------------
|    BtController::OnDecoderStateNotification
+---------------------------------------------------------------------*/
void
BtController::OnDecoderStateNotification(BLT_DecoderServer::State state)
{
    ATX_ConsoleOutput("BLT_Player::OnDecoderStateNotification state=");

    switch (state) {
      case BLT_DecoderServer::STATE_STOPPED:
        ATX_ConsoleOutput("[STOPPED]\n");
        break;

      case BLT_DecoderServer::STATE_PLAYING:
        ATX_ConsoleOutput("[PLAYING]\n");
        break;

      case BLT_DecoderServer::STATE_PAUSED:
        ATX_ConsoleOutput("[PAUSED]\n");
        break;

      case BLT_DecoderServer::STATE_EOS:
        ATX_ConsoleOutput("[END OF STREAM]\n");
        break;

      default:
        ATX_ConsoleOutput("[UNKNOWN]\n");
        break;
    }
}

/*----------------------------------------------------------------------
|    BtController::OnStreamTimeCodeNotification
+---------------------------------------------------------------------*/
void 
BtController::OnStreamTimeCodeNotification(BLT_TimeCode time_code)
{
    char time[32];
    ATX_FormatStringN(time, 32,
                      "%02d:%02d:%02d",
                      time_code.h,
                      time_code.m,
                      time_code.s);
    ATX_ConsoleOutputF("BtController::OnStreamTimeCodeNotification - %s\n", time);
}

/*----------------------------------------------------------------------
|    BtController::OnStreamInfoNotification
+---------------------------------------------------------------------*/
void 
BtController::OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info)
{       
    if (update_mask & BLT_STREAM_INFO_MASK_NOMINAL_BITRATE) {
        ATX_ConsoleOutputF("Nominal Bitrate = %ld\n", info.nominal_bitrate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_AVERAGE_BITRATE) {
        ATX_ConsoleOutputF("Average Bitrate = %ld\n", info.average_bitrate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_INSTANT_BITRATE) {
        ATX_ConsoleOutputF("Instant Bitrate = %ld\n", info.instant_bitrate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_SAMPLE_RATE) {
        ATX_ConsoleOutputF("Sample Rate = %ld\n", info.sample_rate);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_CHANNEL_COUNT) {
        ATX_ConsoleOutputF("Channels = %d\n", info.channel_count);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_SIZE) {
        ATX_ConsoleOutputF("Stream Size = %ld\n", info.size);
    }
    if (update_mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
        ATX_ConsoleOutputF("Data Type = %s\n", info.data_type);
    }
}

/*----------------------------------------------------------------------
|    BtController::OnPropertyNotification
+---------------------------------------------------------------------*/
void 
BtController::OnPropertyNotification(BLT_PropertyScope   scope,
                                     const char*       /*source*/,
                                     const ATX_Property& property)
{
    const char* scope_name;
    switch (scope) {
        case BLT_PROPERTY_SCOPE_CORE: scope_name = "core"; break;
        case BLT_PROPERTY_SCOPE_STREAM: scope_name = "stream"; break;
        default: scope_name = "unknown";
    }
    
    // when the name is NULL, it means that all the properties in that scope for
    // that source have been deleted 
    if (property.name == NULL || property.name[0] == '\0') {
        ATX_ConsoleOutputF("All properties in '%s' scope deleted\n", scope_name);
        return;
    }
    
    ATX_ConsoleOutputF("Property %s (%s) ", property.name, scope_name);
    
    switch (property.type) {
        case ATX_PROPERTY_TYPE_NONE:
            ATX_ConsoleOutputF("deleted\n");
            break;
            
        case ATX_PROPERTY_TYPE_INTEGER:
            ATX_ConsoleOutputF("= [I] %d\n", property.value.integer);
            break;
            
        case ATX_PROPERTY_TYPE_FLOAT:
            ATX_ConsoleOutputF("= [F] %f\n", property.value.fp);
            break;
            
        case ATX_PROPERTY_TYPE_STRING:
            ATX_ConsoleOutputF("= [S] %s\n", property.value.string);
            break;
            
        case ATX_PROPERTY_TYPE_BOOLEAN:
            ATX_ConsoleOutputF("= [B] %s\n", property.value.boolean == ATX_TRUE?"true":"false");
            break;
            
        case ATX_PROPERTY_TYPE_RAW_DATA:
            ATX_ConsoleOutputF("= [R] %d bytes of data\n", property.value.raw_data.size);
            break;
    }
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int /*argc*/, char** /*argv*/)
{
    // create the controller
    BtController* controller = new BtController();

    // pump notification messages
    while (controller->PumpMessage() == NPT_SUCCESS) {/* */}

    ATX_Debug("BtController::main Received Terminate Message\n");

    // delete the controller
    delete controller;

    return 0;
}
