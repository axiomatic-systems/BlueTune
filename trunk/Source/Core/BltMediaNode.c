/*****************************************************************
|
|      File: BltMediaNode.c
|
|      BlueTune - MediaNode Objects
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune MediaNode Objects
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltDefs.h"
#include "BltErrors.h"
#include "BltCore.h"
#include "BltDebug.h"
#include "BltMediaNode.h"

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Construct
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Construct(BLT_BaseMediaNode* node, 
                            BLT_Module*        module,
                            BLT_Core*          core)
{
    node->reference_count = 1;
    node->core            = *core;

    /* setup the node info */
    ATX_SetMemory(&node->info, 0, sizeof(node->info));
    node->info.module = *module;

    /* keep a reference to the module */
    ATX_REFERENCE_OBJECT(module);

    /* by default, use the module name as the node name */
    if (module) {
        BLT_ModuleInfo module_info;
        BLT_Result     result;
        
        result = BLT_Module_GetInfo(module, &module_info);
        if (BLT_SUCCEEDED(result) && module_info.name) {
            node->info.name = ATX_DuplicateString(module_info.name);
        }
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Destruct
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Destruct(BLT_BaseMediaNode* node)
{
    /* free the node name */
    if (node->info.name) {
        ATX_FreeMemory((void*)node->info.name);
    }

    /* release the reference to the module */
    ATX_RELEASE_OBJECT(&node->info.module);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_GetInfo
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_GetInfo(BLT_MediaNodeInstance* instance, 
                          BLT_MediaNodeInfo*     info)
{
    BLT_BaseMediaNode* node = (BLT_BaseMediaNode*)instance;
    *info = node->info;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Activate
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Activate(BLT_MediaNodeInstance* instance,
                           BLT_Stream*            stream)
{
    BLT_BaseMediaNode* node = (BLT_BaseMediaNode*)instance;
    node->context = *stream;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Deactivate
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Deactivate(BLT_MediaNodeInstance* instance)
{
    BLT_BaseMediaNode* node = (BLT_BaseMediaNode*)instance;
    ATX_CLEAR_OBJECT(&node->context);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Start
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Start(BLT_MediaNodeInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Stop
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Stop(BLT_MediaNodeInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Pause
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Pause(BLT_MediaNodeInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Resume
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Resume(BLT_MediaNodeInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_BaseMediaNode_Seek
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseMediaNode_Seek(BLT_MediaNodeInstance* instance, 
                       BLT_SeekMode*          mode,
                       BLT_SeekPoint*         point)
{
    BLT_COMPILER_UNUSED(instance);
    BLT_COMPILER_UNUSED(point);
    BLT_COMPILER_UNUSED(mode);
    return BLT_SUCCESS;
}

