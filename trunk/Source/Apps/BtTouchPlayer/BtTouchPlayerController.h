#import <UIKit/UIKit.h>
#import "BlueTune.h"

@interface TouchPlayerController : NSObject 
{
    BLT_PlayerObject*          player;
/*    IBOutlet NSButton*         playButton;
    IBOutlet NSTextField*      playerState;
    IBOutlet NSTextField*      playerInput;
    IBOutlet NSTextField*      playerTimecode;
    IBOutlet NSSlider*         playerPosition;
    IBOutlet NSTableView*      playerStreamInfoView;
    IBOutlet NSTableView*      playerPropertiesView;
*/
}

-(IBAction) play:     (id) sender;
-(IBAction) setInput: (id) sender;

-(void) ackWasReceived: (BLT_Player_CommandId) command_id;
-(void) nackWasReceived: (BLT_Player_CommandId) command_id result: (BLT_Result) result;
-(void) decoderStateDidChange: (BLT_Player_DecoderState) state;
-(void) streamTimecodeDidChange: (BLT_TimeCode) timecode;
-(void) streamPositionDidChange: (BLT_StreamPosition) position;
-(void) streamInfoDidChange: (const BLT_StreamInfo*) info updateMask: (BLT_Mask) mask;
-(void) propertyDidChange: (BLT_PropertyScope) scope source: (const char*) source name: (const char*) name value: (const ATX_PropertyValue*) value;

@end
