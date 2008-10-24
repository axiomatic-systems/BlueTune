/*****************************************************************
|
|   MacOSX Video Output Module
|
|   (c) 2002-2007 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreAudio/HostTime.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltOsxVideoOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"
#include "BltPixels.h"
#include "BltTime.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.macosx.video")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE 5
#define BLT_MACOSX_VIDEO_MIN_VIEW_WIDTH     8
#define BLT_MACOSX_VIDEO_MIN_VIEW_HEIGHT    8
#define BLT_MACOSX_VIDEO_MIN_PICTURE_WIDTH  8
#define BLT_MACOSX_VIDEO_MIN_PICTURE_HEIGHT 8

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} OsxVideoOutputModule;

@interface OsxVideoPicture : NSObject
{
    NSSize           size;
    void*            pixel_memory;
	CVPixelBufferRef pixel_buffer;
    uint64_t         display_time;
} OsxVideoPicture;

- (void)             dealloc;
- (CVPixelBufferRef) buffer;
- (float)            width;
- (float)            height;
- (uint64_t)         displayTime;
- (void)             setDisplayTime: (uint64_t)time;
- (void)             setSize: (NSSize)new_size;
- (void)             setPixels:(const unsigned char*)pixels 
                        format:(const BLT_RawVideoMediaType*)format;

@end

@interface OsxVideoView : NSOpenGLView
{
    NSLock*                 lock;
	NSRect                  screen_bounds;
	NSRect                  texture_bounds;
	CVOpenGLTextureCacheRef texture_cache;
	CVOpenGLTextureRef      texture;
    OsxVideoPicture*        pictures[BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE];
    unsigned int            picture_out;
    unsigned int            picture_count;
    CVDisplayLinkRef        display_link;
}

- (id)   initWithFrame:(NSRect)frameRect 
         pixelFormat:(NSOpenGLPixelFormat *)format;
- (void) dealloc;
- (void) render;
- (void) clear;
- (void) reshape;
- (void) start;
- (void) stop;
- (void) prepareOpenGL;
- (void) setupDisplayLink;
- (void) processEvents;
- (void) selectNextPicture;
- (BLT_Boolean) queueIsFull;
- (BLT_Result) queuePicture: (const unsigned char*)pixels 
                     format: (const BLT_RawVideoMediaType*)format
                displayTime: (uint64_t)displayTime; 
- (CVReturn) displayLinkCallback: (CVDisplayLinkRef)display_link
                             now: (const CVTimeStamp*)now
                      outputTime: (const CVTimeStamp*)outputTime
                         flagsIn: (CVOptionFlags)flagsIn
                        flagsOut: (CVOptionFlags*)flagsOut;
@end

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
    BLT_MediaType expected_media_type;
    BLT_MediaType media_type;
    
    NSWindow*     window;
    NSView*       hostView;
    OsxVideoView* videoView;
    double        time_offset;
} OsxVideoOutput;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(OsxVideoOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(OsxVideoOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(OsxVideoOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(OsxVideoOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(OsxVideoOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(OsxVideoOutput, BLT_PacketConsumer)

/*----------------------------------------------------------------------
|    OsxVideoPicture::PixelBufferReleaseCallback
+---------------------------------------------------------------------*/
static void 
OsxVideoPicture_PixelBufferReleaseCallback(void* context, const void* base_address)
{
    free((void*)base_address);
}   

/*----------------------------------------------------------------------
|    OsxVideoView::DisplayLinkCallback
+---------------------------------------------------------------------*/
static CVReturn
OsxVideoView_DisplayLinkCallback(CVDisplayLinkRef   display_link,
                                 const CVTimeStamp* in_now,
                                 const CVTimeStamp* in_output_time,
                                 CVOptionFlags      flags_in,
                                 CVOptionFlags*     flags_out,
                                 void*              context)
{
    OsxVideoView* self = (OsxVideoView*)context;
    if (self) return [self displayLinkCallback: display_link
                                           now: in_now
                                    outputTime: in_output_time
                                       flagsIn: flags_in
                                      flagsOut: flags_out];
    return 0;
}   

