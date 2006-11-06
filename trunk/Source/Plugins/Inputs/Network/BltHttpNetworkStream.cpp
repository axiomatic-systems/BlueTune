/*****************************************************************
|
|   BlueTune - HTTP Network Stream
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "Atomix.h"
#include "BltTypes.h"
#include "BltModule.h"
#include "BltHttpNetworkStream.h"

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter
+---------------------------------------------------------------------*/
struct HttpInputStreamAdapter {
    // interfaces
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_Referenceable);

    // class methods
    static ATX_Result  MapResult(NPT_Result result);

    // ATX_Polymorphic methods
    static ATX_Object* GetInterface_ATX_InputStream(ATX_InputStream* self, const ATX_InterfaceId* id);
    static ATX_Object* GetInterface_ATX_Referenceable(ATX_Referenceable* self, const ATX_InterfaceId* id);

    // ATX_InputStream methods
    static ATX_Result  Read(ATX_InputStream* self, ATX_Any buffer, ATX_Size bytes_to_read, ATX_Size* bytes_read);
    static ATX_Result  Seek(ATX_InputStream* self, ATX_Position where);
    static ATX_Result  Tell(ATX_InputStream* self, ATX_Position* where);
    static ATX_Result  GetSize(ATX_InputStream* self, ATX_Size* size);
    static ATX_Result  GetAvailable(ATX_InputStream* self, ATX_Size* available);

    // ATX_Referenceable methods
    static ATX_Result  AddReference(ATX_Referenceable* self);
    static ATX_Result  Release(ATX_Referenceable* self);

    // class members
    static ATX_InputStreamInterface   ATX_InputStreamInterface;
    static ATX_ReferenceableInterface ATX_ReferenceableInterface;

    // methods
    HttpInputStreamAdapter(NPT_InputStreamReference input_stream);
    ATX_Object* GetInterface(const ATX_InterfaceId* id);

    // members
    ATX_Cardinal             m_ReferenceCount;
    NPT_InputStreamReference m_InputStream;
};

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::ATX_InputStreamInterface
+---------------------------------------------------------------------*/
ATX_InputStreamInterface 
HttpInputStreamAdapter::ATX_InputStreamInterface = {
    HttpInputStreamAdapter::GetInterface_ATX_InputStream,
    HttpInputStreamAdapter::Read,
    HttpInputStreamAdapter::Seek,
    HttpInputStreamAdapter::Tell,
    HttpInputStreamAdapter::GetSize,
    HttpInputStreamAdapter::GetAvailable
};

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::ATX_ReferenceableInterface
+---------------------------------------------------------------------*/
ATX_ReferenceableInterface 
HttpInputStreamAdapter::ATX_ReferenceableInterface = {
    HttpInputStreamAdapter::GetInterface_ATX_Referenceable,
    HttpInputStreamAdapter::AddReference,
    HttpInputStreamAdapter::Release,
};

#define HttpInputStreamAdapter_ATX_InputStreamInterface HttpInputStreamAdapter::ATX_InputStreamInterface
#define HttpInputStreamAdapter_ATX_ReferenceableInterface HttpInputStreamAdapter::ATX_ReferenceableInterface

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::GetInterface
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStreamAdapter::GetInterface(const ATX_InterfaceId* id)
{
    if (ATX_INTERFACE_IDS_EQUAL(id, &ATX_INTERFACE_ID(ATX_InputStream))) {
        return (ATX_Object*)(void*)&ATX_InputStream_Base; 
    } else if (ATX_INTERFACE_IDS_EQUAL(id, &ATX_INTERFACE_ID(ATX_Referenceable))) {
        return (ATX_Object*)(void*)&ATX_Referenceable_Base; 
    } else {
        return NULL;
    }
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::MapResult
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::MapResult(NPT_Result result)
{
    switch (result) {
        case NPT_ERROR_EOS: return ATX_ERROR_EOS;
        default: return result;
    }
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::GetInterface_ATX_InputStream
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStreamAdapter::GetInterface_ATX_InputStream(ATX_InputStream* _self, 
                                                     const ATX_InterfaceId* id)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_InputStream);
    return self->GetInterface(id);
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::GetInterface_ATX_Referenceable
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStreamAdapter::GetInterface_ATX_Referenceable(ATX_Referenceable* _self, 
                                                       const ATX_InterfaceId* id)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_Referenceable);
    return self->GetInterface(id);
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::HttpInputStreamAdapter
+---------------------------------------------------------------------*/
HttpInputStreamAdapter::HttpInputStreamAdapter(NPT_InputStreamReference stream) :
    m_ReferenceCount(1),
    m_InputStream(stream)
{
    /* setup interfaces */
    ATX_SET_INTERFACE(this, HttpInputStreamAdapter, ATX_InputStream);
    ATX_SET_INTERFACE(this, HttpInputStreamAdapter, ATX_Referenceable);
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::AddReference
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::AddReference(ATX_Referenceable* _self)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_Referenceable);
    ++self->m_ReferenceCount;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::Release
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::Release(ATX_Referenceable* _self)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_Referenceable);
    if (self == NULL) return ATX_SUCCESS;
    if (--self->m_ReferenceCount == 0) {
        delete self;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::Read
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::Read(ATX_InputStream* _self,
                             ATX_Any          buffer,
                             ATX_Size         bytes_to_read,
                             ATX_Size*        bytes_read)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_InputStream);
    return MapResult(self->m_InputStream->Read(buffer, bytes_to_read, bytes_read));
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::Seek
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::Seek(ATX_InputStream* _self, 
                             ATX_Position     where)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_InputStream);
    return MapResult(self->m_InputStream->Seek(where));
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::Tell
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::Tell(ATX_InputStream* _self, 
                             ATX_Position*    position)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_InputStream);
    NPT_Position _position;
    ATX_Result result = MapResult(self->m_InputStream->Tell(_position));
    if (position) *position = _position;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::GetSize
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::GetSize(ATX_InputStream* _self, 
                                ATX_Size*        size)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_InputStream);
    NPT_Size _size;
    ATX_Result result = MapResult(self->m_InputStream->GetSize(_size));
    if (size) *size = _size;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStreamAdapter::GetAvailable
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStreamAdapter::GetAvailable(ATX_InputStream* _self, 
                                     ATX_Size*        available)
{
    HttpInputStreamAdapter* self = ATX_SELF(HttpInputStreamAdapter, ATX_InputStream);
    NPT_Size _available;
    ATX_Result result = MapResult(self->m_InputStream->GetAvailable(_available));
    if (available) *available = _available;
    return result;
}

/*----------------------------------------------------------------------
|   BLT_HttpNetworkStream_Create
+---------------------------------------------------------------------*/
BLT_Result 
BLT_HttpNetworkStream_Create(const char* url, ATX_InputStream** stream)
{
    BLT_Result result = BLT_FAILURE;

    // default return value
    *stream = NULL;

    // create the request
    NPT_HttpRequest request(url, NPT_HTTP_METHOD_GET);
    if (!request.GetUrl().IsValid()) return BLT_ERROR_INVALID_PARAMETERS;

    // send the request
    NPT_HttpClient    client;
    NPT_HttpResponse* response = NULL;
    result = client.SendRequest(request, response);
    if (NPT_FAILED(result)) return result;

    if (response->GetStatusCode() == 200) {
        NPT_InputStreamReference input_stream;
        response->GetEntity()->GetInputStream(input_stream);
        *stream = &ATX_BASE(new HttpInputStreamAdapter(input_stream), ATX_InputStream);
    }

    // cleanup
    delete response;

    return result;
}

