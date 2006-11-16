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
#include "BltNetworkStream.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
const BLT_Size BLT_HTTP_NETWORK_STREAM_BUFFER_SIZE = 65536;

/*----------------------------------------------------------------------
|   HttpInputStream
+---------------------------------------------------------------------*/
struct HttpInputStream {
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
    HttpInputStream(const char* url);
    ~HttpInputStream();
    ATX_Object* GetInterface(const ATX_InterfaceId* id);
    BLT_Result SendRequest();

    // members
    ATX_Cardinal             m_ReferenceCount;
    NPT_HttpClient           m_HttpClient;
    NPT_HttpUrl              m_Url;
    NPT_HttpResponse*        m_Response;
    NPT_InputStreamReference m_InputStream;
};

/*----------------------------------------------------------------------
|   HttpInputStream::ATX_InputStreamInterface
+---------------------------------------------------------------------*/
ATX_InputStreamInterface 
HttpInputStream::ATX_InputStreamInterface = {
    HttpInputStream::GetInterface_ATX_InputStream,
    HttpInputStream::Read,
    HttpInputStream::Seek,
    HttpInputStream::Tell,
    HttpInputStream::GetSize,
    HttpInputStream::GetAvailable
};

/*----------------------------------------------------------------------
|   HttpInputStream::ATX_ReferenceableInterface
+---------------------------------------------------------------------*/
ATX_ReferenceableInterface 
HttpInputStream::ATX_ReferenceableInterface = {
    HttpInputStream::GetInterface_ATX_Referenceable,
    HttpInputStream::AddReference,
    HttpInputStream::Release,
};

#define HttpInputStream_ATX_InputStreamInterface HttpInputStream::ATX_InputStreamInterface
#define HttpInputStream_ATX_ReferenceableInterface HttpInputStream::ATX_ReferenceableInterface

/*----------------------------------------------------------------------
|   HttpInputStream::GetInterface
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStream::GetInterface(const ATX_InterfaceId* id)
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
|   HttpInputStream::MapResult
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::MapResult(NPT_Result result)
{
    switch (result) {
        case NPT_ERROR_EOS: return ATX_ERROR_EOS;
        default: return result;
    }
}

/*----------------------------------------------------------------------
|   HttpInputStream::GetInterface_ATX_InputStream
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStream::GetInterface_ATX_InputStream(ATX_InputStream*       _self, 
                                              const ATX_InterfaceId* id)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    return self->GetInterface(id);
}

/*----------------------------------------------------------------------
|   HttpInputStream::GetInterface_ATX_Referenceable
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStream::GetInterface_ATX_Referenceable(ATX_Referenceable*     _self, 
                                                const ATX_InterfaceId* id)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_Referenceable);
    return self->GetInterface(id);
}

/*----------------------------------------------------------------------
|   HttpInputStream::HttpInputStream
+---------------------------------------------------------------------*/
HttpInputStream::HttpInputStream(const char* url) :
    m_ReferenceCount(1),
    m_Url(url),
    m_Response(NULL)
{
    /* setup interfaces */
    ATX_SET_INTERFACE(this, HttpInputStream, ATX_InputStream);
    ATX_SET_INTERFACE(this, HttpInputStream, ATX_Referenceable);
}

/*----------------------------------------------------------------------
|   HttpInputStream::~HttpInputStream
+---------------------------------------------------------------------*/
HttpInputStream::~HttpInputStream()
{
    delete m_Response;
}

/*----------------------------------------------------------------------
|   HttpInputStream::AddReference
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::AddReference(ATX_Referenceable* _self)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_Referenceable);
    ++self->m_ReferenceCount;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   HttpInputStream::Release
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::Release(ATX_Referenceable* _self)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_Referenceable);
    if (self == NULL) return ATX_SUCCESS;
    if (--self->m_ReferenceCount == 0) {
        delete self;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   HttpInputStream::Read
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::Read(ATX_InputStream* _self,
                             ATX_Any          buffer,
                             ATX_Size         bytes_to_read,
                             ATX_Size*        bytes_read)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    return MapResult(self->m_InputStream->Read(buffer, bytes_to_read, bytes_read));
}

/*----------------------------------------------------------------------
|   HttpInputStream::Seek
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::Seek(ATX_InputStream* _self, 
                             ATX_Position     where)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    return MapResult(self->m_InputStream->Seek(where));
}

/*----------------------------------------------------------------------
|   HttpInputStream::Tell
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::Tell(ATX_InputStream* _self, 
                      ATX_Position*    position)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    NPT_Position _position;
    ATX_Result result = MapResult(self->m_InputStream->Tell(_position));
    if (position) *position = _position;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStream::GetSize
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::GetSize(ATX_InputStream* _self, 
                         ATX_Size*        size)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    if (self->m_Response) {
        if (size) *size = self->m_Response->GetEntity()->GetContentLength();
        return ATX_SUCCESS;
    } else {
        return ATX_ERROR_INVALID_STATE;
    }
}

/*----------------------------------------------------------------------
|   HttpInputStream::GetAvailable
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::GetAvailable(ATX_InputStream* _self, 
                              ATX_Size*        available)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    NPT_Size _available;
    ATX_Result result = MapResult(self->m_InputStream->GetAvailable(_available));
    if (available) *available = _available;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStream::SendRequest
+---------------------------------------------------------------------*/
BLT_Result
HttpInputStream::SendRequest()
{
    // send the request
    NPT_Result        result = BLT_FAILURE;
    NPT_HttpRequest   request(m_Url, NPT_HTTP_METHOD_GET);

    // delete any previous response we may have
    delete m_Response;

    result = m_HttpClient.SendRequest(request, m_Response);
    if (NPT_FAILED(result)) return result;

    switch (m_Response->GetStatusCode()) {
        case 200:
            m_Response->GetEntity()->GetInputStream(m_InputStream);
            result = BLT_SUCCESS;
            break;

        case 404:
            result = BLT_ERROR_STREAM_INPUT_NOT_FOUND;
            break;

        default:
            result = BLT_FAILURE;
    }

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

    // create a stream object
    HttpInputStream* http_stream = new HttpInputStream(url);
    if (!http_stream->m_Url.IsValid()) return BLT_ERROR_INVALID_PARAMETERS;

    // send the request
    result = http_stream->SendRequest();
    if (NPT_FAILED(result)) return result;

    ATX_InputStream* adapted_input_stream = &ATX_BASE(http_stream, ATX_InputStream);
    BLT_NetworkStream_Create(BLT_HTTP_NETWORK_STREAM_BUFFER_SIZE, adapted_input_stream, stream);

    return BLT_SUCCESS;
}

