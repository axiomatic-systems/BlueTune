/*****************************************************************
|
|   BlueTune - Player Web Service
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "Atomix.h"
#include "Neptune.h"
#include "BlueTune.h"

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
const unsigned int BT_HTTP_SERVER_DEFAULT_PORT = 8927;

const char* BT_CONTROL_FORM = 
"<form action='/player/set-input' method='get'><input type='text' name='name'/><input type='submit' value='Set Input'/></form>"
"<form action='/player/seek' method='get'><input type='text' name='timecode'/><input type='submit' value='Seek To Timecode'/></form>"
"<form action='/player/play' method='get'><input type='submit' value='Play'/></form>"
"<form action='/player/stop' method='get'><input type='submit' value='Stop'/></form>"
"<form action='/player/pause' method='get'><input type='submit' value='Pause'/></form>";

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
class BtPlayerServer : public NPT_HttpRequestHandler,
                       public BLT_Player::EventListener,
                       public NPT_Runnable
{
public:
    // methods
    BtPlayerServer();
    virtual ~BtPlayerServer();

    // methods
    NPT_Result Loop();
     
    // NPT_HttpResponseHandler methods
    virtual NPT_Result SetupResponse(NPT_HttpRequest&              request,
                                     const NPT_HttpRequestContext& context,
                                     NPT_HttpResponse&             response);

    // BLT_DecoderClient_MessageHandler methods
    void OnDecoderStateNotification(BLT_DecoderServer::State state);
    void OnStreamTimeCodeNotification(BLT_TimeCode time_code);
    void OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info);
    void OnPropertyNotification(BLT_PropertyScope        scope,
                                const char*              source,
                                const char*              name,
                                const ATX_PropertyValue* value);
    
    // NPT_Runnable methods
    void Run();
    
    // members
    NPT_Mutex m_Lock;

private:
    // methods
    NPT_Result ConstructResponse(NPT_HttpResponse& response, const char* content);
    NPT_Result SendControlForm(NPT_HttpResponse& response);
    void       DoSeekToTimecode(const char* time);
    
    // members
    BLT_Player               m_Player;
    NPT_HttpServer*          m_HttpServer;
    BLT_StreamInfo           m_StreamInfo;
    ATX_Properties*          m_CoreProperties;
    ATX_Properties*          m_StreamProperties;
    BLT_DecoderServer::State m_DecoderState;
    BLT_TimeCode             m_DecoderTimecode;
};

/*----------------------------------------------------------------------
|    BtPlayerServer::BtPlayerServer
+---------------------------------------------------------------------*/
BtPlayerServer::BtPlayerServer() :
    m_DecoderState(BLT_DecoderServer::STATE_STOPPED)
{
    // initialize status fields
    ATX_SetMemory(&m_StreamInfo, 0, sizeof(m_StreamInfo));
    ATX_Properties_Create(&m_CoreProperties);
    ATX_Properties_Create(&m_StreamProperties);
    
    // set ourselves as the player event listener
    m_Player.SetEventListener(this);
    
    // create the http server
    m_HttpServer = new NPT_HttpServer(BT_HTTP_SERVER_DEFAULT_PORT);
    
    // attach ourselves as a dynamic handler for the form control
    m_HttpServer->AddRequestHandler(this, "/control/form", false);
    
    // attach ourselves as a dynamic handler for commands
    m_HttpServer->AddRequestHandler(this, "/player", true);
}

/*----------------------------------------------------------------------
|    BtPlayerServer::~BtPlayerServer
+---------------------------------------------------------------------*/
BtPlayerServer::~BtPlayerServer()
{
    delete m_HttpServer;
    ATX_DESTROY_OBJECT(m_CoreProperties);
    ATX_DESTROY_OBJECT(m_StreamProperties);
}

/*----------------------------------------------------------------------
|    BtPlayerServer::Run
+---------------------------------------------------------------------*/
void
BtPlayerServer::Run()
{
    NPT_Result result;
    do {
        result = m_Player.PumpMessage();
    } while (NPT_SUCCEEDED(result));
}

