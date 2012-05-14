/*****************************************************************
|
|   BlueTune - Network Queued Input Module
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltNetworkQueuedInput.h"
#include "BltCore.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltModule.h"
#include "BltByteStreamProvider.h"
#include "BltTcpNetworkStream.h"
#include "BltHttpNetworkStream.h"
#include "BltNetworkInputSource.h"
#include "BltNetworkInput.h"
#include "BltNetworkStream.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.inputs.queued-network")

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
class NetworkQueue : public NPT_Thread {
public:
    // types
    class Entry {
        public:
            Entry(const char* url, ATX_Object* media_node, NPT_UInt64 sequence);
            ~Entry();
            NPT_String                 m_Url;
            ATX_Object*                m_MediaNode;
            BLT_BufferedNetworkStream* m_NetworkStream;
            NPT_String                 m_Id;
    };
    
    // methods
    NetworkQueue(BLT_Module* factory_module);
    ~NetworkQueue();
    BLT_Result   Attach(BLT_Core* core);
    virtual void Run();
    void         Abort();
    BLT_Result   Enqueue(const char* url, Entry*& entry);
    BLT_Result   Remove(const char* entry_id);
    BLT_Result   Extract(const char* entry_id, ATX_Object** object);
    BLT_Result   GetStatus(const char* entry_id, BLT_BufferedNetworkStreamStatus* status);
    
private:
    // members
    NPT_Mutex        m_Lock;
    NPT_Thread*      m_Thread;
    volatile bool    m_ShouldExit;
    NPT_List<Entry*> m_Entries;
    NPT_UInt64       m_Sequence;
    BLT_Core*        m_Core;
    BLT_Module*      m_FactoryModule;
};

struct BLT_NetworkQueuedInputModule {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
    
    /* members */
    NetworkQueue* queue;
};

/*----------------------------------------------------------------------
|   NetworkQueue::Entry::Entry
+---------------------------------------------------------------------*/
NetworkQueue::Entry::Entry(const char* url, ATX_Object* media_node, NPT_UInt64 sequence) :
    m_Url(url),
    m_MediaNode(media_node),
    m_NetworkStream(NULL)
{
    m_Id = "netq:";
    m_Id += NPT_String::FromIntegerU(sequence);
    
    ATX_REFERENCE_OBJECT(media_node);
    
    BLT_InputStreamProvider* input_stream_provider = ATX_CAST(media_node, BLT_InputStreamProvider);
    if (input_stream_provider) {
        ATX_InputStream* input_stream = NULL;
        BLT_InputStreamProvider_GetStream(input_stream_provider, &input_stream);
        m_NetworkStream = ATX_CAST(input_stream, BLT_BufferedNetworkStream);
    }
}

/*----------------------------------------------------------------------
|   NetworkQueue::Entry::~Entry
+---------------------------------------------------------------------*/
NetworkQueue::Entry::~Entry()
{
    ATX_RELEASE_OBJECT(m_NetworkStream);
    ATX_RELEASE_OBJECT(m_MediaNode);
}

/*----------------------------------------------------------------------
|   NetworkQueue::NetworkQueue
+---------------------------------------------------------------------*/
NetworkQueue::NetworkQueue(BLT_Module* factory_module) :
    m_ShouldExit(false),
    m_Sequence(0),
    m_Core(NULL),
    m_FactoryModule(factory_module)
{
    ATX_REFERENCE_OBJECT(factory_module);
}

/*----------------------------------------------------------------------
|   NetworkQueue::~NetworkQueue
+---------------------------------------------------------------------*/
NetworkQueue::~NetworkQueue()
{
    m_Entries.Apply(NPT_ObjectDeleter<NetworkQueue::Entry>());
    ATX_RELEASE_OBJECT(m_FactoryModule);
}

