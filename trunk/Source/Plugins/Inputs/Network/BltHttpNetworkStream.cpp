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
// it is important to keep this structure a POD (no methods)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
struct HttpInputStream {
    // interfaces
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_Referenceable);

    // class methods
    static ATX_Result  MapResult(NPT_Result result);
    static HttpInputStream* Create(const char* url);
    static void Destroy(HttpInputStream* self);
    static BLT_Result SendRequest(HttpInputStream* self, NPT_Position position);

    // ATX_Polymorphic methods
    static ATX_Object* GetInterface(HttpInputStream* self, const ATX_InterfaceId* id);
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
    static ATX_InputStreamInterface   InputStreamInterface;
    static ATX_ReferenceableInterface ReferenceableInterface;

    // members
    ATX_Cardinal             m_ReferenceCount;
    NPT_HttpClient           m_HttpClient;
    NPT_HttpUrl              m_Url;
    NPT_HttpResponse*        m_Response;
    NPT_InputStreamReference m_InputStream;
    NPT_Size                 m_ContentLength;
    bool                     m_Eos;
};

/*----------------------------------------------------------------------
|   HttpInputStream::InputStreamInterface
+---------------------------------------------------------------------*/
ATX_InputStreamInterface 
HttpInputStream::InputStreamInterface = {
    HttpInputStream::GetInterface_ATX_InputStream,
    HttpInputStream::Read,
    HttpInputStream::Seek,
    HttpInputStream::Tell,
    HttpInputStream::GetSize,
    HttpInputStream::GetAvailable
};

/*----------------------------------------------------------------------
|   HttpInputStream::ReferenceableInterface
+---------------------------------------------------------------------*/
ATX_ReferenceableInterface 
HttpInputStream::ReferenceableInterface = {
    HttpInputStream::GetInterface_ATX_Referenceable,
    HttpInputStream::AddReference,
    HttpInputStream::Release,
};

#define HttpInputStream_ATX_InputStreamInterface HttpInputStream::InputStreamInterface
#define HttpInputStream_ATX_ReferenceableInterface HttpInputStream::ReferenceableInterface

/*----------------------------------------------------------------------
|   HttpInputStream::GetInterface
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStream::GetInterface(HttpInputStream* self, const ATX_InterfaceId* id)
{
    if (ATX_INTERFACE_IDS_EQUAL(id, &ATX_INTERFACE_ID(ATX_InputStream))) {
        return (ATX_Object*)(void*)&self->ATX_InputStream_Base; 
    } else if (ATX_INTERFACE_IDS_EQUAL(id, &ATX_INTERFACE_ID(ATX_Referenceable))) {
        return (ATX_Object*)(void*)&self->ATX_Referenceable_Base; 
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
    return HttpInputStream::GetInterface(self, id);
}

/*----------------------------------------------------------------------
|   HttpInputStream::GetInterface_ATX_Referenceable
+---------------------------------------------------------------------*/
ATX_Object* 
HttpInputStream::GetInterface_ATX_Referenceable(ATX_Referenceable*     _self, 
                                                const ATX_InterfaceId* id)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_Referenceable);
    return HttpInputStream::GetInterface(self, id);
}

/*----------------------------------------------------------------------
|   HttpInputStream::Create
+---------------------------------------------------------------------*/
HttpInputStream*
HttpInputStream::Create(const char* url)
{
    // create and initialize
    HttpInputStream* stream = new HttpInputStream;
    stream->m_ReferenceCount = 1;
    stream->m_Url = url;
    stream->m_Response = NULL;
    stream->m_ContentLength = 0;
    stream->m_Eos = false;

    // setup interfaces
    ATX_SET_INTERFACE(stream, HttpInputStream, ATX_InputStream);
    ATX_SET_INTERFACE(stream, HttpInputStream, ATX_Referenceable);

    return stream;
}

/*----------------------------------------------------------------------
|   HttpInputStream::Destroy
+---------------------------------------------------------------------*/
void
HttpInputStream::Destroy(HttpInputStream* self)
{
    delete self->m_Response;
    delete self;
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
        HttpInputStream::Destroy(self);
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
    if (self->m_Eos) return ATX_ERROR_EOS;
    if (self->m_InputStream.IsNull()) return ATX_ERROR_INVALID_STATE;
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
    NPT_Result result = SendRequest(self, where);
    if (NPT_SUCCEEDED(result)) self->m_Eos = false;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStream::Tell
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::Tell(ATX_InputStream* _self, 
                      ATX_Position*    position)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    if (self->m_Eos) {
        if (self->m_Response) {
            *position = self->m_Response->GetEntity()->GetContentLength();
        }
    }
    if (self->m_InputStream.IsNull()) return ATX_ERROR_INVALID_STATE;
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
    *size = self->m_ContentLength;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   HttpInputStream::GetAvailable
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream::GetAvailable(ATX_InputStream* _self, 
                              ATX_Size*        available)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    *available = 0;
    if (self->m_InputStream.IsNull()) return ATX_ERROR_INVALID_STATE;
    NPT_Size _available;
    ATX_Result result = MapResult(self->m_InputStream->GetAvailable(_available));
    if (available) *available = _available;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStream::SendRequest
+---------------------------------------------------------------------*/
BLT_Result
HttpInputStream::SendRequest(HttpInputStream* self, NPT_Position position)
{
    // send the request
    NPT_Result      result = BLT_FAILURE;
    NPT_HttpRequest request(self->m_Url, NPT_HTTP_METHOD_GET);

    // delete any previous response we may have
    delete self->m_Response;
    self->m_Response = NULL;
    self->m_InputStream = NULL;

    // handle a non-zero start position
    if (position) {
        if (self->m_ContentLength == position) {
            // special case: seek to end of stream
            self->m_Eos = true;
            return BLT_SUCCESS;
        }
        NPT_String range = "bytes="+NPT_String::FromInteger(position);
        range += "-";
        request.GetHeaders().SetHeader(NPT_HTTP_HEADER_RANGE, range);
    }

    // send the request
    result = self->m_HttpClient.SendRequest(request, self->m_Response);
    if (NPT_FAILED(result)) return result;

    switch (self->m_Response->GetStatusCode()) {
        case 200:
            // if this is a Range request, expect a 206 instead
            if (position) return BLT_ERROR_PROTOCOL_FAILURE;
            self->m_Response->GetEntity()->GetInputStream(self->m_InputStream);
            self->m_ContentLength = self->m_Response->GetEntity()->GetContentLength();
            result = BLT_SUCCESS;
            break;

        case 206:
            // if this is not a Range request, expect a 200 instead
            if (position == 0) return BLT_ERROR_PROTOCOL_FAILURE;
            self->m_Response->GetEntity()->GetInputStream(self->m_InputStream);
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
    HttpInputStream* http_stream = HttpInputStream::Create(url);
    if (!http_stream->m_Url.IsValid()) return BLT_ERROR_INVALID_PARAMETERS;

    // send the request
    result = HttpInputStream::SendRequest(http_stream, 0);
    if (NPT_FAILED(result)) return result;

    ATX_InputStream* adapted_input_stream = &ATX_BASE(http_stream, ATX_InputStream);
    BLT_NetworkStream_Create(BLT_HTTP_NETWORK_STREAM_BUFFER_SIZE, adapted_input_stream, stream);

    return BLT_SUCCESS;
}

