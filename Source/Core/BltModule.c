/*****************************************************************
|
|      File: BltModule.c
|
|      BlueTune - Module Interface
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltConfig.h"
#include "BltDefs.h"
#include "BltDebug.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|       BLT_BaseModule_Construct
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
|       BLT_BaseModule_Destruct
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
|       BLT_BaseModule_Create
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Create(BLT_CString                name, 
                      BLT_ModuleId               id,
                      BLT_Flags                  flags,
                      const BLT_ModuleInterface* module_interface,
                      BLT_Module*                object)
{
    BLT_BaseModule* module;

    /* allocate memory for the object */
    module = (BLT_BaseModule*)ATX_AllocateZeroMemory(sizeof(BLT_BaseModule));
    
    /* construct the object */
    BLT_BaseModule_Construct(module, name, id, flags);

    /* create the object reference */
    ATX_INSTANCE(object)  = (BLT_ModuleInstance*)module;
    ATX_INTERFACE(object) = module_interface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_BaseModule_Destroy
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Destroy(BLT_BaseModule* module)
{
    BLT_BaseModule_Destruct(module);
    ATX_FreeMemory((void*)module);
    
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_BaseModule_GetInfo
+---------------------------------------------------------------------*/
BLT_DIRECT_METHOD
BLT_BaseModule_GetInfo(BLT_ModuleInstance* instance, BLT_ModuleInfo* info)
{
    BLT_BaseModule* module = (BLT_BaseModule*)instance;

    /* check parameters */
    if (info == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    /* return the module info */
    *info = module->info;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_BaseModule_Attach
+---------------------------------------------------------------------*/
BLT_DIRECT_METHOD
BLT_BaseModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core)
{
    BLT_BaseModule* module = (BLT_BaseModule*)instance;
    BLT_COMPILER_UNUSED(core);
    BLT_Debug("%sModule::Attach\n", module->info.name?module->info.name:"");
    return BLT_SUCCESS;
}

