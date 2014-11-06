/*****************************************************************
|
|   BlueTune - Node.js binding
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#if !defined(BUILDING_NODE_EXTENSION)
#define BUILDING_NODE_EXTENSION
#endif
#include <node.h>
#include <uv.h>
#include "BlueTune.h"

/*----------------------------------------------------------------------
|    declarations
+---------------------------------------------------------------------*/
using namespace v8;
NPT_SET_LOCAL_LOGGER("bluetune.node")

/*----------------------------------------------------------------------
|   NodeMessageQueue
+---------------------------------------------------------------------*/
class NodeMessageQueue : public NPT_SimpleMessageQueue
{
private:
    // class methods
    static void OnAsyncWakeup(uv_async_t* handle, int status /*UNUSED*/) {
        BLT_Player* player = (BLT_Player*)(handle->data);
        if (player) {
            NPT_Result result;
            do {
                result = player->PumpMessage(0);
            } while (NPT_SUCCEEDED(result));
        }
    }

public:
    // constructor and destructor
    NodeMessageQueue() {
        uv_async_init(uv_default_loop(), &m_Async, OnAsyncWakeup);
        m_Async.data = NULL;
    }
    ~NodeMessageQueue() {
        uv_close((uv_handle_t*)&m_Async, NULL);
    }
    void SetPlayer(BLT_Player* player) {
        m_Async.data = player;
    }
    
    // NPT_MessageQueue methods
    virtual NPT_Result QueueMessage(NPT_Message*        message, 
                                    NPT_MessageHandler* handler) {
        NPT_SimpleMessageQueue::QueueMessage(message, handler);
        uv_async_send(&m_Async);
        return NPT_SUCCESS;
    }
    
private:
    // members
    uv_async_t m_Async;
};

/*----------------------------------------------------------------------
|    NodePlayer
+---------------------------------------------------------------------*/
class NodePlayer : public BLT_Player {
public:
    NodePlayer(NPT_MessageQueue* message_queue) : BLT_Player(message_queue) {}
    
    // BLT_DecoderClient_MessageHandler methods
    void OnAckNotification(BLT_DecoderServer_Message::CommandId id) {
        NPT_LOG_FINER_1("OnAckNotification id=%d", id);
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("ack"),
                Integer::New((int32_t)id)
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 2, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }

    void OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                            BLT_Result                           result) {
        NPT_LOG_FINER_2("OnNackNotification id=%d, result=%d", id, result);
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("nack"),
                Integer::New((int32_t)id),
                Integer::New((int32_t)result)
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 3, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }
    
    void OnDecoderStateNotification(BLT_DecoderServer::State state) {
        NPT_LOG_FINER_1("OnDecoderStateNotification state=%d", (int)state);
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("decoder-state"),
                Integer::New((int32_t)state)
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 2, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }

    void OnDecoderEventNotification(BLT_DecoderServer::DecoderEvent& event) {
        NPT_LOG_FINER("OnDecoderEventNotification");
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("decoder-event")
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 1, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }
    
    void OnStreamTimeCodeNotification(BLT_TimeCode time_code) {
        NPT_LOG_FINER_4("OnStreamTimeCodeNotification %d:%d:%d:%d", time_code.h, time_code.m, time_code.s, time_code.f);
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("timecode"),
                Integer::New((int32_t)time_code.h),
                Integer::New((int32_t)time_code.m),
                Integer::New((int32_t)time_code.s),
                Integer::New((int32_t)time_code.f),
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 5, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }
    
    void OnStreamInfoNotification(BLT_Mask update_mask, BLT_StreamInfo& info) {
        NPT_LOG_FINER("OnStreamInfoNotification");
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("stream-info")
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 1, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }
    
    void OnPropertyNotification(BLT_PropertyScope        property_scope,
                                const char*              source,
                                const char*              name,
                                const ATX_PropertyValue* value) {
        NPT_LOG_FINER_3("OnPropertyNotification scope=%d, source=%s, name=%s",
                        (int)property_scope,
                        source?source:"(null)",
                        name?name:"(null)");
        if (!m_Callback.IsEmpty()) {
            HandleScope scope;
            TryCatch try_catch;

            Local<Value> argv[] = {
                String::New("property"),
                Integer::New((int32_t)property_scope),
                String::New(source?source:""),
                String::New(name?name:"")
            };
            m_Callback->Call(Context::GetCurrent()->Global(), 4, argv);
            
            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }
        }
    }
    
    // members
    Persistent<Function> m_Callback;
};

