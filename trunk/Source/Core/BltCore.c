/*****************************************************************
|
|      File: BltCore.c
|
|      BlueTune - Core Object
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Core
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltDebug.h"
#include "BltModule.h"
#include "BltCore.h"
#include "BltCorePriv.h"
#include "BltStreamPriv.h"
#include "BltMediaNode.h"
#include "BltRegistryPriv.h"
#include "BltMediaPacketPriv.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
struct Core {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_Core);
    ATX_IMPLEMENTS(ATX_Destroyable);

    /* members */
    BLT_Registry*   registry;
    ATX_Properties* settings;
    ATX_List*       modules;
};

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(Core, BLT_Core)
ATX_DECLARE_INTERFACE_MAP(Core, ATX_Destroyable)

/*----------------------------------------------------------------------
|    Core_Create
+---------------------------------------------------------------------*/
static BLT_Result
Core_Create(BLT_Core** object)
{
    Core*      core;
    BLT_Result result;

    /* allocate memory for the object */
    core = ATX_AllocateZeroMemory(sizeof(Core));
    if (core == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* create the registry */
    result = Registry_Create(&core->registry);
    if (BLT_FAILED(result)) {
        *object = NULL;
        ATX_FreeMemory(core);
        return result;
    }

    /* create the settings */
    ATX_Properties_Create(&core->settings);

    /* create the module list */
    result = ATX_List_Create(&core->modules);
    if (BLT_FAILED(result)) {
        ATX_DESTROY_OBJECT(core->registry);
        *object = NULL;
        ATX_FreeMemory(core);
        return result;
    }

    /* setup interfaces */
    ATX_SET_INTERFACE(core, Core, BLT_Core);
    ATX_SET_INTERFACE(core, Core, ATX_Destroyable);
    *object = &ATX_BASE(core, BLT_Core);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Core_Destroy(ATX_Destroyable* _self)
{
    Core* core = ATX_SELF(Core, ATX_Destroyable);

    /* release the modules in the list */
    ATX_ListItem* item = ATX_List_GetFirstItem(core->modules);
    while (item) {
        BLT_Module* module = (BLT_Module*)ATX_ListItem_GetData(item);
        ATX_RELEASE_OBJECT(module);
        item = ATX_ListItem_GetNext(item);
    }

    /* delete the module list */
    ATX_List_Destroy(core->modules);

    /* destroy the settings */
    ATX_DESTROY_OBJECT(core->settings);

    /* destroy the registry */
    BLT_Registry_Destroy(core->registry);

    /* free the object memory */
    ATX_FreeMemory((void*)core);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_CreateStream
+---------------------------------------------------------------------*/
BLT_METHOD 
Core_CreateStream(BLT_Core* self, BLT_Stream** stream)
{
    /* create a stream and return */
    return Stream_Create(self, stream);
}

/*----------------------------------------------------------------------
|    Core_RegisterModule
+---------------------------------------------------------------------*/
BLT_METHOD 
Core_RegisterModule(BLT_Core* _self, BLT_Module* module)
{
    Core*      self = ATX_SELF(Core, BLT_Core);
    BLT_Result result;

    /* add the module object to the list */
    result = ATX_List_AddData(self->modules, (ATX_Object*)module);
    if (BLT_FAILED(result)) return result;

    /* attach the module to the core */
    result = BLT_Module_Attach(module, _self);
    if (BLT_FAILED(result)) return result;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_UnRegisterModule
+---------------------------------------------------------------------*/
BLT_METHOD 
Core_UnRegisterModule(BLT_Core* _self, BLT_Module* module)
{
    Core* self = ATX_SELF(Core, BLT_Core);

    /* remove the module object from the list */
    return ATX_List_RemoveData(self->modules, (ATX_Object*)module);
}

/*----------------------------------------------------------------------
|    Core_EnumerateModules
+---------------------------------------------------------------------*/
BLT_METHOD
Core_EnumerateModules(BLT_Core*      self, 
                      BLT_Mask       categories,
                      ATX_Iterator** iterator)
{
    /* NOT IMPLEMENTED YET */
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(categories);
    *iterator = NULL;
    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|    Core_GetRegistry
+---------------------------------------------------------------------*/
BLT_METHOD
Core_GetRegistry(BLT_Core* _self, BLT_Registry** registry)
{
    Core* self = ATX_SELF(Core, BLT_Core);
    *registry = self->registry;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_GetSettings
+---------------------------------------------------------------------*/
BLT_METHOD
Core_GetSettings(BLT_Core* _self, ATX_Properties** settings)
{
    Core* self = ATX_SELF(Core, BLT_Core);
    *settings = self->settings;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_CreateCompatibleNode
+---------------------------------------------------------------------*/
BLT_METHOD
Core_CreateCompatibleNode(BLT_Core*                 _self, 
                          BLT_MediaNodeConstructor* constructor,
                          BLT_MediaNode**           node)
{
    Core*         core = ATX_SELF(Core, BLT_Core);
    ATX_ListItem* item       = ATX_List_GetFirstItem(core->modules);
    int           best_match = -1;
    BLT_Module*   best_module;
            
    /* find a module that responds to the probe */
    while (item) {
        BLT_Result   result;
        BLT_Module*  module;
        BLT_Cardinal match;

        /* get the module object from the list */
        module = (BLT_Module*)ATX_ListItem_GetData(item);

        /* probe the module */
        result = BLT_Module_Probe(
            module, 
            _self,
            BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR,
            constructor,
            &match);

        /* keep it if it is a better match than the others */
        if (BLT_SUCCEEDED(result)) {
            if ((int)match > best_match) {
                best_match  = match;
                best_module = module;
            }
        }

        /* move on to the next module */
        item = ATX_ListItem_GetNext(item);
    }

    if (best_match == -1) {
        /* no matching module found */
        return BLT_ERROR_NO_MATCHING_MODULE;
    }

    /* create a node instance */
    return BLT_Module_CreateInstance(
        best_module, 
        _self, 
        BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR, 
        constructor,
        &ATX_INTERFACE_ID__BLT_MediaNode,
        (ATX_Object**)node);
}

/*----------------------------------------------------------------------
|    Core_CreateMediaPacket
+---------------------------------------------------------------------*/
BLT_METHOD
Core_CreateMediaPacket(BLT_Core*            self,
                       BLT_Size             size,
                       const BLT_MediaType* type,
                       BLT_MediaPacket**    packet)
{       
    BLT_COMPILER_UNUSED(self);
    return BLT_MediaPacket_Create(size, type, packet);
}

/*----------------------------------------------------------------------
|       GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Core)
    ATX_GET_INTERFACE_ACCEPT(Core, BLT_Core)
    ATX_GET_INTERFACE_ACCEPT(Core, ATX_Destroyable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_Core interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Core, BLT_Core)
    Core_CreateStream,
    Core_RegisterModule,
    Core_UnRegisterModule,
    Core_EnumerateModules,
    Core_GetRegistry,
    Core_GetSettings,
    Core_CreateCompatibleNode,
    Core_CreateMediaPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_DESTROYABLE_INTERFACE(Core)

/*----------------------------------------------------------------------
|    BLT_Init
+---------------------------------------------------------------------*/
BLT_Result
BLT_Init(BLT_Core** core)
{
    return Core_Create(core);
}

/*----------------------------------------------------------------------
|    BLT_Terminate
+---------------------------------------------------------------------*/
BLT_Result
BLT_Terminate(void)
{
    return BLT_SUCCESS;
}

