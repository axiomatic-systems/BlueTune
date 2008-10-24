/*****************************************************************
|
|   Objective C Wrapper for the C++ class
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#import "BltPlayer.h"
#import "NptCocoaMessageQueue.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_ACK_WAS_RECEIVED               0x0001
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_NACK_WAS_RECEIVED              0x0002
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_PONG_WAS_RECEIVED              0x0004
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_DECODER_STATE_DID_CHANGE       0x0008
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_STREAM_TIMECODE_DID_CHANGE     0x0010
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_STREAM_POSITION_DID_CHANGE     0x0020
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_STREAM_INFO_DID_CHANGE         0x0040
#define BLT_PLAYER_DELEGATE_RESPONDS_TO_PROPERTY_DID_CHANGE            0x0080

/*----------------------------------------------------------------------
|   BLT_PlayerObjectiveC_Listener
+---------------------------------------------------------------------*/
class BLT_PlayerObjectiveC_Listener : public BLT_Player::EventListener {
public:
    BLT_PlayerObjectiveC_Listener(BLT_PlayerObject* target) : m_Target(target) {}
    
    // BLT_DecoderClient_MessageHandler methods
    void OnAckNotification(BLT_DecoderServer_Message::CommandId command_id) {
        if (![[m_Target delegate] respondsToSelector: @selector(ackWasReceived:)]) return;
        [[m_Target delegate] ackWasReceived: BLT_PlayerApiMapper::MapCommandId(command_id)];
    }
    void OnNackNotification(BLT_DecoderServer_Message::CommandId command_id,
                            BLT_Result                           result) {
        if (![[m_Target delegate] respondsToSelector: @selector(nackWasReceived:result:)]) return;
        [[m_Target delegate] nackWasReceived: BLT_PlayerApiMapper::MapCommandId(command_id) result: result];
    }
    void OnPongNotification(const void* cookie) {
        if (![[m_Target delegate] respondsToSelector: @selector(pongWasReceived:)]) return;
        [[m_Target delegate] pongWasReceived: cookie];
    }
    void OnDecoderStateNotification(BLT_DecoderServer::State state) {
        if (![[m_Target delegate] respondsToSelector: @selector(decoderStateDidChange:)]) return;
        [[m_Target delegate] decoderStateDidChange: BLT_PlayerApiMapper::MapDecoderState(state)];
    }
    void OnStreamTimeCodeNotification(BLT_TimeCode timecode) {
        if (![[m_Target delegate] respondsToSelector: @selector(streamTimecodeDidChange:)]) return;
        [[m_Target delegate] streamTimecodeDidChange: timecode];
    }
    void OnStreamPositionNotification(BLT_StreamPosition& position) {
        if (![[m_Target delegate] respondsToSelector: @selector(streamPositionDidChange:)]) return;
        [[m_Target delegate] streamPositionDidChange: position];
    }
    void OnStreamInfoNotification(BLT_Mask        update_mask, 
                                  BLT_StreamInfo& info) {
        if (![[m_Target delegate] respondsToSelector: @selector(streamInfoDidChange:updateMask:)]) return;
        [[m_Target delegate] streamInfoDidChange: &info updateMask: update_mask];
    }
    void OnPropertyNotification(BLT_PropertyScope        scope,
                                const char*              source,
                                const char*              name,
                                const ATX_PropertyValue* value) {
        if (![[m_Target delegate] respondsToSelector: @selector(propertyDidChange: source: name: value:)]) return;
        [[m_Target delegate] propertyDidChange: scope source: source name: name value: value];
    }
    
private:
    BLT_PlayerObject* m_Target;
};

/*----------------------------------------------------------------------
|   BLT_PlayerObjectiveC
+---------------------------------------------------------------------*/
@implementation BLT_PlayerObject
-(BLT_PlayerObject *) init
{
    if ((self = [super init])) {
        messageQueue  = new NPT_CocoaMessageQueue();
        player = new BLT_Player((NPT_CocoaMessageQueue*)messageQueue);
        player->SetEventListener(new BLT_PlayerObjectiveC_Listener(self));
    }
    return self;
}

-(void) dealloc
{
    [delegate release];
    player->SetEventListener(NULL);
    delete player->GetEventListener();
    delete player;
    delete ((NPT_CocoaMessageQueue*)messageQueue);
    [super dealloc];
}

-(id) delegate
{
    return delegate;
}

-(void) setDelegate: (id) new_delegate
{
    if (delegate == new_delegate) return;
    delegate = [new_delegate retain];
}

-(BLT_Player*) player
{
    return player;
}

-(BLT_Result) setInput: (NSString*) name
{
    return [self setInput: name withType: NULL];
}

-(BLT_Result) setInput: (NSString*) name withType: (NSString*) mime_type
{
    return player->SetInput([name UTF8String], [mime_type UTF8String]);
}

-(BLT_Result) play
{
    return player->Play();
}

-(BLT_Result) pause
{
    return player->Pause();
}

-(BLT_Result) stop
{
    return player->Stop();
}

@end
