#import "BtTouchPlayerController.h"

@interface CocoaPlayerRecordList : NSObject<UITableViewDataSource>
{
    NSMutableArray* records;
    NSString*       titleString;
}
-(NSInteger) numberOfSectionsInTableView:(UITableView *)tableView;
-(NSInteger) tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section;
-(UITableViewCell*) tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath;
-(NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section;
@end


@implementation CocoaPlayerRecordList

-(id) initWithTitle:(NSString*) title
{
    if ((self = [super init])) {
        records = [[NSMutableArray alloc] init];
    }
    titleString = [title retain];
    return self;
}

-(void) dealloc
{
    [records release];
    [super dealloc];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    BLT_COMPILER_UNUSED(tableView);
	return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    BLT_COMPILER_UNUSED(tableView);
    BLT_COMPILER_UNUSED(section);
	return [records count];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
	static NSString *MyIdentifier = @"MyIdentifier";
	
	// Try to retrieve from the table view a now-unused cell with the given identifier.
	UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:MyIdentifier];
	
	// If no cell is available, create a new one using the given identifier.
	if (cell == nil) {
		// Use the default cell style.
		cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:MyIdentifier] autorelease];
	}

    NSDictionary* entry = [records objectAtIndex: indexPath.row];
    NSString* entryText = [NSString stringWithFormat:@"%@: %@", [entry valueForKey: @"Name"], [entry valueForKey: @"Value"]];
    cell.textLabel.text = entryText;
	
	return cell;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    BLT_COMPILER_UNUSED(tableView);
    BLT_COMPILER_UNUSED(section);
    return titleString;
}

-(void) setProperty: (NSString*) name value: (NSString*) value
{
    NSMutableDictionary* dict = [[NSMutableDictionary alloc] init];
    [dict setValue: name forKey: @"Name"];
    [dict setValue: value forKey: @"Value"];
    
    unsigned int record_count = (unsigned int)[records count];
    unsigned int i;
    for (i=0; i<record_count; i++) {
        NSString* record_name = [[records objectAtIndex: i] valueForKey: @"Name"];
        if ([record_name compare: name] == NSOrderedSame) {
            [records replaceObjectAtIndex: i withObject: dict];
            return;
        }
    }
    [records addObject: dict];
}

@end

@implementation TouchPlayerController
-(id) init 
{
    if ((self = [super init])) {
        player = [[BLT_PlayerObject alloc] init];
        [player setDelegate: self];
    }
    volume = 1.0f;
    pendingVolume = 1.0f;
    positionSliderPressed = FALSE;
    return self;
}

-(void) awakeFromNib
{        
    // set the data source for the list views
    playerPropertiesView.dataSource = [[CocoaPlayerRecordList alloc]initWithTitle: @"Properties"];
    playerStreamInfoView.dataSource = [[CocoaPlayerRecordList alloc]initWithTitle: @"Stream Info"];
}

-(IBAction) play: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    [player play];
}

-(IBAction) pause: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    [player pause];
}

-(IBAction) stop: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    [player stop];
}

-(IBAction) setInput: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    //[player setInput: @"http://www.bok.net/tmp/test.mp3"];
    [player stop];
    [player setInput: [playerInput text]];
}

-(IBAction) seek: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    positionSliderPressed = FALSE;
    [player seekToPosition: (unsigned int)(playerPosition.value*1000.0f)  range: (unsigned int)(playerPosition.maximumValue*1000.0f)];
}

-(IBAction) setVolume: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    if (pendingVolume == volume) {
        [player setVolume: [playerVolume value]];
    }
    pendingVolume = [playerVolume value];
}

-(IBAction) positionSliderWasPressed: (id) sender;
{
    BLT_COMPILER_UNUSED(sender);
    positionSliderPressed = TRUE;
}

-(void) ackWasReceived: (BLT_Player_CommandId) command_id
{
    printf("ACK %d\n", command_id);
    
    if (command_id == BLT_PLAYER_COMMAND_ID_SET_INPUT) {
        // autoplay
        [player play];
    } else if (command_id == BLT_PLAYER_COMMAND_ID_SET_VOLUME) {
        // update the actual volume value
        volume = pendingVolume;
    }
}

