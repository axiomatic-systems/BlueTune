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
// it is important to keep this structure a POD (no methods or object members)
// because the strict compilers will not like use using
// the offsetof() macro necessary when using ATX_SELF()
typedef struct {
    // interfaces
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_Referenceable);

    // members
    ATX_Cardinal              m_ReferenceCount;
    NPT_HttpClient*           m_HttpClient;
    NPT_HttpUrl*              m_Url;
    NPT_HttpResponse*         m_Response;
    NPT_InputStreamReference* m_InputStream;
    NPT_Size                  m_ContentLength;
    bool                      m_Eos;
} HttpInputStream;

/*----------------------------------------------------------------------
|   HttpInputStream_MapResult
+---------------------------------------------------------------------*/
ATX_Result
HttpInputStream_MapResult(NPT_Result result)
{
    switch (result) {
        case NPT_ERROR_EOS: return ATX_ERROR_EOS;
        default: return result;
    }
}

/*----------------------------------------------------------------------
|   HttpInputStream_Destroy
+---------------------------------------------------------------------*/
void
HttpInputStream_Destroy(HttpInputStream* self)
{
    delete self->m_HttpClient;
    delete self->m_Url;
    delete self->m_Response;
    delete self->m_InputStream;
    delete self;
}

/*----------------------------------------------------------------------
|   HttpInputStream_SendRequest
+---------------------------------------------------------------------*/
BLT_METHOD
HttpInputStream_SendRequest(HttpInputStream* self, NPT_Position position)
{
    // send the request
    NPT_Result      result = BLT_FAILURE;
    NPT_HttpRequest request(*self->m_Url, NPT_HTTP_METHOD_GET);

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
    result = self->m_HttpClient->SendRequest(request, self->m_Response);
    if (NPT_FAILED(result)) return result;

    switch (self->m_Response->GetStatusCode()) {
        case 200:
            // if this is a Range request, expect a 206 instead
            if (position) return BLT_ERROR_PROTOCOL_FAILURE;
            self->m_Response->GetEntity()->GetInputStream(*self->m_InputStream);
            self->m_ContentLength = self->m_Response->GetEntity()->GetContentLength();
            result = BLT_SUCCESS;
            break;

        case 206:
            // if this is not a Range request, expect a 200 instead
            if (position == 0) return BLT_ERROR_PROTOCOL_FAILURE;
            self->m_Response->GetEntity()->GetInputStream(*self->m_InputStream);
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
|   HttpInputStream_Read
+---------------------------------------------------------------------*/
BLT_METHOD
HttpInputStream_Read(ATX_InputStream* _self,
                     ATX_Any          buffer,
                     ATX_Size         bytes_to_read,
                     ATX_Size*        bytes_read)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    if (self->m_Eos) return ATX_ERROR_EOS;
    if (self->m_InputStream->IsNull()) return ATX_ERROR_INVALID_STATE;
    return HttpInputStream_MapResult((*(self->m_InputStream))->Read(buffer, bytes_to_read, bytes_read));
}

/*----------------------------------------------------------------------
|   HttpInputStream_Seek
+---------------------------------------------------------------------*/
BLT_METHOD
HttpInputStream_Seek(ATX_InputStream* _self, 
                     ATX_Position     where)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    NPT_Result result = HttpInputStream_SendRequest(self, where);
    if (NPT_SUCCEEDED(result)) self->m_Eos = false;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStream_Tell
+---------------------------------------------------------------------*/
BLT_METHOD
HttpInputStream_Tell(ATX_InputStream* _self, 
                     ATX_Position*    position)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    if (self->m_Eos) {
        if (self->m_Response) {
            *position = self->m_Response->GetEntity()->GetContentLength();
        }
    }
    if (self->m_InputStream->IsNull()) return ATX_ERROR_INVALID_STATE;
    NPT_Position _position;
    ATX_Result result = HttpInputStream_MapResult((*(self->m_InputStream))->Tell(_position));
    if (position) *position = _position;
    return result;
}

/*----------------------------------------------------------------------
|   HttpInputStream_GetSize
+---------------------------------------------------------------------*/
BLT_METHOD
HttpInputStream_GetSize(ATX_InputStream* _self, 
                        ATX_Size*        size)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    *size = self->m_ContentLength;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   HttpInputStream_GetAvailable
+---------------------------------------------------------------------*/
BLT_METHOD
HttpInputStream_GetAvailable(ATX_InputStream* _self, 
                             ATX_Size*        available)
{
    HttpInputStream* self = ATX_SELF(HttpInputStream, ATX_InputStream);
    *available = 0;
    if (self->m_InputStream->IsNull()) return ATX_ERROR_INVALID_STATE;
    NPT_Size _available;
    ATX_Result result = HttpInputStream_MapResult((*(self->m_InputStream))->GetAvailable(_available));
    if (available) *available = _available;
    return result;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(HttpInputStream)
    ATX_GET_INTERFACE_ACCEPT(HttpInputStream, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT(HttpInputStream, ATX_InputStream)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    ATX_InputStream interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(HttpInputStream, ATX_InputStream)
    HttpInputStream_Read,
    HttpInputStream_Seek,
    HttpInputStream_Tell,
    HttpInputStream_GetSize,
    HttpInputStream_GetAvailable
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE(HttpInputStream, m_ReferenceCount)

/*----------------------------------------------------------------------
|   HttpInputStream_Create
+---------------------------------------------------------------------*/
HttpInputStream*
HttpInputStream_Create(const char* url)
{
    // create and initialize
    HttpInputStream* stream  = new HttpInputStream;
    stream->m_ReferenceCount = 1;
    stream->m_HttpClient     = new NPT_HttpClient;
    stream->m_Url            = new NPT_HttpUrl(url);
    stream->m_Response       = NULL;
    stream->m_InputStream    = new NPT_InputStreamReference;
    stream->m_ContentLength  = 0;
    stream->m_Eos            = false;

    // setup interfaces
    ATX_SET_INTERFACE(stream, HttpInputStream, ATX_InputStream);
    ATX_SET_INTERFACE(stream, HttpInputStream, ATX_Referenceable);

    return stream;
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
    HttpInputStream* http_stream = HttpInputStream_Create(url);
    if (!http_stream->m_Url->IsValid()) return BLT_ERROR_INVALID_PARAMETERS;

    // send the request
    result = HttpInputStream_SendRequest(http_stream, 0);
    if (NPT_FAILED(result)) return result;

    ATX_InputStream* adapted_input_stream = &ATX_BASE(http_stream, ATX_InputStream);
    BLT_NetworkStream_Create(BLT_HTTP_NETWORK_STREAM_BUFFER_SIZE, adapted_input_stream, stream);

    return BLT_SUCCESS;
}