/*----------------------------------------------------------------------
|    BtPlayerServer::Loop
+---------------------------------------------------------------------*/
NPT_Result
BtPlayerServer::Loop()
{
    return m_HttpServer->Loop();
}

/*----------------------------------------------------------------------
|    BtPlayerServer::DoSeekToTimecode
+---------------------------------------------------------------------*/
void
BtPlayerServer::DoSeekToTimecode(const char* time)
{
    BLT_UInt8    val[4] = {0,0,0,0};
    ATX_Size     length = ATX_StringLength(time);
    unsigned int val_c = 0;
    bool         has_dot = false;
    
    if (length != 11 && length != 8 && length != 5 && length != 2) return;
    
    do {
        if ( time[0] >= '0' && time[0] <= '9' && 
             time[1] >= '0' && time[0] <= '9' &&
            (time[2] == ':' || time[2] == '.' || time[2] == '\0')) {
            if (time[2] == '.') {
                if (length != 5) return; // dots only on the last part
                has_dot = true;
            } else {
                if (val_c == 3) return; // too many parts
            }
            val[val_c++] = (time[0]-'0')*10 + (time[1]-'0');
            length -= (time[2]=='\0')?2:3;
            time += 3;
        } else {
            return;
        }
    } while (length >= 2);
    
    BLT_UInt8 h,m,s,f;
    if (has_dot) --val_c;    
    h = val[(val_c+1)%4];
    m = val[(val_c+2)%4];
    s = val[(val_c+3)%4];
    f = val[(val_c  )%4];

    m_Player.SeekToTimeStamp(h,m,s,f);
}

