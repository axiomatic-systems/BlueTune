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
    BLT_Registry   registry;
    ATX_Properties settings;
    ATX_List*      modules;
};

/*----------------------------------------------------------------------
|    forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Core)
static const BLT_CoreInterface Core_BLT_CoreInterface;

/*----------------------------------------------------------------------
|    Core_Create
+---------------------------------------------------------------------*/
static BLT_Result
Core_Create(BLT_Core* object)
{
    Core*      core;
    BLT_Result result;

    /* allocate memory for the object */
    core = ATX_AllocateZeroMemory(sizeof(Core));
    if (core == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* create the registry */
    result = Registry_Create(&core->registry);
    if (BLT_FAILED(result)) {
        ATX_CLEAR_OBJECT(object);
        ATX_FreeMemory(core);
        return result;
    }

    /* create the settings */
    ATX_Properties_Create(&core->settings);

    /* create the module list */
    result = ATX_List_Create(&core->modules);
    if (BLT_FAILED(result)) {
        ATX_DESTROY_OBJECT(&core->registry);
        ATX_CLEAR_OBJECT(object);
        ATX_FreeMemory(core);
        return result;
    }

    /* construct reference */
    ATX_INSTANCE(object) = (BLT_CoreInstance*)core;
    ATX_INTERFACE(object) = &Core_BLT_CoreInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Core_Destroy(ATX_DestroyableInstance* instance)
{
    Core* core = (Core*)instance;

    /* release the modules in the list */
    ATX_List_ReleaseObjects(core->modules);

    /* delete the module list */
    ATX_List_Destroy(core->modules);

    /* destroy the settings */
    ATX_DESTROY_OBJECT(&core->settings);

    /* destroy the registry */
    BLT_Registry_Destroy(&core->registry);

    /* free the object memory */
    ATX_FreeMemory(core);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_CreateStream
+---------------------------------------------------------------------*/
BLT_METHOD 
Core_CreateStream(BLT_CoreInstance* instance, BLT_Stream* stream)
{
    /* create a stream and return */
    BLT_Core core_object;
    ATX_INSTANCE(&core_object)  = instance;
    ATX_INTERFACE(&core_object) = &Core_BLT_CoreInterface;
    return Stream_Create(&core_object, stream);
}

/*----------------------------------------------------------------------
|    Core_RegisterModule
+---------------------------------------------------------------------*/
BLT_METHOD 
Core_RegisterModule(BLT_CoreInstance* instance, const BLT_Module* module)
{
    Core*      core = (Core*)instance;
    BLT_Core   core_object;
    BLT_Result result;

    /* make a reference to the core */
    ATX_INSTANCE(&core_object)  = instance;
    ATX_INTERFACE(&core_object) = &Core_BLT_CoreInterface;

    /* add the module object to the list */
    result = ATX_List_AddObject(core->modules, (ATX_Object*)module);
    if (BLT_FAILED(result)) return result;

    /* attach the module to the core */
    result = BLT_Module_Attach(module, &core_object);
    if (BLT_FAILED(result)) return result;
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_UnRegisterModule
+---------------------------------------------------------------------*/
BLT_METHOD 
Core_UnRegisterModule(BLT_CoreInstance* instance, BLT_Module* module)
{
    Core* core = (Core*)instance;

    /* remove the module object from the list */
    return ATX_List_RemoveObject(core->modules, (ATX_Object*)module);
}

/*----------------------------------------------------------------------
|    Core_EnumerateModules
+---------------------------------------------------------------------*/
BLT_METHOD
Core_EnumerateModules(BLT_CoreInstance* instance, 
                      BLT_Mask          categories,
                      ATX_Iterator*     iterator)
{
    BLT_COMPILER_UNUSED(instance);
    BLT_COMPILER_UNUSED(categories);
    BLT_COMPILER_UNUSED(iterator);
    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|    Core_GetRegistry
+---------------------------------------------------------------------*/
BLT_METHOD
Core_GetRegistry(BLT_CoreInstance* instance, BLT_Registry* registry)
{
    Core* core = (Core*)instance;

    *registry = core->registry;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_GetSettings
+---------------------------------------------------------------------*/
BLT_METHOD
Core_GetSettings(BLT_CoreInstance* instance, ATX_Properties* settings)
{
    Core* core = (Core*)instance;
    *settings = core->settings;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Core_CreateCompatibleNode
+---------------------------------------------------------------------*/
BLT_METHOD
Core_CreateCompatibleNode(BLT_CoreInstance*         instance, 
                          BLT_MediaNodeConstructor* constructor,
                          BLT_MediaNode*            node)
{
    Core*         core       = (Core*)instance;
    ATX_ListItem* item       = ATX_List_GetFirstItem(core->modules);
    int           best_match = -1;
    BLT_Module    best_module;
    BLT_Core      core_object;
    
    /* setup core reference */
    ATX_INSTANCE(&core_object)  = (BLT_CoreInstance*)core;
    ATX_INTERFACE(&core_object) = &Core_BLT_CoreInterface;
        
    /* find a module that responds to the probe */
    while (item) {
        BLT_Result   result;
        BLT_Module   module;
        BLT_Cardinal match;

        /* get the module object from the list */
        result = ATX_ListItem_GetObject(item, (ATX_Object*)&module);
        if (BLT_FAILED(result)) return result;

        /* probe the module */
        result = BLT_Module_Probe(
            &module, 
            &core_object,
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
        &best_module, 
        &core_object, 
        BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR, 
        constructor,
        &ATX_INTERFACE_ID__BLT_MediaNode,
        (ATX_Object*)node);
}

/*----------------------------------------------------------------------
|    Core_CreateMediaPacket
+---------------------------------------------------------------------*/
BLT_METHOD
Core_CreateMediaPacket(BLT_CoreInstance*    instance,
                       BLT_Size             size,
                       const BLT_MediaType* type,
                       BLT_MediaPacket**    packet)
{       
    BLT_COMPILER_UNUSED(instance);
    return BLT_MediaPacket_Create(size, type, packet);
}

/*----------------------------------------------------------------------
|    BLT_Core interface
+---------------------------------------------------------------------*/
static const BLT_CoreInterface
Core_BLT_CoreInterface = {
    Core_GetInterface,
    Core_CreateStream,
    Core_RegisterModule,
    Core_UnRegisterModule,
    Core_EnumerateModules,
    Core_GetRegistry,
    Core_GetSettings,
    Core_CreateCompatibleNode,
    Core_CreateMediaPacket
};

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_DESTROYABLE_INTERFACE(Core)

/*----------------------------------------------------------------------
|       standard GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Core)
ATX_INTERFACE_MAP_ADD(Core, BLT_Core)
ATX_INTERFACE_MAP_ADD(Core, ATX_Destroyable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(Core)

/*----------------------------------------------------------------------
|    BLT_Init
+---------------------------------------------------------------------*/
BLT_Result
BLT_Init(BLT_Core* core)
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

