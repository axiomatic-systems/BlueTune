/*****************************************************************
|
|      File: BltModule.h
|
|      BlueTune - Module Interface
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file 
 * Header file for the BLT_Module interface 
 */

#ifndef _BLT_MODULE_H_
#define _BLT_MODULE_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltTypes.h"
#include "BltCore.h"

/*----------------------------------------------------------------------
|       types
+---------------------------------------------------------------------*/
typedef enum {
    BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR
} BLT_ModuleParametersType;

typedef unsigned char BLT_ModuleId[16];

typedef struct {
    BLT_CString  name;
    BLT_ModuleId id;
    BLT_Flags    flags;
} BLT_ModuleInfo;

typedef struct {
    BLT_Cardinal   reference_count;
    BLT_ModuleInfo info;
} BLT_BaseModule;

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_MODULE_PROBE_MATCH_DEFAULT 0
#define BLT_MODULE_PROBE_MATCH_MIN     1
#define BLT_MODULE_PROBE_MATCH_LOW     64
#define BLT_MODULE_PROBE_MATCH_MEDIUM  128
#define BLT_MODULE_PROBE_MATCH_HIGH    192
#define BLT_MODULE_PROBE_MATCH_MAX     253
#define BLT_MODULE_PROBE_MATCH_EXACT   254
#define BLT_MODULE_PROBE_MATCH_FORCE   255

/*----------------------------------------------------------------------
|       error codes
+---------------------------------------------------------------------*/
#define BLT_ERROR_NO_MATCHING_MODULE (BLT_ERROR_BASE_MODULE - 0)

/*----------------------------------------------------------------------
|       BLT_Module interface
+---------------------------------------------------------------------*/
/**
 * @brief Interface implemented by objects that create other objects
 *  
 * A Module object is responsible for creating object instance of a certain 
 * class. Module objects implement the BLT_Module interface, and clients that
 * want to create instances of that module will call the CreateObject method.
 */
ATX_BEGIN_INTERFACE_DEFINITION(BLT_Module)
    BLT_Result (*GetInfo)(BLT_ModuleInstance* module, BLT_ModuleInfo* info);
    BLT_Result (*Attach)(BLT_ModuleInstance* module, BLT_Core* core);

    /** create an instance of the module that implements a given interface
     * @param instance Instance pointer of the object on which the method 
     * is called 
     * @param parameters Generic parameters used for constructing the object
     * @param interface_id Interface ID that the object needs to implement
     * @param object address of an object reference where the created object
     * will be returned if the call succeeds
     * @blt_method_result
     */
    BLT_Result (*CreateInstance)(BLT_ModuleInstance*      module,
                                 BLT_Core*                core,
                                 BLT_ModuleParametersType parameters_type,
                                 BLT_AnyConst             parameters,
                                 const ATX_InterfaceId*   interface_id,
                                 ATX_Object*              object);

    BLT_Result (*Probe)(BLT_ModuleInstance*      module,
                        BLT_Core*                core,
                        BLT_ModuleParametersType parameters_type,
                        BLT_AnyConst             parameters,
                        BLT_Cardinal*            match);
ATX_END_INTERFACE_DEFINITION(BLT_Module)

/*----------------------------------------------------------------------
|       convenience macros
+---------------------------------------------------------------------*/
#define BLT_Module_GetInfo(object, info) \
ATX_INTERFACE(object)->GetInfo(ATX_INSTANCE(object), info)

#define BLT_Module_Attach(object, core) \
ATX_INTERFACE(object)->Attach(ATX_INSTANCE(object), core)

#define BLT_Module_CreateInstance(object, core, parameters_type, parameters, interface_id, new_object) \
ATX_INTERFACE(object)->CreateInstance(ATX_INSTANCE(object),               \
                                      core,                               \
                                      parameters_type,                    \
                                      parameters,                         \
                                      interface_id,                       \
                                      new_object)

#define BLT_Module_Probe(object, core, type, query, match) \
ATX_INTERFACE(object)->Probe(ATX_INSTANCE(object), core, type, query, match)

/*----------------------------------------------------------------------
|       base methods
+---------------------------------------------------------------------*/
BLT_Result
BLT_BaseModule_Construct(BLT_BaseModule* module, 
                         BLT_CString     name, 
                         BLT_ModuleId    id,
                         BLT_Flags       flags);

BLT_Result
BLT_BaseModule_Destruct(BLT_BaseModule* module);

BLT_Result
BLT_BaseModule_Create(BLT_CString                name, 
                      BLT_ModuleId               id,
                      BLT_Flags                  flags,
                      const BLT_ModuleInterface* module_interface,
                      BLT_Module*                object);

BLT_Result
BLT_BaseModule_Destroy(BLT_BaseModule* module);

BLT_DIRECT_METHOD
BLT_BaseModule_GetInfo(BLT_ModuleInstance* instance, BLT_ModuleInfo* info);

BLT_DIRECT_METHOD
BLT_BaseModule_Attach(BLT_ModuleInstance* instance, BLT_Core* core);

/*----------------------------------------------------------------------
|       template macros
+---------------------------------------------------------------------*/
#define BLT_MODULE_IMPLEMENT_SIMPLE_CONSTRUCTOR(_module_type,               \
                                                _module_name,               \
                                                _module_flags)              \
static BLT_Result                                                           \
_module_type##Module_Create(BLT_Module* object)                             \
{                                                                           \
    _module_type##Module* module;                                           \
                                                                            \
    /* allocate memory for the object */                                    \
    module = (_module_type##Module*)                                        \
        ATX_AllocateZeroMemory(sizeof(_module_type##Module));               \
                                                                            \
    /* construct the inherited object */                                    \
    BLT_BaseModule_Construct(&module->base,                                 \
                             _module_name,                                  \
                             NULL,                                          \
                             _module_flags);                                \
                                                                            \
    /* create the object reference */                                       \
    ATX_INSTANCE(object)  = (BLT_ModuleInstance*)module;                    \
    ATX_INTERFACE(object) = &_module_type##Module_BLT_ModuleInterface;      \
                                                                            \
    return BLT_SUCCESS;                                                     \
}                                                                           \

#endif /* _BLT_MODULE_H_ */