/*----------------------------------------------------------------------
|    BtPlayerServer::SendControlForm
+---------------------------------------------------------------------*/
NPT_Result
BtPlayerServer::SendControlForm(NPT_HttpResponse& response)
{
    // create the html document
    NPT_String html = "<html>";
    
    // status
    html += "<p><b>State: </b>";
    switch (m_DecoderState) {
      case BLT_DecoderServer::STATE_STOPPED:
        html += "[STOPPED]";
        break;

      case BLT_DecoderServer::STATE_PLAYING:
        html += "[PLAYING]";
        break;

      case BLT_DecoderServer::STATE_PAUSED:
        html += "[PAUSED]";
        break;

      case BLT_DecoderServer::STATE_EOS:
        html += "[END OF STREAM]";
        break;

      default:
        html += "[UNKNOWN]";
        break;
    }
    html += "</p><p><b>Time Code: </b>";

    char time[32];
    ATX_FormatStringN(time, 32,
                      "%02d:%02d:%02d",
                      m_DecoderTimecode.h,
                      m_DecoderTimecode.m,
                      m_DecoderTimecode.s);
    html += time;
    html += "</p>";
    
    html += "<p>";
    html += "<b>Content Format: </b>";
    if (m_StreamInfo.data_type) {
        html += m_StreamInfo.data_type;
    }
    html += "<br>";
    
    
    // control form
    html += BT_CONTROL_FORM;
    html += "</html>";
    
    // send the html document
    NPT_HttpEntity* entity = response.GetEntity();
    entity->SetContentType("text/html");
    entity->SetInputStream(html);        

    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BtPlayerServer::ConstructResponse
+---------------------------------------------------------------------*/
NPT_Result
BtPlayerServer::ConstructResponse(NPT_HttpResponse& response,
                                  const char*       content)
{
    response.SetStatus(200, "Ok");
    if (content) {
        NPT_HttpEntity* entity = response.GetEntity();
        entity->SetContentType("text/html");
        entity->SetInputStream(content);        
    }
    
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BtPlayerServer::SetupResponse
+---------------------------------------------------------------------*/
NPT_Result 
BtPlayerServer::SetupResponse(NPT_HttpRequest&              request,
                              const NPT_HttpRequestContext& /*context*/,
                              NPT_HttpResponse&             response)
{
    const NPT_Url&    url  = request.GetUrl();
    const NPT_String& path = url.GetPath();
    NPT_UrlQuery      query;
    NPT_Result        result;
    
    // parse the query part, if any
    if (url.HasQuery()) {
        query.Parse(url.GetQuery());
    }
    
    // lock the player 
    NPT_AutoLock lock(m_Lock);
    
    // handle form requests
    if (path == "/control/form") {
        return SendControlForm(response);
    }
    
    // handle commands
    if (path == "/player/set-input") {
        const char* name_field = query.GetField("name");
        if (name_field) {
            NPT_String name = NPT_UrlQuery::UrlDecode(name_field);
            printf("BtPlayerServer::SetupResponse - set-input %s\n", name.GetChars());
            result = m_Player.SetInput(name);
            return ConstructResponse(response, "OK");
        } else {
            return ConstructResponse(response, "NO NAME?");
        }
    } else if (path == "/player/play") {
        result = m_Player.Play();
        return ConstructResponse(response, "OK");
    } else if (path == "/player/pause") {
        result = m_Player.Pause();
        return ConstructResponse(response, "OK");
    } else if (path == "/player/stop") {
        result = m_Player.Stop();
        return ConstructResponse(response, "OK");
    } else if (path == "/player/seek") {
        const char* timecode_field = query.GetField("timecode");
        if (timecode_field) {
            NPT_String timecode = NPT_UrlQuery::UrlDecode(timecode_field);
            DoSeekToTimecode(timecode);
            return ConstructResponse(response, "OK");
        } else {
            return ConstructResponse(response, "NO NAME?");
        }
    }
    
    printf("BtPlayerServer::SetupResponse - command not found\n");
    
    response.SetStatus(404, "Command Not Found");
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BtPlayerServer::OnDecoderStateNotification
+---------------------------------------------------------------------*/
void
BtPlayerServer::OnDecoderStateNotification(BLT_DecoderServer::State state)
{
    NPT_AutoLock lock(m_Lock);
    m_DecoderState = state;
}

/*----------------------------------------------------------------------
|    BtPlayerServer::OnStreamTimeCodeNotification
+---------------------------------------------------------------------*/
void 
BtPlayerServer::OnStreamTimeCodeNotification(BLT_TimeCode time_code)
{
    NPT_AutoLock lock(m_Lock);
    m_DecoderTimecode = time_code;
}

/*----------------------------------------------------------------------
|    BtPlayerServer::OnStreamInfoNotification
+---------------------------------------------------------------------*/
void 
BtPlayerServer::OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info)
{       
    NPT_AutoLock lock(m_Lock);
    m_StreamInfo = info;
    ATX_SET_CSTRING(m_StreamInfo.data_type, info.data_type);
}

/*----------------------------------------------------------------------
|    BtPlayerServer::OnPropertyNotification
+---------------------------------------------------------------------*/
void 
BtPlayerServer::OnPropertyNotification(BLT_PropertyScope        scope,
                                     const char*              /* source */,
                                     const char*              name,
                                     const ATX_PropertyValue* value)
{
    ATX_Properties* properties = NULL;
    switch (scope) {
        case BLT_PROPERTY_SCOPE_CORE:   properties = m_CoreProperties;   break;
        case BLT_PROPERTY_SCOPE_STREAM: properties = m_StreamProperties; break;
        default: return;
    }
    
    // when the name is NULL or empty, it means that all the properties in that 
    // scope fo that source have been deleted 
    if (name == NULL || name[0] == '\0') {
        ATX_Properties_Clear(properties);
        return;
    }
    
    ATX_Properties_SetProperty(properties, name, value);
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int /*argc*/, char** /*argv*/)
{
    // create the controller a
    BtPlayerServer* server = new BtPlayerServer();

    // create a thread to handle notifications
    NPT_Thread notification_thread(*server);
    notification_thread.Start();
    
    // loop until a termination request arrives
    server->Loop();
    
    // wait for the notification thread to end
    notification_thread.Wait();
    
    // delete the controller
    delete server;

    return 0;
}
