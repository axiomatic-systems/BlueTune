/*****************************************************************
|
|   BlueTune - Module Interface
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltConfig.h"
#include "BltDefs.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.core.module")

/*----------------------------------------------------------------------
|   forward references
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BLT_BaseModule, BLT_Module)
ATX_DECLARE_INTERFACE_MAP(BLT_BaseModule, ATX_Referenceable)

/*----------------------------------------------------------------------
|   BLT_BaseModule_Construct
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Construct(BLT_BaseModule* module, 
                         BLT_CString     name, 
                         BLT_ModuleId    id,
                         BLT_Flags       flags)
{
    if (name) {
        module->info.name = ATX_DuplicateString(name);
    } else {
        module->info.name = NULL;
    }
    if (id) {
        ATX_CopyMemory(module->info.id, id, sizeof(id));
    } else {
        ATX_SetMemory(module->info.id, 0, sizeof(id));
    }
    module->info.flags = flags;
    module->reference_count = 1;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_BaseModule_Destruct
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Destruct(BLT_BaseModule* module)
{
    if (module->info.name) {
        ATX_FreeMemory((void*)module->info.name);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_BaseModule_Create
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Create(BLT_CString                name, 
                      BLT_ModuleId               id,
                      BLT_Flags                  flags,
                      const BLT_ModuleInterface* module_interface,
                      const ATX_ReferenceableInterface* referenceable_interface,
                      BLT_Module**               object)
{
    BLT_BaseModule* module;

    ATX_LOG_FINE_1("creating module name=%s", name);
    
    /* allocate memory for the object */
    module = (BLT_BaseModule*)ATX_AllocateZeroMemory(sizeof(BLT_BaseModule));
    if (module == NULL) {
        *object = NULL;
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct the object */
    BLT_BaseModule_Construct(module, name, id, flags);

    /* setup interfaces */
    ATX_BASE(module, BLT_Module).iface = module_interface;
    ATX_BASE(module, ATX_Referenceable).iface = referenceable_interface;
    *object = &ATX_BASE(module, BLT_Module);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_BaseModule_Destroy
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Destroy(BLT_BaseModule* module)
{
    ATX_LOG_FINE_1("destroying module name=%s", module->info.name);

    BLT_BaseModule_Destruct(module);
    ATX_FreeMemory((void*)module);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_BaseModule_GetInfo
+---------------------------------------------------------------------*/
BLT_DIRECT_METHOD
BLT_BaseModule_GetInfo(BLT_Module* _self, BLT_ModuleInfo* info)
{
    BLT_BaseModule* self = ATX_SELF(BLT_BaseModule, BLT_Module);

    /* check parameters */
    if (info == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    /* return the module info */
    *info = self->info;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   BLT_BaseModule_Attach
+---------------------------------------------------------------------*/
BLT_DIRECT_METHOD
BLT_BaseModule_Attach(BLT_Module* _self, BLT_Core* core)
{
    BLT_COMPILER_UNUSED(_self);
    BLT_COMPILER_UNUSED(core);
#if defined(BLT_DEBUG)
    {
        BLT_BaseModule* self = ATX_SELF(BLT_BaseModule, BLT_Module);
        ATX_LOG_FINE_1("attaching module name=%s", self->info.name?self->info.name:"");
    }
#endif
    return BLT_SUCCESS;
}