-(void) nackWasReceived: (BLT_Player_CommandId) command_id result: (BLT_Result) result
{
    printf("NACK %d, %d\n", command_id, result);
}

-(void) decoderStateDidChange: (BLT_Player_DecoderState) state
{
    switch (state) {
        case BLT_PLAYER_DECODER_STATE_EOS:
            playerState.text = @"[EOS]"; break;
        case BLT_PLAYER_DECODER_STATE_STOPPED:
            playerState.text = @"[STOPPED]"; break;
        case BLT_PLAYER_DECODER_STATE_PLAYING:
            playerState.text = @"[PLAYING]"; break;
        case BLT_PLAYER_DECODER_STATE_PAUSED:
            playerState.text = @"[PAUSED]"; break;
    }
}

-(void) streamTimecodeDidChange: (BLT_TimeCode) timecode
{
    NSString* timecode_string = [NSString stringWithFormat:@"%02d:%02d:%02d", timecode.h, timecode.m, timecode.s];
    playerTimecode.text = timecode_string;
}

-(void) streamPositionDidChange: (BLT_StreamPosition) position
{
    if (!positionSliderPressed) {
        float where = (float)position.offset/(float)position.range;
        playerPosition.value = where;
    }
}

-(void) streamInfoDidChange: (const BLT_StreamInfo*) info updateMask: (BLT_Mask) mask
{
    BLT_COMPILER_UNUSED(info);
    BLT_COMPILER_UNUSED(mask);

    if (mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Data Type" value: [NSString stringWithUTF8String: info->data_type]];
    }
    if (mask & BLT_STREAM_INFO_MASK_DURATION) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Duration" value: [NSString stringWithFormat:@"%2f", (float)info->duration/1000.0f]];
    }
    if (mask & BLT_STREAM_INFO_MASK_NOMINAL_BITRATE) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Nominal Bitrate" value: [NSString stringWithFormat:@"%d", info->nominal_bitrate]];
    }
    if (mask & BLT_STREAM_INFO_MASK_AVERAGE_BITRATE) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Average Bitrate" value: [NSString stringWithFormat:@"%d", info->average_bitrate]];
    }
    if (mask & BLT_STREAM_INFO_MASK_INSTANT_BITRATE) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Instant Bitrate" value: [NSString stringWithFormat:@"%d", info->instant_bitrate]];
    }
    if (mask & BLT_STREAM_INFO_MASK_SIZE) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Size" value: [NSString stringWithFormat:@"%lld", info->size]];
    }
    if (mask & BLT_STREAM_INFO_MASK_SAMPLE_RATE) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Sample Rate" value: [NSString stringWithFormat:@"%d", info->sample_rate]];
    }
    if (mask & BLT_STREAM_INFO_MASK_CHANNEL_COUNT) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Channels" value: [NSString stringWithFormat:@"%d", info->channel_count]];
    }
    if (mask & BLT_STREAM_INFO_MASK_FLAGS) {
        [(CocoaPlayerRecordList*)[playerStreamInfoView dataSource] setProperty: @"Flags" value: [NSString stringWithFormat:@"%x", info->flags]];
    }
    [playerStreamInfoView reloadData];
}

-(void) propertyDidChange: (BLT_PropertyScope)        scope 
                   source: (const char*)              source 
                     name: (const char*)              name 
                    value: (const ATX_PropertyValue*) value
{
    BLT_COMPILER_UNUSED(scope);
    BLT_COMPILER_UNUSED(source);

    if (name == NULL || value == NULL) return;
    
    NSString* value_string = nil;
    switch (value->type) {
        case ATX_PROPERTY_VALUE_TYPE_STRING:
            value_string = [NSString stringWithUTF8String: value->data.string];
            break;

        case ATX_PROPERTY_VALUE_TYPE_INTEGER:
            value_string = [NSString stringWithFormat:@"%d", value->data.integer];
            break;
            
        default:
            break;
    }
    if (value_string) {
        [(CocoaPlayerRecordList*)[playerPropertiesView dataSource] setProperty: [NSString stringWithUTF8String: name] value: value_string];
    }
    [playerPropertiesView reloadData];
}

@end
