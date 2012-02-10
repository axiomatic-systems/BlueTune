/*****************************************************************
|
|   BlueTune - Network Stream
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltNetworkStream.h"
#include "BltErrors.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
struct BLT_NetworkStream {
    /* interfaces */
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_Properties);
    ATX_IMPLEMENTS(ATX_Referenceable);

    /* members */
    ATX_Cardinal     reference_count;
    BLT_Stream*      context;
    ATX_InputStream* source;
    ATX_Properties*  source_properties;
    ATX_LargeSize    source_size;
    ATX_RingBuffer*  buffer;
    ATX_Size         buffer_size;
    ATX_Position     position;
    ATX_Boolean      eos;
    ATX_Result       eos_cause;
    ATX_Size         seek_as_read_threshold;
    ATX_Size         min_buffer_fullness;
    ATX_Int64        last_notification;
};

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.network.stream")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
/**
 * Threshold below which a seek forward is implemented by reading from
 * the source instead of seeking in the source.
 */
#define BLT_NETWORK_STREAM_DEFAULT_SEEK_AS_READ_THRESHOLD 0     /* when seek is normal */ 
#define BLT_NETWORK_STREAM_SLOW_SEEK_AS_READ_THRESHOLD    32768 /* when seek is slow   */

#define BLT_NETWORK_STREAM_NOTIFICATION_INTERVAL          1000000000 /* 1 second */

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BLT_NetworkStream, ATX_InputStream)
ATX_DECLARE_INTERFACE_MAP(BLT_NetworkStream, ATX_Properties)
ATX_DECLARE_INTERFACE_MAP(BLT_NetworkStream, ATX_Referenceable)

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Create
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkStream_Create(BLT_Size            buffer_size,
                         BLT_Size            min_buffer_fullness,
                         ATX_InputStream*    source, 
                         BLT_NetworkStream** stream)
{
    ATX_Result result;

    /* allocate the object */
    *stream = (BLT_NetworkStream*)ATX_AllocateZeroMemory(sizeof(BLT_NetworkStream));
    if (*stream == NULL) {
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct the object */
    (*stream)->reference_count = 1;
    result = ATX_RingBuffer_Create(buffer_size, &(*stream)->buffer);
    if (ATX_FAILED(result)) {
        *stream = NULL;
        return result;
    }
    (*stream)->buffer_size = buffer_size;
    (*stream)->min_buffer_fullness = min_buffer_fullness;
    (*stream)->eos_cause = ATX_ERROR_EOS;
    (*stream)->source = source;
    ATX_REFERENCE_OBJECT(source);
    ATX_InputStream_GetSize(source, &(*stream)->source_size);
    
    /* get the properties interface of the source */
    (*stream)->source_properties = ATX_CAST(source, ATX_Properties);
    
    /* determine when we should read data instead issuing a seek when */
    /* the target position is close enough                            */
    (*stream)->seek_as_read_threshold = BLT_NETWORK_STREAM_SLOW_SEEK_AS_READ_THRESHOLD;
    
    /* setup the interfaces */
    ATX_SET_INTERFACE((*stream), BLT_NetworkStream, ATX_InputStream);
    ATX_SET_INTERFACE((*stream), BLT_NetworkStream, ATX_Properties);
    ATX_SET_INTERFACE((*stream), BLT_NetworkStream, ATX_Referenceable);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
BLT_NetworkStream_Destroy(BLT_NetworkStream* self)
{
    ATX_RELEASE_OBJECT(self->source);
    ATX_RingBuffer_Destroy(self->buffer);
    ATX_FreeMemory(self);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_InputStream_AddReference
+---------------------------------------------------------------------*/
ATX_METHOD 
BLT_NetworkStream_InputStream_AddReference(ATX_Referenceable* _self)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_Referenceable);
    self->reference_count++;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_InputStream_Release
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_NetworkStream_InputStream_Release(ATX_Referenceable* _self)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_Referenceable);
    return BLT_NetworkStream_Release(self);
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Release
+---------------------------------------------------------------------*/
BLT_Result
BLT_NetworkStream_Release(BLT_NetworkStream* self)
{
    if (--self->reference_count == 0) {
        BLT_NetworkStream_Destroy(self);
    }
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_SetContext
+---------------------------------------------------------------------*/
void
BLT_NetworkStream_SetContext(BLT_NetworkStream* self, BLT_Stream* context)
{
    self->context = context;

    if (self->context) {
        ATX_Properties* properties = NULL;
        BLT_Stream_GetProperties(self->context, &properties);
        if (properties) {
            ATX_PropertyValue value;
            
            value.type = ATX_PROPERTY_VALUE_TYPE_INTEGER;
            value.data.integer = self->buffer_size;
            ATX_Properties_SetProperty(properties, BLT_NETWORK_STREAM_BUFFER_SIZE_PROPERTY, &value);
            
            value.type = ATX_PROPERTY_VALUE_TYPE_INTEGER;
            value.data.integer = 0;
            ATX_Properties_SetProperty(properties, BLT_NETWORK_STREAM_BUFFER_EOS_PROPERTY, &value);            
        }
    }
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_GetInputStream
+---------------------------------------------------------------------*/
ATX_InputStream*
BLT_NetworkStream_GetInputStream(BLT_NetworkStream* self)
{
    return &ATX_BASE(self, ATX_InputStream);
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_GetProperty
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_NetworkStream_GetProperty(ATX_Properties*    _self, 
                              const char*        name,
                              ATX_PropertyValue* value)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_Properties);
    
    if (self->source_properties && name != NULL &&
        ATX_StringsEqual(name, ATX_INPUT_STREAM_PROPERTY_SEEK_SPEED)) {        
        /* ask the source if it has the seek speed property */
        if (ATX_FAILED(ATX_Properties_GetProperty(self->source_properties, name, value))) {
            /* the source does not have the property, use a default value */
            value->type = ATX_PROPERTY_VALUE_TYPE_INTEGER;
            value->data.integer = ATX_INPUT_STREAM_SEEK_SPEED_SLOW;
        }
        return ATX_SUCCESS;
    } else {
        return ATX_ERROR_NO_SUCH_PROPERTY;
    }
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_FillBuffer
+---------------------------------------------------------------------*/
static void
BLT_NetworkStream_FillBuffer(BLT_NetworkStream* self)
{
    ATX_Size       read_from_source = 0;
    ATX_Size       should_read = ATX_RingBuffer_GetContiguousSpace(self->buffer);
    unsigned char* in = ATX_RingBuffer_GetIn(self->buffer);
    ATX_Result     result = ATX_SUCCESS;

    /* read from the source */
    ATX_LOG_FINER_1("reading up to %d bytes", should_read);
    result = ATX_InputStream_Read(self->source, 
                                  in, 
                                  should_read, 
                                  &read_from_source);
    if (ATX_SUCCEEDED(result)) {
        ATX_LOG_FINER_2("read %d bytes of %d from source", read_from_source, should_read);
        
        /* adjust the ring buffer */
        ATX_RingBuffer_MoveIn(self->buffer, read_from_source);
        
        /* check if we've read everything there is to read */
        if (self->source_size && 
            self->position+ATX_RingBuffer_GetAvailable(self->buffer) >= self->source_size) {
            self->eos = ATX_TRUE;
            self->eos_cause = ATX_ERROR_EOS;
        }
    } else {
        ATX_LOG_FINE_2("read from source failed: %d (%S)", result, BLT_ResultText(result));
        self->eos = ATX_TRUE;
        self->eos_cause = result;
    }
    
    /* notify that we've reached the end of stream */
    if (self->eos && self->context) {
        ATX_Properties* properties = NULL;
        BLT_Stream_GetProperties(self->context, &properties);
        if (properties) {
            ATX_PropertyValue value;
            value.type = ATX_PROPERTY_VALUE_TYPE_INTEGER;
            value.data.integer = self->eos_cause;
            ATX_Properties_SetProperty(properties, BLT_NETWORK_STREAM_BUFFER_EOS_PROPERTY, &value);
        }
    }        
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Read
+---------------------------------------------------------------------*/
static ATX_Result
BLT_NetworkStream_Read(ATX_InputStream* _self, 
                       void*            buffer,
                       ATX_Size         bytes_to_read,
                       ATX_Size*        bytes_read)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    ATX_Size           buffered;
    ATX_Size           chunk;
    ATX_Size           bytes_read_storage = 0;
    ATX_LargeSize      source_available = 0;
    
    /* default */
    if (bytes_read) {
        *bytes_read = 0;
    } else {
        bytes_read = &bytes_read_storage;
    }

    /* shortcut */
    if (bytes_to_read == 0) return ATX_SUCCESS;

    /* if we have a min buffer fullness, wait until we have refilled the buffer enough */
    if (self->min_buffer_fullness) {
        while ((!self->eos) && (ATX_RingBuffer_GetAvailable(self->buffer) < self->min_buffer_fullness)) {
            ATX_LOG_FINE_2("buffer below minimum fullness (%d of %d), filling buffer",
                           ATX_RingBuffer_GetAvailable(self->buffer), self->min_buffer_fullness);
            BLT_NetworkStream_FillBuffer(self);
        }
    }
        
    /* ask the source how much data is available */
    if (ATX_FAILED(ATX_InputStream_GetAvailable(self->source, &source_available))) {
        source_available = 0;
    }
    
    /* read what we can from the source */
    buffered = ATX_RingBuffer_GetAvailable(self->buffer);
    ATX_LOG_FINER_1("buffer available=%d", buffered);
    if ((buffered == 0 || source_available) && !self->eos) {
        BLT_NetworkStream_FillBuffer(self);
    }
    
    /* use all we can from the buffer */
    buffered = ATX_RingBuffer_GetAvailable(self->buffer);
    chunk = buffered > bytes_to_read ? bytes_to_read : buffered;
    if (chunk) {
        ATX_RingBuffer_Read(self->buffer, buffer, chunk);
        bytes_to_read -= chunk;
        *bytes_read += chunk;
        self->position += chunk;
        buffer = (void*)((char*)buffer+chunk);
    }

    /* notify of the stream status periodically */
    ATX_TimeStamp now;
    if (ATX_SUCCEEDED(ATX_System_GetCurrentTimeStamp(&now))) {
        ATX_Int64 next_update;
        ATX_Int64 now_int;
        ATX_TimeStamp_ToInt64(now, now_int);
        next_update = self->last_notification+BLT_NETWORK_STREAM_NOTIFICATION_INTERVAL;
        if (now_int > next_update) {
            ATX_LOG_FINE("updating buffer status");
            self->last_notification = now_int;
            if (self->context) {
                ATX_Properties* properties = NULL;
                BLT_Stream_GetProperties(self->context, &properties);
                if (properties) {
                    ATX_PropertyValue value;
                    value.type = ATX_PROPERTY_VALUE_TYPE_INTEGER;
                    value.data.integer = ATX_RingBuffer_GetAvailable(self->buffer);
                    ATX_Properties_SetProperty(properties, BLT_NETWORK_STREAM_BUFFER_FULLNESS_PROPERTY, &value);
                }
            }
        }
    }
    
    if (self->eos && *bytes_read == 0) {
        return self->eos_cause;
    } else {
        return ATX_SUCCESS;
    }
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Seek
+---------------------------------------------------------------------*/
static ATX_Result 
BLT_NetworkStream_Seek(ATX_InputStream* _self, ATX_Position position)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    ATX_Int64          move = position-self->position;
    ATX_Result         result;

    /* shortcut */
    if (move == 0) {
        if (position == 0) {
            /* force a call to the source, because some callers will  */
            /* use this to determine if the source is seekable or not */
            ATX_Position current = 0;
            result = ATX_InputStream_Tell(self->source, &current);
            if (ATX_FAILED(result)) return result;
            return ATX_InputStream_Seek(self->source, current);
        }
        return ATX_SUCCESS;
    }
    
    ATX_LOG_FINER_2("move by %ld, buffered=%ld",
                    (long)move, (long)ATX_RingBuffer_GetAvailable(self->buffer));
                  
    /* see if we can seek entirely within our buffer */
    if (move > 0 && move < (ATX_Int64)ATX_RingBuffer_GetAvailable(self->buffer)) {
        ATX_RingBuffer_MoveOut(self->buffer, (ATX_Size)move);
        self->position = position;

        return ATX_SUCCESS;
    }

    /* we're seeking outside the buffered zone */
    self->eos = ATX_FALSE;
    self->eos_cause = ATX_ERROR_EOS;
    if (move > 0 && (unsigned int)move <= self->seek_as_read_threshold) {
        /* simulate a seek by reading data up to the position */
        char buffer[256];
        ATX_LOG_FINE_1("performing seek of %d as a read", move);
        while (move) {
            unsigned int chunk = ((unsigned int)move) > sizeof(buffer)?sizeof(buffer):(unsigned int)move;
            ATX_Size     bytes_read = 0;
            result = BLT_NetworkStream_Read(_self, buffer, chunk, &bytes_read);
            if (ATX_FAILED(result)) return result;
            if (bytes_read == 0) return ATX_ERROR_EOS;
            move -= bytes_read;
        }
    } else {
        /* perform a real seek in the source */
        ATX_LOG_FINE_2("performing seek of %ld as input seek(%ld)", move, (long)position);
        result = ATX_InputStream_Seek(self->source, position);
        if (ATX_FAILED(result)) return result;
        ATX_RingBuffer_Reset(self->buffer);
        self->position = position;
    }

    if (self->context) {
        ATX_Properties* properties = NULL;
        BLT_Stream_GetProperties(self->context, &properties);
        if (properties) {
            ATX_PropertyValue value;
            value.type = ATX_PROPERTY_VALUE_TYPE_INTEGER;
            value.data.integer = 0;
            ATX_Properties_SetProperty(properties, BLT_NETWORK_STREAM_BUFFER_EOS_PROPERTY, &value);
        }
    }        
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Tell
+---------------------------------------------------------------------*/
static ATX_Result 
BLT_NetworkStream_Tell(ATX_InputStream* _self, ATX_Position* offset)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    *offset = self->position;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_GetSize
+---------------------------------------------------------------------*/
static ATX_Result 
BLT_NetworkStream_GetSize(ATX_InputStream* _self, ATX_LargeSize* size)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    return ATX_InputStream_GetSize(self->source, size);
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_GetAvailable
+---------------------------------------------------------------------*/
static ATX_Result 
BLT_NetworkStream_GetAvailable(ATX_InputStream* _self, ATX_LargeSize* available)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    ATX_LargeSize available_from_source = 0;
    ATX_InputStream_GetAvailable(self->source, &available_from_source);
    *available = available_from_source+ATX_RingBuffer_GetAvailable(self->buffer);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLT_NetworkStream)
    ATX_GET_INTERFACE_ACCEPT(BLT_NetworkStream, ATX_InputStream)
    ATX_GET_INTERFACE_ACCEPT(BLT_NetworkStream, ATX_Properties)
    ATX_GET_INTERFACE_ACCEPT(BLT_NetworkStream, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   ATX_InputStream interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLT_NetworkStream, ATX_InputStream)
    BLT_NetworkStream_Read,
    BLT_NetworkStream_Seek,
    BLT_NetworkStream_Tell,
    BLT_NetworkStream_GetSize,
    BLT_NetworkStream_GetAvailable
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|       ATX_Properties interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_STATIC_PROPERTIES_INTERFACE(BLT_NetworkStream)

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLT_NetworkStream, ATX_Referenceable)
    BLT_NetworkStream_InputStream_AddReference,
    BLT_NetworkStream_InputStream_Release
};         

