/*****************************************************************
|
|   File: BltCore.h
|
|   BlueTune - Core API
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Core API Header file
 */

#ifndef _BLT_CORE_H_
#define _BLT_CORE_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltRegistry.h"
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_MODULE_CATEGORY_INPUT     0x01
#define BLT_MODULE_CATEGORY_PARSER    0x02
#define BLT_MODULE_CATEGORY_FORMATTER 0x04
#define BLT_MODULE_CATEGORY_DECODER   0x08
#define BLT_MODULE_CATEGORY_ENCODER   0x10
#define BLT_MODULE_CATEGORY_FILTER    0x20
#define BLT_MODULE_CATEGORY_OUTPUT    0x40

/*----------------------------------------------------------------------
|   references
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_Stream)
ATX_DECLARE_INTERFACE(BLT_Module)
ATX_DECLARE_INTERFACE(BLT_MediaNode)
typedef struct BLT_MediaNodeConstructor BLT_MediaNodeConstructor;

/*----------------------------------------------------------------------
|   BLT_Core Interface
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_Core)
/**
 * @brief Interface implemented by the core of the BlueTune system
 *  
 */
ATX_BEGIN_INTERFACE_DEFINITION(BLT_Core)
    BLT_Result (*CreateStream)(BLT_Core* self, BLT_Stream** stream);
    BLT_Result (*RegisterModule)(BLT_Core* self, BLT_Module* module);
    BLT_Result (*UnRegisterModule)(BLT_Core* self, BLT_Module* module);
    BLT_Result (*EnumerateModules)(BLT_Core*      self,
                                   BLT_Mask       categories,
                                   ATX_Iterator** iterator);
    BLT_Result (*GetRegistry)(BLT_Core* self, BLT_Registry** registry);
    BLT_Result (*GetSettings)(BLT_Core* stream, ATX_Properties** settings);
    BLT_Result (*CreateCompatibleMediaNode)(BLT_Core*                 self,
                                            BLT_MediaNodeConstructor* constructor,
                                            BLT_MediaNode**           node);
    BLT_Result (*CreateMediaPacket)(BLT_Core*            self, 
                                    BLT_Size             size, 
                                    const BLT_MediaType* type,
                                    BLT_MediaPacket**    packet);
ATX_END_INTERFACE_DEFINITION

/*----------------------------------------------------------------------
|   convenience macros
+---------------------------------------------------------------------*/
#define BLT_Core_CreateStream(object, stream) \
ATX_INTERFACE(object)->CreateStream(object, stream)

#define BLT_Core_RegisterModule(object, module) \
ATX_INTERFACE(object)->RegisterModule(object, module)

#define BLT_Core_UnRegisterModule(object, module) \
ATX_INTERFACE(object)->UnRegisterModule(object, module)

#define BLT_Core_EnumerateModules(object, categories, iterator) \
ATX_INTERFACE(object)->EnumerateModules(object, categories, iterator)

#define BLT_Core_GetRegistry(object, registry) \
ATX_INTERFACE(object)->GetRegistry(object, registry)

#define BLT_Core_GetSettings(object, settings) \
ATX_INTERFACE(object)->GetSettings(object, settings)

#define BLT_Core_CreateCompatibleMediaNode(object, constructor, node) \
ATX_INTERFACE(object)->CreateCompatibleMediaNode(object, constructor, node)

#define BLT_Core_CreateMediaPacket(object, size, type, packet)\
ATX_INTERFACE(object)->CreateMediaPacket(object, size, type, packet)

#define BLT_Core_Destroy(object) ATX_DESTROY_OBJECT(object)

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
BLT_Result BLT_Init(BLT_Core** core);
BLT_Result BLT_Terminate(void);

#endif /* _BLT_CORE_H_ */
