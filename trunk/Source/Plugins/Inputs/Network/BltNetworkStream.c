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

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_Referenceable);

    /* members */
    ATX_Cardinal     reference_count;
    ATX_InputStream* source;
    ATX_RingBuffer*  buffer;
    ATX_Size         buffer_size;
    ATX_Offset       position;
    ATX_Boolean      eos;
} BLT_NetworkStream;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BLT_NetworkStream, ATX_InputStream)
ATX_DECLARE_INTERFACE_MAP(BLT_NetworkStream, ATX_Referenceable)

/*----------------------------------------------------------------------
|   BLT_NetworkStream_Create
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkStream_Create(BLT_Size          buffer_size,
                         ATX_InputStream*  source, 
                         ATX_InputStream** stream)
{
    ATX_Result result;

    /* allocate the object */
    BLT_NetworkStream* self = (BLT_NetworkStream*)ATX_AllocateZeroMemory(sizeof(BLT_NetworkStream));
    if (self == NULL) {
        *stream = NULL;
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct the object */
    self->reference_count = 1;
    result = ATX_RingBuffer_Create(buffer_size, &self->buffer);
    if (ATX_FAILED(result)) {
        *stream = NULL;
        return ATX_ERROR_OUT_OF_MEMORY;
    }
    self->buffer_size = buffer_size;
    self->source = source;
    ATX_REFERENCE_OBJECT(source);

    /* setup the interfaces */
    ATX_SET_INTERFACE(self, BLT_NetworkStream, ATX_InputStream);
    ATX_SET_INTERFACE(self, BLT_NetworkStream, ATX_Referenceable);
    *stream = &ATX_BASE(self, ATX_InputStream);

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
|   BLT_NetworkStream_Destroy
+---------------------------------------------------------------------*/
static ATX_Result
BLT_NetworkStream_Read(ATX_InputStream* _self, 
                       ATX_Any          buffer,
                       ATX_Size         bytes_to_read,
                       ATX_Size*        bytes_read)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    ATX_Size           buffered = ATX_RingBuffer_GetAvailable(self->buffer);
    ATX_Size           total_read = 0;

    /* default */
    if (bytes_read)  *bytes_read = 0;

    /* shortcut */
    if (bytes_to_read == 0) return ATX_SUCCESS;

    /* use all we can from the buffer */
    if (buffered > bytes_to_read) buffered = bytes_to_read;
    if (buffered) {
        ATX_RingBuffer_Read(self->buffer, buffer, buffered);
        total_read += buffered;
        bytes_to_read -= buffered;
        if (bytes_read) *bytes_read += buffered;
        self->position += buffered;
        buffer = (ATX_Any)((unsigned char*)buffer+buffered);
    }

    /* read what we can from the source */
    while (bytes_to_read && !self->eos) {
        ATX_Size       read_from_source = 0;
        ATX_Size       can_read = ATX_RingBuffer_GetContiguousSpace(self->buffer);
        unsigned char* in = ATX_RingBuffer_GetIn(self->buffer);
        ATX_Result     result;

        /* read from the source */
        result = ATX_InputStream_Read(self->source, 
                                      in, 
                                      can_read, 
                                      &read_from_source);
        if (ATX_SUCCEEDED(result)) {
            ATX_Size chunk;

            /* adjust the ring buffer */
            ATX_RingBuffer_MoveIn(self->buffer, read_from_source);

            /* transfer some of what was read */
            chunk = (bytes_to_read <= read_from_source)?bytes_to_read:read_from_source;
            ATX_RingBuffer_Read(self->buffer, buffer, chunk);

            /* adjust counters and pointers */
            total_read += chunk;
            bytes_to_read -= chunk;
            if (bytes_read) *bytes_read += chunk;
            self->position += chunk;
            buffer = (ATX_Any)((unsigned char*)buffer+chunk);
        } else if (result == ATX_ERROR_EOS) {
            /* we can't continue further */
            self->eos = ATX_TRUE;
            break;
        } else {
            return (total_read == 0) ? result : ATX_SUCCESS;
        }

        /* don't loop if this was a short read */
        if (read_from_source != can_read) break;
    }

    if (self->eos && total_read == 0) {
        return ATX_ERROR_EOS;
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
    ATX_Size           buffered = ATX_RingBuffer_GetAvailable(self->buffer);
    int                move = position-self->position;
    ATX_Result         result;

    /* see if we can seek entirely within our buffer */
    if (move >= 0 && move <= (int)buffered) {
        ATX_RingBuffer_MoveOut(self->buffer, move);
        self->position = position;
        self->eos = ATX_FALSE;
        return ATX_SUCCESS;
    }

    /* we're seeking outside the buffered zone */
    result = ATX_InputStream_Seek(self->source, position);
    if (ATX_FAILED(result)) return result;
    ATX_RingBuffer_Reset(self->buffer);
    self->position = position;
    
    self->eos = ATX_FALSE;
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
BLT_NetworkStream_GetSize(ATX_InputStream* _self, ATX_Size* size)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    return ATX_InputStream_GetSize(self->source, size);
}

/*----------------------------------------------------------------------
|   BLT_NetworkStream_GetAvailable
+---------------------------------------------------------------------*/
static ATX_Result 
BLT_NetworkStream_GetAvailable(ATX_InputStream* _self, ATX_Size* available)
{
    BLT_NetworkStream* self = ATX_SELF(BLT_NetworkStream, ATX_InputStream);
    ATX_Size available_from_source = 0;
    ATX_InputStream_GetAvailable(self->source, &available_from_source);
    *available = available_from_source+ATX_RingBuffer_GetAvailable(self->buffer);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLT_NetworkStream)
    ATX_GET_INTERFACE_ACCEPT(BLT_NetworkStream, ATX_InputStream)
    ATX_GET_INTERFACE_ACCEPT(BLT_NetworkStream, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|       ATX_InputStream interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLT_NetworkStream, ATX_InputStream)
    BLT_NetworkStream_Read,
    BLT_NetworkStream_Seek,
    BLT_NetworkStream_Tell,
    BLT_NetworkStream_GetSize,
    BLT_NetworkStream_GetAvailable
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE(BLT_NetworkStream, reference_count)

