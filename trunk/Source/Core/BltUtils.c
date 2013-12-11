/*****************************************************************
|
|   BlueTune - Utilities
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "AtxStreams.h"
#include "AtxReferenceable.h"
#include "AtxUtils.h"
#include "BltUtils.h"

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper
+---------------------------------------------------------------------*/
/* NOTE: keep this a plain-old C struct, no C++ methods */
typedef struct {
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_Referenceable);
    
    unsigned int                             reference_count;
    void*                                    stream_instance;
    const BLT_InputStream_CallbackInterface* stream_interface;
} BLT_InputStreamWrapper;

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper_Read
+---------------------------------------------------------------------*/
static ATX_Result
BLT_InputStreamWrapper_Read(ATX_InputStream* _self,
                            void*            buffer,
                            ATX_Size         bytes_to_read,
                            ATX_Size*        bytes_read)
{
    BLT_InputStreamWrapper* self = ATX_SELF(BLT_InputStreamWrapper, ATX_InputStream);
    return self->stream_interface->Read(self->stream_instance, buffer, bytes_to_read, bytes_read);
}

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper_Seek
+---------------------------------------------------------------------*/
static ATX_Result
BLT_InputStreamWrapper_Seek(ATX_InputStream* _self, ATX_Position position)
{
    BLT_InputStreamWrapper* self = ATX_SELF(BLT_InputStreamWrapper, ATX_InputStream);
    return self->stream_interface->Seek(self->stream_instance, position);
}

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper_Tell
+---------------------------------------------------------------------*/
static ATX_Result
BLT_InputStreamWrapper_Tell(ATX_InputStream* _self, ATX_Position* where)
{
    BLT_InputStreamWrapper* self = ATX_SELF(BLT_InputStreamWrapper, ATX_InputStream);
    return self->stream_interface->Tell(self->stream_instance, where);
}

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper_GetSize
+---------------------------------------------------------------------*/
static ATX_Result
BLT_InputStreamWrapper_GetSize(ATX_InputStream* _self, ATX_LargeSize* size)
{
    BLT_InputStreamWrapper* self = ATX_SELF(BLT_InputStreamWrapper, ATX_InputStream);
    return self->stream_interface->GetSize(self->stream_instance, size);
}

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper_GetAvailable
+---------------------------------------------------------------------*/
static ATX_Result
BLT_InputStreamWrapper_GetAvailable(ATX_InputStream* _self, ATX_LargeSize* available)
{
    BLT_InputStreamWrapper* self = ATX_SELF(BLT_InputStreamWrapper, ATX_InputStream);
    return self->stream_interface->GetAvailable(self->stream_instance, available);
}

/*----------------------------------------------------------------------
|   BLT_InputStreamWrapper_Destroy
+---------------------------------------------------------------------*/
static ATX_Result
BLT_InputStreamWrapper_Destroy(BLT_InputStreamWrapper* self)
{
    ATX_FreeMemory(self);
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLT_InputStreamWrapper)
    ATX_GET_INTERFACE_ACCEPT(BLT_InputStreamWrapper, ATX_InputStream)
    ATX_GET_INTERFACE_ACCEPT(BLT_InputStreamWrapper, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   ATX_InputStream interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BLT_InputStreamWrapper, ATX_InputStream)
    BLT_InputStreamWrapper_Read,
    BLT_InputStreamWrapper_Seek,
    BLT_InputStreamWrapper_Tell,
    BLT_InputStreamWrapper_GetSize,
    BLT_InputStreamWrapper_GetAvailable
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE(BLT_InputStreamWrapper, reference_count)

/*----------------------------------------------------------------------
|   BLT_BLT_InputStreamWrapper_Create
+---------------------------------------------------------------------*/
ATX_InputStream*
BLT_InputStreamWrapper_Create(void*                                    stream_instance,
                              const BLT_InputStream_CallbackInterface* stream_interface)
{
    /* create a new object */
    BLT_InputStreamWrapper* self = (BLT_InputStreamWrapper*)ATX_AllocateZeroMemory(sizeof(BLT_InputStreamWrapper));
    if (self == NULL) return ATX_ERROR_OUT_OF_MEMORY;
    
    /* construct the object */
    self->reference_count  = 1;
    self->stream_instance  = stream_instance;
    self->stream_interface = stream_interface;
    
    /* setup interfaces */
    ATX_SET_INTERFACE(self, BLT_InputStreamWrapper, ATX_InputStream);
    ATX_SET_INTERFACE(self, BLT_InputStreamWrapper, ATX_Referenceable);

    /* return a pointer to the ATX_InputStream interface */
    return &ATX_BASE(self, ATX_InputStream);
}