/*----------------------------------------------------------------------
|    PlayerWrapper
+---------------------------------------------------------------------*/
class PlayerWrapper : public node::ObjectWrap {
public:
    // class methods
    static void Init(Handle<Object> exports);
    
private:
    PlayerWrapper(NodeMessageQueue* message_queue);
    virtual ~PlayerWrapper();

    // class methods
    static Persistent<Function> constructor;
    static Handle<Value> New(const Arguments& args);
    static Handle<Value> Close(const Arguments& args);
    static Handle<Value> SetInput(const Arguments& args);
    static Handle<Value> SetOutput(const Arguments& args);
    static Handle<Value> SetVolume(const Arguments& args);
    static Handle<Value> Play(const Arguments& args);
    static Handle<Value> Stop(const Arguments& args);
    static Handle<Value> Pause(const Arguments& args);
    
    // members
    NodePlayer*       m_Player;
    NodeMessageQueue* m_MessageQueue;
};

/*----------------------------------------------------------------------
|    globals
+---------------------------------------------------------------------*/
Persistent<Function> PlayerWrapper::constructor;

/*----------------------------------------------------------------------
|    PlayerWrapper::PlayerWrapper
+---------------------------------------------------------------------*/
PlayerWrapper::PlayerWrapper(NodeMessageQueue* message_queue) :
    m_Player(new NodePlayer(message_queue)),
    m_MessageQueue(message_queue) // ownership is transfered
{
    NPT_LOG_FINE("Create");
}

/*----------------------------------------------------------------------
|    PlayerWrapper::~PlayerWrapper
+---------------------------------------------------------------------*/
PlayerWrapper::~PlayerWrapper() {
    NPT_LOG_FINE("Destroy");
    delete m_Player;
    delete m_MessageQueue;
}

/*----------------------------------------------------------------------
|    PlayerWrapper::Init
+---------------------------------------------------------------------*/
void
PlayerWrapper::Init(Handle<Object> exports)
{
    NPT_LOG_FINE("Init");
    
    // Prepare constructor template
    Local<FunctionTemplate> ftpl = FunctionTemplate::New(New);
    ftpl->SetClassName(String::NewSymbol("Player"));
    ftpl->InstanceTemplate()->SetInternalFieldCount(1);
  
    // Prototype
    Local<ObjectTemplate> ptpl = ftpl->PrototypeTemplate();
    ptpl->Set(String::NewSymbol("setInput"),  FunctionTemplate::New(SetInput)->GetFunction());
    ptpl->Set(String::NewSymbol("setOutput"), FunctionTemplate::New(SetOutput)->GetFunction());
    ptpl->Set(String::NewSymbol("setVolume"), FunctionTemplate::New(SetVolume)->GetFunction());
    ptpl->Set(String::NewSymbol("play"),      FunctionTemplate::New(Play)->GetFunction());
    ptpl->Set(String::NewSymbol("stop"),      FunctionTemplate::New(Stop)->GetFunction());
    ptpl->Set(String::NewSymbol("pause"),     FunctionTemplate::New(Pause)->GetFunction());

    constructor = Persistent<Function>::New(ftpl->GetFunction());
    exports->Set(String::NewSymbol("Player"), constructor);
}