@implementation OsxVideoPicture

/*----------------------------------------------------------------------
|    OsxVideoPicture::dealloc
+---------------------------------------------------------------------*/
- (void)dealloc
{
    if (pixel_buffer) CVPixelBufferRelease(pixel_buffer);
    [super dealloc];
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::buffer
+---------------------------------------------------------------------*/
- (CVPixelBufferRef) buffer
{
    return pixel_buffer;
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::width
+---------------------------------------------------------------------*/
- (float) width
{
    return size.width;
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::height
+---------------------------------------------------------------------*/
- (float) height
{
    return size.height;
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::displayTime
+---------------------------------------------------------------------*/
- (uint64_t) displayTime
{
    return display_time;
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::setDisplayTime
+---------------------------------------------------------------------*/
- (void) setDisplayTime: (uint64_t)displayTime
{
    display_time = displayTime;
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::setSize
+---------------------------------------------------------------------*/
- (void)setSize: (NSSize)new_size
{
    // shortcut if this is the same size as the current one
    if (NSEqualSizes(size, new_size)) return;
    
    // first, reset the existing buffers
    if (pixel_buffer) {
        CVPixelBufferRelease(pixel_buffer);
        pixel_buffer = NULL;
        pixel_memory = NULL;
    }
    size = NSMakeSize(0,0);
    
    // allocate new buffers
	pixel_memory = malloc(new_size.width*new_size.height*2);
	
	CVReturn err;
	err = CVPixelBufferCreateWithBytes(NULL,             // alloctor
                                       new_size.width,   // width
                                       new_size.height,  // height
                                       kYUVSPixelFormat, // pixel format
                                       pixel_memory,     // base address
                                       new_size.width*2, // bytes per row
                                       OsxVideoPicture_PixelBufferReleaseCallback, // release callback
                                       NULL,             // release callback context
                                       NULL,             // pixel buffer attributes
                                       &pixel_buffer);
	if (err != kCVReturnSuccess) {
        ATX_LOG_WARNING_1("CVPixelBufferCreateWithBytes failed (%d)", err);
        return;
    }
	
    // update the size
    size = new_size;
}

/*----------------------------------------------------------------------
|    OsxVideoPicture::setPixels
+---------------------------------------------------------------------*/
- (void) setPixels: (const unsigned char*)pixels format: (const BLT_RawVideoMediaType*)format
{
    // set the picture size
    [self setSize: NSMakeSize(format->width, format->height)];
    
    // setup pixel pointers
    unsigned char*       dst = pixel_memory;
    const unsigned char* src_y;
    const unsigned char* src_u;
    const unsigned char* src_v;
    unsigned int         row, col;
            
    // get the addr of the planes
    src_y = pixels+format->planes[0].offset;
    src_u = pixels+format->planes[1].offset;
    src_v = pixels+format->planes[2].offset;
    for (row=0; row<format->height; row++) {
        for (col=0; col<format->width/2; col++) {
            *dst++ = src_y[2*col];
            *dst++ = src_u[col];
            *dst++ = src_y[2*col+1];
            *dst++ = src_v[col];
        }
        src_y += format->planes[0].bytes_per_line;
        if (row&1) {
            src_u += format->planes[1].bytes_per_line;
            src_v += format->planes[2].bytes_per_line;
        }
    }
}

@end

@implementation OsxVideoView

/*----------------------------------------------------------------------
|    OsxVideoView::initWithFrame:pixelFormat
+---------------------------------------------------------------------*/
- (id) initWithFrame:(NSRect)frameRect pixelFormat:(NSOpenGLPixelFormat *)format
{
    lock = [[NSLock alloc] init];
    self = [super initWithFrame: frameRect pixelFormat: format];
    [self setupDisplayLink];
    return self;
}

/*----------------------------------------------------------------------
|    OsxVideoView::dealloc
+---------------------------------------------------------------------*/
- (void)dealloc
{
    // stop the display link
    [self stop];

    // free the GL objects
    if (texture) CVOpenGLTextureRelease(texture);
    if (texture_cache) CVOpenGLTextureCacheRelease(texture_cache);
    if (display_link) CVDisplayLinkRelease(display_link);

    // free all pictures
    unsigned int i;
    for (i=0; i<BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE; i++) {
        if (pictures[i]) [pictures[i] dealloc];
    }
    
    [lock release];
    [super dealloc];
}

/*----------------------------------------------------------------------
|    OsxVideoView::clear
+---------------------------------------------------------------------*/
- (void) clear
{
	glClearColor(0,0,0,0);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();
	[[self openGLContext] flushBuffer];
}

/*----------------------------------------------------------------------
|    OsxVideoView::reshape
+---------------------------------------------------------------------*/
- (void)reshape
{
    NSRect frame = [self bounds];
	
    [lock lock];
    
	// setup the frame
	glViewport(0, 0, frame.size.width, frame.size.height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, frame.size.width, frame.size.height, 0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

    [lock unlock];
}

/*----------------------------------------------------------------------
|    OsxVideoView::render
+---------------------------------------------------------------------*/
- (void) render
{
    // serialize access
    [lock lock];
    
    // make the GL context the current context
    [[self openGLContext] makeCurrentContext];

	if (texture == NULL) {
        [self clear];
        [lock unlock];
        return;
    }

	glClear(GL_COLOR_BUFFER_BIT);

    // get the texture bounds
    GLfloat	bottom_left[2]; 
    GLfloat bottom_right[2]; 
    GLfloat top_left[2];
    GLfloat top_right[2];
    CVOpenGLTextureGetCleanTexCoords(texture, bottom_left, bottom_right, top_right, top_left);	

    // compute the aspect ratio and the positioning of the image in the view
    NSRect view_bounds = [self bounds];
    if (view_bounds.size.height < BLT_MACOSX_VIDEO_MIN_VIEW_HEIGHT) goto end;
    if (view_bounds.size.height < BLT_MACOSX_VIDEO_MIN_VIEW_WIDTH) goto end;
    if (view_bounds.size.width  < 0.0) goto end;
    GLfloat view_aspect_ratio = view_bounds.size.width/view_bounds.size.height;
    GLfloat texture_width  = bottom_right[0]-bottom_left[0];
    GLfloat texture_height = bottom_left[1]-top_left[1];
    if (texture_width  < BLT_MACOSX_VIDEO_MIN_PICTURE_WIDTH) goto end;
    if (texture_height < BLT_MACOSX_VIDEO_MIN_PICTURE_HEIGHT) goto end;
    GLfloat texture_aspect_ratio = texture_width/texture_height;
    NSRect picture_bounds;
    if (view_aspect_ratio > texture_aspect_ratio) {
        // we need space on the left and right sides of the picture
        picture_bounds.size.height = view_bounds.size.height;
        picture_bounds.size.width  = view_bounds.size.height*texture_aspect_ratio;
        picture_bounds.origin.x    = (view_bounds.size.width-picture_bounds.size.width)/2;
        picture_bounds.origin.y    = 0;
    } else {
        // we need space on the top and bottom sides of the picture
        picture_bounds.size.height = view_bounds.size.width/texture_aspect_ratio;
        picture_bounds.size.width  = view_bounds.size.width;
        picture_bounds.origin.x    = 0;
        picture_bounds.origin.y    = (view_bounds.size.height-picture_bounds.size.height)/2;
    }
    
    // display the texture
    GLenum target = CVOpenGLTextureGetTarget(texture);
    GLint name = CVOpenGLTextureGetName(texture);
    glEnable(target);
    glBindTexture(target, name);
    glColor3f(1,1,1);
    glBegin(GL_QUADS);
        glTexCoord2fv(top_left);     glVertex2i(picture_bounds.origin.x,                           picture_bounds.origin.y);
        glTexCoord2fv(bottom_left);  glVertex2i(picture_bounds.origin.x,                           picture_bounds.origin.y+picture_bounds.size.height);
        glTexCoord2fv(bottom_right); glVertex2i(picture_bounds.origin.x+picture_bounds.size.width, picture_bounds.origin.y+picture_bounds.size.height);
        glTexCoord2fv(top_right);    glVertex2i(picture_bounds.origin.x+picture_bounds.size.width, picture_bounds.origin.y);
    glEnd();
    glDisable(target);

end:
    // we're done with GL
    glFlush();
    [lock unlock];
}

/*----------------------------------------------------------------------
|    OsxVideoView::drawRect
+---------------------------------------------------------------------*/
- (void) drawRect: (NSRect *) bounds
{
	[self render];
}
    
/*----------------------------------------------------------------------
|    OsxVideoView::setupDisplayLink
+---------------------------------------------------------------------*/
- (void)setupDisplayLink
{
    CVReturn err;
    
    // create the display link if does not already exist
    CGDirectDisplayID display_id;
    if (display_link == NULL) {
        display_id = CGMainDisplayID();
    
        err = CVDisplayLinkCreateWithCGDisplay(display_id, &display_link);
        if (err) {
            ATX_LOG_WARNING_1("CVDisplayLinkCreateWithCGDisplay failed (%d)", err);
            return;
        }
        err = CVDisplayLinkSetOutputCallback(display_link, OsxVideoView_DisplayLinkCallback, self);
        if (err) {
            ATX_LOG_WARNING_1("CVDisplayLinkSetOutputCallback failed (%d)", err);
            return;
        }    
    }

    // set the right parameters for the display link
    //err = CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(display_link,                            // display link
    //                                                        [[self openGLContext] CGLContextObj],    // context
    //                                                        [[self pixelFormat] CGLPixelFormatObj]); // pixel format
    //if (err) {
    //    ATX_LOG_WARNING_1("OsxVideoView::setupDisplayLink - CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext failed (%d)", err);
    //}    
    
#if defined(ATX_CONFIG_ENABLE_LOGGING)
    // show information about the updated configuration
    CVTime latency = CVDisplayLinkGetOutputVideoLatency(display_link);
    float latency_f = 0.0f;
    if (latency.timeScale != 0 && !(latency.flags & kCVTimeIsIndefinite)) {
        latency_f = (float)latency.timeValue/(float)latency.timeScale;
    }
    CVTime nominal_refresh_period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(display_link);
    float nominal_refresh_fps = 0.0f;
    if (!(nominal_refresh_period.flags & kCVTimeIsIndefinite) && nominal_refresh_period.timeValue != 0) {
        nominal_refresh_fps = (float)nominal_refresh_period.timeScale/(float)nominal_refresh_period.timeValue;
    }
    CVTime actual_refresh_period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(display_link);
    float actual_refresh_fps = 0.0f;
    if ((actual_refresh_period.flags & kCVTimeIsIndefinite) == 0 && actual_refresh_period.timeValue) {
        actual_refresh_fps = (float)actual_refresh_period.timeScale/(float)actual_refresh_period.timeValue;
    }
    display_id = CVDisplayLinkGetCurrentCGDisplay(display_link);
    uint32_t display_unit = CGDisplayUnitNumber(display_id);
    ATX_LOG_FINE_4("display=%d, latency=%f, nominal_refresh=%f, actual_refresh=%f",
                   display_unit, latency_f, nominal_refresh_fps, actual_refresh_fps);
#endif
}

/*----------------------------------------------------------------------
|    OsxVideoView::prepareOpenGL
+---------------------------------------------------------------------*/
- (void) prepareOpenGL
{
	// setup GL params
	glEnable(GL_BLEND); 
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);

   	// set up the GL contexts swap interval -- passing 1 means that
    // the buffers are swapped only during the vertical retrace of the monitor
	long swap_interval = 1;
	[[self openGLContext] setValues:&swap_interval forParameter:NSOpenGLCPSwapInterval];

    // create a texture cache
    CVReturn err;
	err = CVOpenGLTextureCacheCreate(NULL,                                   // allocator
                                     NULL,                                   // cache attributes
                                     [[self openGLContext] CGLContextObj],   // context
                                     [[self pixelFormat] CGLPixelFormatObj], // pixel format
                                     NULL,                                   // texture attributes
                                     &texture_cache);
	if (err != kCVReturnSuccess) {
        ATX_LOG_WARNING_1("CVOpenGLTextureCacheCreate failed (%d)", err);
        return;
	}
    
	[self reshape];
}

/*----------------------------------------------------------------------
|    OsxVideoView::processEvents
+---------------------------------------------------------------------*/
- (void) processEvents
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
	NSEvent* event = [NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0001] inMode:NSEventTrackingRunLoopMode dequeue:YES];
	if (event) [NSApp sendEvent:event];
    [pool release];
}

/*----------------------------------------------------------------------
|    OsxVideoView::queuePicture
+---------------------------------------------------------------------*/
- (BLT_Result) queuePicture: (const unsigned char*)pixels 
                     format: (const BLT_RawVideoMediaType*)format
                displayTime: (uint64_t)displayTime
{
    // wait for some space in the queue
    [lock lock];
    while (picture_count == BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE) {
        [lock unlock];
        ATX_LOG_FINEST("waiting for space in picture queue");
        ATX_TimeInterval sleep_duration = {0,10000000}; /* 10 ms */
        ATX_System_Sleep(&sleep_duration);
        [lock lock];
    }
    
    unsigned int picture_in = (picture_out+picture_count)%BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE;
    if (pictures[picture_in] == NULL) {
        pictures[picture_in] = [OsxVideoPicture alloc];
    }
    OsxVideoPicture* picture = pictures[picture_in];
    [picture setPixels:pixels format:format];
    [picture setDisplayTime: displayTime];
    ++picture_count;
    
    ATX_LOG_FINEST_3("picture queued at %d (%d in queue, next out=%d)", picture_in, picture_count, picture_out);

    [lock unlock];
     
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxVideoView::queueIsFull
+---------------------------------------------------------------------*/
- (BLT_Boolean) queueIsFull
{
    BLT_Boolean queue_is_full = BLT_FALSE;
    [lock lock];
    if (picture_count == BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE) {
        queue_is_full = BLT_TRUE;
    }
    [lock unlock];
    
    return queue_is_full;
}

/*----------------------------------------------------------------------
|    OsxVideoView::start
+---------------------------------------------------------------------*/
- (void) start
{
    if (display_link) CVDisplayLinkStart(display_link);
}

/*----------------------------------------------------------------------
|    OsxVideoView::stop
+---------------------------------------------------------------------*/
- (void) stop
{
    if (display_link) CVDisplayLinkStop(display_link);
}

/*----------------------------------------------------------------------
|    OsxVideoView::selectNextPicture
+---------------------------------------------------------------------*/
- (void) selectNextPicture
{   
    // do nothing if we don't have a picture
    if (picture_count == 0) return;
    
    // release the previous texture
    if (texture) {
        CVOpenGLTextureRelease(texture);
        texture = NULL;
    }
    
    // let the GL texture cache do some housekeeping
    CVOpenGLTextureCacheFlush(texture_cache, 0);

    // move to the next picture
    OsxVideoPicture* picture = pictures[picture_out];
    picture_out = (picture_out+1)%BLT_MACOSX_VIDEO_PICTURE_QUEUE_SIZE;
    --picture_count;
    if (picture == NULL || [picture buffer] == NULL) return;
    
    {
        static uint64_t previous_pts = 0;
        uint64_t delta = [picture displayTime]-previous_pts;
        ATX_LOG_FINEST_1("picture pts delta=%lld", delta);
        previous_pts = [picture displayTime];
    }
    ATX_LOG_FINEST_2("selecting next picture pts=%lld (%d still in queue)", [picture displayTime], picture_count);
    
    // create a new GL texture from the pixels
    CVReturn err;
	err = CVOpenGLTextureCacheCreateTextureFromImage(NULL,             // allocator
                                                     texture_cache,    // texture cache
                                                     [picture buffer], // image
                                                     NULL,             // attributes
                                                     &texture);
	if (err != kCVReturnSuccess) {
        ATX_LOG_WARNING_1("CVOpenGLTextureCacheCreateTextureFromImage failed (%d)", err);
        texture = NULL;
        return;
    }
    
    // set the texture bounds
    texture_bounds = NSMakeRect(0, 0, [picture width], [picture height]);
}

/*----------------------------------------------------------------------
|    OsxVideoView::displayLinkCallback
+---------------------------------------------------------------------*/
- (CVReturn) displayLinkCallback: (CVDisplayLinkRef)this_display_link
                             now: (const CVTimeStamp*)now
                      outputTime: (const CVTimeStamp*)output_time
                         flagsIn: (CVOptionFlags)flags_in
                        flagsOut: (CVOptionFlags*)flags_out
{
    // make sure we have the target timestamp in host time units
    CVTimeStamp target_time = *output_time;
    if (!(target_time.flags &  kCVTimeStampHostTimeValid)) {
        target_time.version = 0;
        target_time.flags |= kCVTimeStampHostTimeValid;
        CVDisplayLinkTranslateTime(this_display_link, output_time, &target_time);
    }
    ATX_LOG_FINEST_1("output time = %lld", output_time->hostTime);
    
    [lock lock];
    BLT_Boolean need_to_render = BLT_FALSE;
    if (picture_count) {
        OsxVideoPicture* picture = pictures[picture_out];
        if (picture) {
            if ([picture displayTime] <= output_time->hostTime) {
                // select the next picture for display
                [self selectNextPicture];
                need_to_render = BLT_TRUE;
            }
        }
    }
    [lock unlock];

    if (need_to_render) [self render];
    
    return 0;
}

@end

/*----------------------------------------------------------------------
|    OsxVideoOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutput_PutPacket(BLT_PacketConsumer* _self,
                         BLT_MediaPacket*    packet)
{
    OsxVideoOutput*              self = ATX_SELF(OsxVideoOutput, BLT_PacketConsumer);
    const unsigned char*         pixel_data = (const unsigned char*)BLT_MediaPacket_GetPayloadBuffer(packet);
    const BLT_RawVideoMediaType* media_type;
        
    // check the media type
    BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_VIDEO_RAW) return BLT_ERROR_INVALID_MEDIA_FORMAT;
    if (media_type->format != BLT_PIXEL_FORMAT_YV12) return BLT_ERROR_INVALID_MEDIA_FORMAT;
    
    // queue the picture
    if (self->videoView) {    
        // FIXME: this is temporary
        [self->videoView processEvents];
        
        // FIXME: this is not the final way to do it
        BLT_TimeStamp ts = BLT_MediaPacket_GetTimeStamp(packet);
        double tsd = (double)(ts.seconds)+(double)(ts.nanoseconds)/1000000000.0;
        tsd /= 10;
        double htd = (double)AudioConvertHostTimeToNanos(CVGetCurrentHostTime())/1000000000.0;
        double delta = tsd+self->time_offset-htd;
        if (tsd == 0.0 || delta > 2.0 || delta < -2.0) {
            self->time_offset = htd-tsd;
        }
        uint64_t display_time = (uint64_t)((tsd+self->time_offset)*1000000000.0);    
        return [self->videoView queuePicture: pixel_data format: media_type displayTime: display_time];
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxVideoOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutput_QueryMediaType(BLT_MediaPort*        _self,
                              BLT_Ordinal           index,
                              const BLT_MediaType** media_type)
{
    OsxVideoOutput* self = ATX_SELF(OsxVideoOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|    OsxVideoOutput_CreateView
+---------------------------------------------------------------------*/
static BLT_Result
OsxVideoOutput_CreateView(OsxVideoOutput* self)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    /* in case we're called from a non-cocoa app, this will set things up */
    NSApplicationLoad();
     
    /* read the properties to see if a host view has been set */
    {
        ATX_Properties* properties;
        ATX_Result      result;
        if (BLT_SUCCEEDED(BLT_Core_GetProperties(ATX_BASE(self, BLT_BaseMediaNode).core, &properties))) {
            ATX_PropertyValue property;
            result = ATX_Properties_GetProperty(properties, "Output.Video.HostView", &property);
            if (ATX_SUCCEEDED(result) && property.type == ATX_PROPERTY_VALUE_TYPE_POINTER) {
                self->hostView = (NSView*)property.data.pointer;
            }
        }
    }
    
    /* use the host view's bounds or choose a default */
    NSRect frame;
    if (self->hostView) {
        frame = [self->hostView bounds];
    } else {
        frame = NSMakeRect(0,0,640,400); // FIXME: default value
    }
     
    /* create the video view */
    self->videoView = [[OsxVideoView alloc] initWithFrame: frame
                                                 pixelFormat: [NSOpenGLView defaultPixelFormat]];
    if (self->videoView == NULL) {
        [pool release];
        return BLT_FAILURE;
    }

    /* attach the video view */
    if (self->hostView) {
        [self->hostView addSubview: self->videoView];
    } else {
        // create a standlone window
        self->window = [[NSWindow alloc] initWithContentRect: frame 
                                                   styleMask: NSTitledWindowMask             | 
                                                              NSTexturedBackgroundWindowMask |
                                                              NSClosableWindowMask           |
                                                              NSMiniaturizableWindowMask     |
                                                              NSResizableWindowMask
                                                     backing: NSBackingStoreBuffered 
                                                       defer: NO];
        if (self->window == NULL) {
            [self->videoView release];
            [pool release];
            return BLT_FAILURE;
        }
        
        [self->window setContentView: self->videoView];
        //[window setDelegate: self->view];
        //[window setInitialFirstResponder: self->view];
        [self->window setAcceptsMouseMovedEvents:YES];
        [self->window setTitle:@"Wasabi Media Player"];
        [self->window setLevel:NSNormalWindowLevel];
        [self->window orderFront: nil];
    }
    
    [pool release];

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxVideoOutput_DestroyView
+---------------------------------------------------------------------*/
static BLT_Result
OsxVideoOutput_DestroyView(OsxVideoOutput* self)
{
    // use a local pool here so that the view can get released right
    // away (the window will autorelease the view)
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    
    if (self->window) {
        [self->window setContentView: nil];
        [self->window close];
        [self->videoView release];
    } else {
        [self->videoView removeFromSuperview];
    }
        
    [pool release];
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxVideoOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
OsxVideoOutput_Create(BLT_Module*              module,
                      BLT_Core*                core, 
                      BLT_ModuleParametersType parameters_type,
                      BLT_CString              parameters, 
                      BLT_MediaNode**          object)
{
    OsxVideoOutput*        self;
    
    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(OsxVideoOutput));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* construct the object */

    /* setup the expected media type */
    BLT_MediaType_Init(&self->expected_media_type, BLT_MEDIA_TYPE_ID_VIDEO_RAW);
    
    /* create a view to display the video */
    OsxVideoOutput_CreateView(self);
    
    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, OsxVideoOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, OsxVideoOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (self, OsxVideoOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE   (self, OsxVideoOutput, BLT_OutputNode);
    ATX_SET_INTERFACE   (self, OsxVideoOutput, BLT_MediaPort);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxVideoOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
OsxVideoOutput_Destroy(OsxVideoOutput* self)
{
    /* close the window if we have one */
    OsxVideoOutput_DestroyView(self);
    
    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|   OsxVideoOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutput_GetPortByName(BLT_MediaNode*  _self,
                             BLT_CString     name,
                             BLT_MediaPort** port)
{
    OsxVideoOutput* self = ATX_SELF_EX(OsxVideoOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|   OsxVideoOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutput_Start(BLT_MediaNode*  _self)
{
    OsxVideoOutput* self = ATX_SELF_EX(OsxVideoOutput, BLT_BaseMediaNode, BLT_MediaNode);
    [self->videoView start];
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   OsxVideoOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutput_Stop(BLT_MediaNode*  _self)
{
    OsxVideoOutput* self = ATX_SELF_EX(OsxVideoOutput, BLT_BaseMediaNode, BLT_MediaNode);
    [self->videoView stop];
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    OsxVideoOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutput_GetStatus(BLT_OutputNode*       _self,
                         BLT_OutputNodeStatus* status)
{
    OsxVideoOutput* self = ATX_SELF(OsxVideoOutput, BLT_OutputNode);
    BLT_COMPILER_UNUSED(self);
    
    /* default value */
    status->media_time.seconds     = 0;
    status->media_time.nanoseconds = 0;

    status->flags = 0;
    if ([self->videoView queueIsFull]) {
        status->flags |= BLT_OUTPUT_NODE_STATUS_QUEUE_FULL;
    }
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxVideoOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxVideoOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxVideoOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (OsxVideoOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT   (OsxVideoOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT   (OsxVideoOutput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(OsxVideoOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(OsxVideoOutput, BLT_MediaPort)
    OsxVideoOutput_GetName,
    OsxVideoOutput_GetProtocol,
    OsxVideoOutput_GetDirection,
    OsxVideoOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxVideoOutput, BLT_PacketConsumer)
    OsxVideoOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(OsxVideoOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    OsxVideoOutput_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    OsxVideoOutput_Start,
    OsxVideoOutput_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(OsxVideoOutput, BLT_OutputNode)
    OsxVideoOutput_GetStatus
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxVideoOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   OsxVideoOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
OsxVideoOutputModule_Probe(BLT_Module*              self, 
                              BLT_Core*                core,
                              BLT_ModuleParametersType parameters_type,
                              BLT_AnyConst             parameters,
                              BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR: {
        BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

        /* the input protocol should be PACKET and the */
        /* output protocol should be NONE              */
        if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
             constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
            (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
             constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_NONE)) {
            return BLT_FAILURE;
        }

        /* the input type should be unknown, or video/raw */
        if (!(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_VIDEO_RAW) &&
            !(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
            return BLT_FAILURE;
        }

        /* the name should be 'macosxv:<n>' */
        if (constructor->name == NULL || 
            !ATX_StringsEqualN(constructor->name, "osxv:", 5)) {
            return BLT_FAILURE;
        }

        /* always an exact match, since we only respond to our name */
        *match = BLT_MODULE_PROBE_MATCH_EXACT;

        return BLT_SUCCESS;
      }    
      break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(OsxVideoOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxVideoOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(OsxVideoOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(OsxVideoOutputModule, OsxVideoOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(OsxVideoOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    OsxVideoOutputModule_CreateInstance,
    OsxVideoOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define OsxVideoOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(OsxVideoOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_OsxVideoOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("OSX Video Output", NULL, 0, 
                                 &OsxVideoOutputModule_BLT_ModuleInterface,
                                 &OsxVideoOutputModule_ATX_ReferenceableInterface,
                                 object);
}