/*----------------------------------------------------------------------
|   NetworkQueue::Attach
+---------------------------------------------------------------------*/
BLT_Result
NetworkQueue::Attach(BLT_Core* core)
{
    m_Core = core;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   NetworkQueue::Run
+---------------------------------------------------------------------*/
void
NetworkQueue::Run()
{
    ATX_LOG_FINE("starting thread");
    while (!m_ShouldExit) {
        NPT_AutoLock lock(m_Lock);
        
        for (NPT_List<Entry*>::Iterator it = m_Entries.GetFirstItem();
                                        it;
                                        ++it) {
            Entry* entry = *it;
            if (entry->m_NetworkStream) {
                ATX_LOG_FINEST_1("filling buffer for entry %s", entry->m_Id.GetChars());
                BLT_BufferedNetworkStream_FillBuffer(entry->m_NetworkStream);
            }
        }

        NPT_System::Sleep(0.1);
    }
}

/*----------------------------------------------------------------------
|   NetworkQueue::Abort
+---------------------------------------------------------------------*/
void
NetworkQueue::Abort()
{
    m_ShouldExit = true;
}

/*----------------------------------------------------------------------
|   NetworkQueue::Enqueue
+---------------------------------------------------------------------*/
BLT_Result   
NetworkQueue::Enqueue(const char* url, Entry*& entry)
{
    NPT_AutoLock lock(m_Lock);
    
    // create an input module instance
    BLT_MediaNodeConstructor input_constructor;
    
    NPT_SetMemory(&input_constructor, 0, sizeof(input_constructor));
    input_constructor.name = url;
    input_constructor.spec.input.media_type  = &BLT_MediaType_Unknown;
    input_constructor.spec.output.media_type = &BLT_MediaType_Unknown;
    
    BLT_Result result;
    ATX_Object* object = NULL;
    result = BLT_Module_CreateInstance(m_FactoryModule, 
                                       m_Core, 
                                       BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR, 
                                       &input_constructor, 
                                       &ATX_INTERFACE_ID__BLT_MediaNode, 
                                       &object);
    if (BLT_FAILED(result)) return result;
    
    entry = new Entry(url, object, m_Sequence++);
    m_Entries.Add(entry);
    ATX_RELEASE_OBJECT(object);
    
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   NetworkQueue::Remove
+---------------------------------------------------------------------*/
BLT_Result 
NetworkQueue::Remove(const char* entry_id)
{
    NPT_AutoLock lock(m_Lock);
    
    for (NPT_List<Entry*>::Iterator it = m_Entries.GetFirstItem();
                                    it;
                                    ++it) {
        Entry* entry = *it;
        if (entry->m_Id == entry_id) {
            ATX_LOG_FINE_1("removing entry %s", entry_id);
            m_Entries.Erase(it);
            delete entry;
            return BLT_SUCCESS;
        }
    }
    
    return ATX_ERROR_NO_SUCH_ITEM;
}

/*----------------------------------------------------------------------
|   NetworkQueue::Extract
+---------------------------------------------------------------------*/
BLT_Result   
NetworkQueue::Extract(const char* entry_id, ATX_Object** object)
{
    NPT_AutoLock lock(m_Lock);
    
    for (NPT_List<Entry*>::Iterator it = m_Entries.GetFirstItem();
                                    it;
                                    ++it) {
        Entry* entry = *it;
        if (entry->m_Id == entry_id) {
            ATX_LOG_FINE_1("extracting entry %s", entry_id);
            m_Entries.Erase(it);
            *object = entry->m_MediaNode;
            entry->m_MediaNode = NULL;
            delete entry;
            return BLT_SUCCESS;
        }
    }
    
    return ATX_ERROR_NO_SUCH_ITEM;
}

/*----------------------------------------------------------------------
|   NetworkQueue::GetStatus
+---------------------------------------------------------------------*/
BLT_Result 
NetworkQueue::GetStatus(const char*                      entry_id, 
                        BLT_BufferedNetworkStreamStatus* status)
{
    NPT_AutoLock lock(m_Lock);
    
    for (NPT_List<Entry*>::Iterator it = m_Entries.GetFirstItem();
                                    it;
                                    ++it) {
        Entry* entry = *it;
        if (entry->m_Id == entry_id) {
            if (entry->m_NetworkStream) {
                ATX_LOG_FINE_1("getting entry status for %s", entry_id);
                return BLT_BufferedNetworkStream_GetStatus(entry->m_NetworkStream, status);
            }
        }
    }
    
    return ATX_ERROR_NO_SUCH_ITEM;
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
BLT_NetworkQueuedInputModule_Destroy(BLT_NetworkQueuedInputModule* module)
{
    if (module->queue) {
        module->queue->Abort();
        module->queue->Wait();
        delete module->queue;
    }
    BLT_BaseModule_Destruct(&module->BLT_BaseModule_Base);
    ATX_FreeMemory((void*)module);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_CreateInstance
+---------------------------------------------------------------------*/
BLT_METHOD
BLT_NetworkQueuedInputModule_CreateInstance(BLT_Module*              _self,
                                            BLT_Core*                core,
                                            BLT_ModuleParametersType parameters_type,
                                            BLT_AnyConst             parameters,
                                            const ATX_InterfaceId*   interface_id,
                                            ATX_Object**             object)
{
    BLT_NetworkQueuedInputModule* self = ATX_SELF_EX(BLT_NetworkQueuedInputModule, BLT_BaseModule, BLT_Module);
    BLT_MediaNodeConstructor*     constructor = (BLT_MediaNodeConstructor*)parameters;

    if (!ATX_INTERFACE_IDS_EQUAL(interface_id, &ATX_INTERFACE_ID__BLT_MediaNode)) {
        return BLT_ERROR_INVALID_INTERFACE;
    }
    
    BLT_COMPILER_UNUSED(core);
    
    *object = NULL;

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    return self->queue->Extract(constructor->name, object);
}                                

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_Enqueue
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkQueuedInputModule_Enqueue(BLT_NetworkQueuedInputModule* self,
                                     const char*                   url,
                                     const char**                  entry_id)
{
    NetworkQueue::Entry* entry = NULL;
    
    ATX_LOG_INFO_1("enqueuing %s", url);
    BLT_Result result = self->queue->Enqueue(url, entry);
    if (BLT_FAILED(result)) {
        *entry_id = NULL;
        return result;
    }
    *entry_id = entry->m_Id;
    ATX_LOG_INFO_1("entry id = %s", *entry_id);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_RemoveEntry
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkQueuedInputModule_RemoveEntry(BLT_NetworkQueuedInputModule* self,
                                         const char*                   entry_id)
{
    return self->queue->Remove(entry_id);
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_GetEntryStatus
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkQueuedInputModule_GetEntryStatus(BLT_NetworkQueuedInputModule*    self,
                                            const char*                      entry_id,
                                            BLT_BufferedNetworkStreamStatus* status)
{
    return self->queue->GetStatus(entry_id, status);
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_Attach
+---------------------------------------------------------------------*/
BLT_DIRECT_METHOD
BLT_NetworkQueuedInputModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    BLT_NetworkQueuedInputModule* self = ATX_SELF_EX(BLT_NetworkQueuedInputModule, BLT_BaseModule, BLT_Module);
    self->queue->Attach(core);
            
    /* set a propoerty to publish the module's address */
    {
        ATX_Properties* properties = NULL;
        if (ATX_SUCCEEDED(BLT_Core_GetProperties(core, &properties))) {
            ATX_PropertyValue property;
            property.type = ATX_PROPERTY_VALUE_TYPE_POINTER;
            property.data.pointer = self;
            ATX_Properties_SetProperty(properties, BLT_NETWORK_QUEUED_INPUT_HANDLE_PROPERTY, &property);
        }
    }
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
BLT_NetworkQueuedInputModule_Probe(BLT_Module*              self, 
                                   BLT_Core*                core,
                                   BLT_ModuleParametersType parameters_type,
                                   BLT_AnyConst             parameters,
                                   BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(core);

#if 0
    // FIXME: testing
    static bool done=false;
    if (1 && !done) {
        BLT_NetworkQueuedInputModule* blabla = NULL;
        BLT_NetworkQueuedInputModule_GetFromCore(core, &blabla);
        const char* entry_id = NULL;
        BLT_NetworkQueuedInputModule_Enqueue(blabla, "http://www.bok.net/tmp/test.mp3", &entry_id);
        BLT_NetworkQueuedInputModule_Enqueue(blabla, "http://www.bok.net/tmp/test.mp3", &entry_id);
        BLT_NetworkQueuedInputModule_Enqueue(blabla, "http://www.bok.net/tmp/test.m4a", &entry_id);
        BLT_BufferedNetworkStreamStatus status;
        BLT_NetworkQueuedInputModule_GetEntryStatus(blabla, entry_id, &status);
        BLT_NetworkQueuedInputModule_Enqueue(blabla, "http://www.bok.net/tmp/test.m4a", &entry_id);
        BLT_NetworkQueuedInputModule_RemoveEntry(blabla, entry_id);
        done = true;
    }
    // FIXME
#endif
    
    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR:
        {
            BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

            /* we need a file name */
            if (constructor->name == NULL) return BLT_FAILURE;

            /* the input protocol should be NONE, and the output */
            /* protocol should be STREAM_PULL                    */
            if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
                 constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_NONE) ||
                (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY && 
                 constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_STREAM_PULL)) {
                return BLT_FAILURE;
            }

            /* check the name */
            if (ATX_StringsEqualN(constructor->name, "netq:", 5)) {
                /* this is an exact match for us */
                *match = BLT_MODULE_PROBE_MATCH_EXACT;
            } else {
                return BLT_FAILURE;
            }

            ATX_LOG_FINE_1("ok [%d]", *match);
            return BLT_SUCCESS;
        }    
        break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_GetFromCore
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkQueuedInputModule_GetFromCore(BLT_Core*                      core, 
                                         BLT_NetworkQueuedInputModule** module)
{
    ATX_List* modules = NULL;

    *module = NULL;
    BLT_Result result = BLT_Core_EnumerateModules(core, &modules);
    if (BLT_FAILED(result)) {
        return result;
    }
    ATX_ListItem* item = ATX_List_GetFirstItem(modules);
    while (item) {
        BLT_Module* module_item = (BLT_Module*)ATX_ListItem_GetData(item);
        if (module_item) {
            BLT_ModuleInfo info;
            if (BLT_SUCCEEDED(BLT_Module_GetInfo(module_item, &info))) {
                if (ATX_StringsEqual(info.uid, BLT_NETWORK_QUEUED_INPUT_MODULE_UID)) {
                    *module = (BLT_NetworkQueuedInputModule*)module_item;
                }
            }
        }
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Destroy(modules);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BLT_NetworkQueuedInputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(BLT_NetworkQueuedInputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(BLT_NetworkQueuedInputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(BLT_NetworkQueuedInputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_NetworkQueuedInputModule_Attach,
    BLT_NetworkQueuedInputModule_CreateInstance,
    BLT_NetworkQueuedInputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(BLT_NetworkQueuedInputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   BLT_NetworkQueuedInputModule_Create
+---------------------------------------------------------------------*/
BLT_Result
BLT_NetworkQueuedInputModule_Create(BLT_Module** object)
{
    BLT_NetworkQueuedInputModule* module = NULL;
    ATX_Property properties[2] = {
        {"version",   {ATX_PROPERTY_VALUE_TYPE_STRING, {"1.0.0"}}},
        {"copyright", {ATX_PROPERTY_VALUE_TYPE_STRING, {BLT_MODULE_AXIOMATIC_COPYRIGHT}}}
    };
    BLT_Result result;
    BLT_Module* factory_module = NULL;
    
    *object = NULL;

    // get the factory module
    result = BLT_NetworkInputModule_GetModuleObject(&factory_module);
    if (BLT_FAILED(result)) return result;

    // create the base module
    result = BLT_BaseModule_CreateEx("Network Queued Input",
                                     BLT_NETWORK_QUEUED_INPUT_MODULE_UID,
                                     0,
                                     ATX_ARRAY_SIZE(properties), 
                                     properties,
                                     &BLT_NetworkQueuedInputModule_BLT_ModuleInterface,
                                     &BLT_NetworkQueuedInputModule_ATX_ReferenceableInterface,
                                     sizeof(BLT_NetworkQueuedInputModule),
                                     object);    
    if (BLT_FAILED(result)) return result;
    module = (BLT_NetworkQueuedInputModule*)*object;
    module->queue = new NetworkQueue(factory_module);
    ATX_RELEASE_OBJECT(factory_module);
    module->queue->Start();
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_NetworkQueuedInputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;
                
    return BLT_NetworkQueuedInputModule_Create(object);
}