/*----------------------------------------------------------------------
|    PlayerWrapper::New
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::New(const Arguments& args)
{
    if (args.IsConstructCall()) {
        // Invoked as constructor: new Player(...)
        NPT_LOG_FINE("constructor New");
        
        // create the native objects
        NodeMessageQueue* message_queue = new NodeMessageQueue();
        PlayerWrapper* obj = new PlayerWrapper(message_queue);

        // set the callback
        if (args.Length() > 0 && args[0]->IsFunction()) {
            // we have a callback parameter
            NPT_LOG_FINE("using a callback");
            obj->m_Player->m_Callback = Persistent<Function>::New(Local<Function>::Cast(args[0]));
        } else {
            NPT_LOG_FINE("no callback");
        }
        
        // update the objects
        message_queue->SetPlayer(obj->m_Player);

        obj->Wrap(args.This());
        return args.This();
    } else {
        // Invoked as plain function: MyObject(...), turn into construct call.
        NPT_LOG_FINE("function New");
        
        HandleScope scope;
        if (args.Length() > 0) {
            unsigned argc = 1;
            Handle<Value> argv[1] = { args[0] };
            return scope.Close(constructor->NewInstance(argc, argv));
        } else {
            return scope.Close(constructor->NewInstance());
        }
    }

    return args.This();
}

/*----------------------------------------------------------------------
|    NodePlayer::Close
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::Close(const Arguments& args)
{
    HandleScope scope;

    NPT_LOG_FINE("Close");

    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    delete self->m_Player;
    self->m_Player = NULL;
    delete self->m_MessageQueue;
    self->m_MessageQueue = NULL;

    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    NodePlayer::SetInput
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::SetInput(const Arguments& args) {
    HandleScope scope;

    NPT_LOG_FINE("SetInput");
    
    if (args.Length() < 1) {
        ThrowException(Exception::Error(String::New("Too few arguments")));
        return scope.Close(Undefined());
    }

    if (!args[0]->IsString()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument 1 type")));
        return scope.Close(Undefined());
    }
    if (args.Length() >= 2 && !args[1]->IsString()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument 2 type")));
        return scope.Close(Undefined());
    }

    String::Utf8Value input_name(args[0]->ToString());

    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    if (self->m_Player) {
        if (args.Length() >= 2) {
            String::Utf8Value type(args[1]->ToString());
            self->m_Player->SetInput(*input_name, *type);
        } else {
            self->m_Player->SetInput(*input_name);
        }
    }
    
    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    NodePlayer::SetOutput
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::SetOutput(const Arguments& args) {
    HandleScope scope;

    NPT_LOG_FINE("SetOutput");
    
    if (args.Length() < 1) {
        ThrowException(Exception::Error(String::New("Too few arguments")));
        return scope.Close(Undefined());
    }

    if (!args[0]->IsString()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument 1 type")));
        return scope.Close(Undefined());
    }
    if (args.Length() >= 2 && !args[1]->IsString()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument 2 type")));
        return scope.Close(Undefined());
    }

    String::Utf8Value output_name(args[0]->ToString());

    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    if (self->m_Player) {
        if (args.Length() >= 2) {
            String::Utf8Value type(args[1]->ToString());
            self->m_Player->SetOutput(*output_name, *type);
        } else {
            self->m_Player->SetOutput(*output_name);
        }
    }
    
    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    NodePlayer::SetVolume
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::SetVolume(const Arguments& args) {
    HandleScope scope;

    NPT_LOG_FINE("SetVolume");
    
    if (args.Length() < 1) {
        ThrowException(Exception::Error(String::New("Too few arguments")));
        return scope.Close(Undefined());
    }

    if (!args[0]->IsNumber()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument 1 type")));
        return scope.Close(Undefined());
    }

    double volume = args[0]->NumberValue();

    if (volume < 0.0 || volume > 1.0) {
        ThrowException(Exception::RangeError(String::New("Argument 1 out of range")));
        return scope.Close(Undefined());
    }

    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    if (self->m_Player) {
        self->m_Player->SetVolume(volume);
    }
    
    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    PlayerWrapper::Play
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::Play(const Arguments& args) {
    HandleScope scope;

    NPT_LOG_FINE("Play");
    
    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    if (self->m_Player) self->m_Player->Play();
    
    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    PlayerWrapper::Stop
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::Stop(const Arguments& args) {
    HandleScope scope;

    NPT_LOG_FINE("Stop");
    
    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    if (self->m_Player) self->m_Player->Stop();
    
    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    PlayerWrapper::Pause
+---------------------------------------------------------------------*/
Handle<Value>
PlayerWrapper::Pause(const Arguments& args) {
    HandleScope scope;

    NPT_LOG_FINE("Pause");
    
    PlayerWrapper* self = ObjectWrap::Unwrap<PlayerWrapper>(args.This());
    if (self->m_Player) self->m_Player->Pause();
    
    return scope.Close(args.This());
}

/*----------------------------------------------------------------------
|    Node module interface
+---------------------------------------------------------------------*/
static void
Init(Handle<Object> exports, Handle<Object> module) {
    PlayerWrapper::Init(exports);
}

NODE_MODULE(bluetune, Init)
