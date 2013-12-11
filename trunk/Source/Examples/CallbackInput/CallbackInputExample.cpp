/*****************************************************************
|
|   BlueTune - Callback Inpout Example
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "BlueTune.h"

/*----------------------------------------------------------------------
|   MyInputStream
+---------------------------------------------------------------------*/
typedef struct {
    ATX_DataBuffer* data;
    ATX_Position    position;
} MyInputStream;

/*----------------------------------------------------------------------
|   MyInputStream_Read
+---------------------------------------------------------------------*/
static ATX_Result
MyInputStream_Read(void*     _self,
                   void*     buffer,
                   ATX_Size  bytes_to_read,
                   ATX_Size* bytes_read)
{
    MyInputStream* self = (MyInputStream*)_self;
    
    /* compute how much data is available */
    unsigned int can_read = (unsigned int)(ATX_DataBuffer_GetDataSize(self->data)-self->position);
    
    /* check for end-of-stream */
    if (can_read == 0) return BLT_ERROR_EOS;
    
    /* don't read more than what's available */
    if (bytes_to_read > can_read) {
        bytes_to_read = can_read;
    }
    if (bytes_to_read) {
        ATX_CopyMemory(buffer, ATX_DataBuffer_GetData(self->data)+self->position, bytes_to_read);
    }
    
    /* update the stream position */
    self->position += bytes_to_read;
    
    /* return the number of bytes read if required */
    if (bytes_read) {
        *bytes_read = bytes_to_read;
    }
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   MyInputStream_Seek
+---------------------------------------------------------------------*/
static ATX_Result
MyInputStream_Seek(void* _self, ATX_Position position)
{
    MyInputStream* self = (MyInputStream*)_self;
    
    if (position <= ATX_DataBuffer_GetDataSize(self->data)) {
        self->position = position;
        return ATX_SUCCESS;
    } else {
        self->position = ATX_DataBuffer_GetDataSize(self->data);
        return BLT_ERROR_OUT_OF_RANGE;
    }
}

/*----------------------------------------------------------------------
|   MyInputStream_Tell
+---------------------------------------------------------------------*/
static ATX_Result
MyInputStream_Tell(void* _self, ATX_Position* where)
{
    MyInputStream* self = (MyInputStream*)_self;

    *where = self->position;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   MyInputStream_GetSize
+---------------------------------------------------------------------*/
static ATX_Result
MyInputStream_GetSize(void* _self, ATX_LargeSize* size)
{
    MyInputStream* self = (MyInputStream*)_self;

    *size = ATX_DataBuffer_GetDataSize(self->data);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   MyInputStream_GetAvailable
+---------------------------------------------------------------------*/
static ATX_Result
MyInputStream_GetAvailable(void* _self, ATX_LargeSize* available)
{
    MyInputStream* self = (MyInputStream*)_self;

    *available = (ATX_LargeSize)ATX_DataBuffer_GetDataSize(self->data)-self->position;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   MyInputStream_Interface
+---------------------------------------------------------------------*/
static BLT_InputStream_CallbackInterface
MyInputStreamInterface = {
    MyInputStream_Read,
    MyInputStream_Seek,
    MyInputStream_Tell,
    MyInputStream_GetSize,
    MyInputStream_GetAvailable
};

/*----------------------------------------------------------------------
|   MyInputStream_Create
+---------------------------------------------------------------------*/
static ATX_Result
MyInputStream_Create(const char* filename, ATX_InputStream** stream)
{
    ATX_Result result;
    
    /* create a new object */
    MyInputStream* self = (MyInputStream*)ATX_AllocateZeroMemory(sizeof(MyInputStream));
    if (self == NULL) return ATX_ERROR_OUT_OF_MEMORY;
    
    /* load the file in a buffer */
    result = ATX_LoadFile(filename, &self->data);
    if (ATX_FAILED(result)) {
        ATX_FreeMemory(self);
        *stream = NULL;
        return result;
    }
    
    /* create a wrapper for our instance */
    *stream = BLT_InputStreamWrapper_Create(self, &MyInputStreamInterface);
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
    BLT_Result result;
    
    if (argc != 2) {
        fprintf(stderr, "usage: callbackinputexample <filename>\n");
        return 1;
    }
    const char* input_filename = argv[1];
    (void)input_filename;
    
    /* create a decoder */
    BLT_Decoder* decoder = NULL;
    BLT_Decoder_Create(&decoder);
    
    /* register builtin modules */
    BLT_Decoder_RegisterBuiltins(decoder);
    
    /* create an input object to be called back */
    ATX_InputStream* input_object = NULL;
    result = MyInputStream_Create(input_filename, &input_object);
    if (ATX_FAILED(result)) {
        fprintf(stderr, "ERROR: failed to open input file (%d)\n", result);
        return 1;
    }
    
    /* construct the input name */
    char input_name[64];
    sprintf(input_name, "callback-input:%lld", (long long)ATX_POINTER_TO_LONG(input_object));

    /* open the input */
    /* IMPORTANT NOTE: this example is hardcoded to an input mime type audio/mp4,
       change to different type if you're not using MP4 */
    result = BLT_Decoder_SetInput(decoder, input_name, "audio/mp4");
    if (BLT_FAILED(result)) {
        fprintf(stderr, "ERROR: SetInput failed (%d)\n", result);
        return 1;
    }
    
    /* open the output */
    result = BLT_Decoder_SetOutput(decoder,
                                   BLT_DECODER_DEFAULT_OUTPUT_NAME, 
                                   NULL);
    if (BLT_FAILED(result)) {
        fprintf(stderr, "ERROR: SetOutput failed (%d)\n", result);
        return 1;
    }
    
    /* pump packets until there are no more */
    do {
        result = BLT_Decoder_PumpPacket(decoder);
    } while (BLT_SUCCEEDED(result));
    
    /* cleanup */
    ATX_RELEASE_OBJECT(input_object);
    BLT_Decoder_Destroy(decoder);
    
    return 0;
}
