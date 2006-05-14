/*****************************************************************
|
|      File: BltMediaNode.h
|
|      BlueTune - Media Node Interface
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune MediaNode Interface Header file
 */

#ifndef _BLT_MEDIA_NODE_H_
#define _BLT_MEDIA_NODE_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltModule.h"
#include "BltMedia.h"
#include "BltCore.h"
#include "BltMediaPort.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_ERROR_NO_SUCH_MEDIA_NODE (BLT_ERROR_BASE_MEDIA_NODE - 0)

/*----------------------------------------------------------------------
|       types
+---------------------------------------------------------------------*/
typedef enum {
    BLT_MEDIA_NODE_STATE_RESET,
    BLT_MEDIA_NODE_STATE_IDLE,
    BLT_MEDIA_NODE_STATE_RUNNING,
    BLT_MEDIA_NODE_STATE_PAUSED
} BLT_MediaNodeState;

typedef struct {
    BLT_Module* module;
    BLT_CString name;
    BLT_Flags   flags;
} BLT_MediaNodeInfo;

typedef struct {
    BLT_MediaPortInterfaceSpec input;
    BLT_MediaPortInterfaceSpec output;
} BLT_MediaNodeSpec;

struct BLT_MediaNodeConstructor {
    BLT_CString       name;
    BLT_MediaNodeSpec spec;
};

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(BLT_MediaNode);
    ATX_IMPLEMENTS(ATX_Referenceable);

    /* members */
    BLT_Cardinal      reference_count;
    BLT_Core*         core;
    BLT_Stream*       context;
    BLT_MediaNodeInfo info;
} BLT_BaseMediaNode;

/*----------------------------------------------------------------------
|       BLT_MediaNode Interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_DEFINITION(BLT_MediaNode)
    BLT_Result (*GetInfo)(BLT_MediaNode* self, BLT_MediaNodeInfo* info);
    BLT_Result (*GetPortByName)(BLT_MediaNode*  self, 
                                BLT_CString     name,
                                BLT_MediaPort** port);
    BLT_Result (*Activate)(BLT_MediaNode* self, BLT_Stream* stream);
    BLT_Result (*Deactivate)(BLT_MediaNode* self);
    BLT_Result (*Start)(BLT_MediaNode* self);
    BLT_Result (*Stop)(BLT_MediaNode* self);
    BLT_Result (*Pause)(BLT_MediaNode* self);
    BLT_Result (*Resume)(BLT_MediaNode* self);
    BLT_Result (*Seek)(BLT_MediaNode* self, 
                       BLT_SeekMode*  mode,
                       BLT_SeekPoint* point);
ATX_END_INTERFACE_DEFINITION

/*----------------------------------------------------------------------
|       convenience macros
+---------------------------------------------------------------------*/
#define BLT_MediaNode_GetInfo(object, info) \
ATX_INTERFACE(object)->GetInfo(object, info)

#define BLT_MediaNode_GetPortByName(object, name, port) \
ATX_INTERFACE(object)->GetPortByName(object, name, port)

#define BLT_MediaNode_Activate(object, stream) \
ATX_INTERFACE(object)->Activate(object, stream)

#define BLT_MediaNode_Deactivate(object) \
ATX_INTERFACE(object)->Deactivate(object)

#define BLT_MediaNode_Start(object) \
ATX_INTERFACE(object)->Start(object)

#define BLT_MediaNode_Stop(object) \
ATX_INTERFACE(object)->Stop(object)

#define BLT_MediaNode_Pause(object) \
ATX_INTERFACE(object)->Pause(object)

#define BLT_MediaNode_Resume(object) \
ATX_INTERFACE(object)->Resume(object)

#define BLT_MediaNode_Seek(object, mode, point) \
ATX_INTERFACE(object)->Seek(object, mode, point)

/*----------------------------------------------------------------------
|       prototypes
+---------------------------------------------------------------------*/
BLT_Result BLT_BaseMediaNode_Construct(BLT_BaseMediaNode* self,
                                       BLT_Module*        module,
                                       BLT_Core*          core);
BLT_Result BLT_BaseMediaNode_Destruct(BLT_BaseMediaNode* self);
BLT_Result BLT_BaseMediaNode_GetInfo(BLT_MediaNode*     self,
                                     BLT_MediaNodeInfo* info);
BLT_Result BLT_BaseMediaNode_Activate(BLT_MediaNode* self,
                                      BLT_Stream*    stream);
BLT_Result BLT_BaseMediaNode_Deactivate(BLT_MediaNode* self);
BLT_Result BLT_BaseMediaNode_Start(BLT_MediaNode* self);
BLT_Result BLT_BaseMediaNode_Stop(BLT_MediaNode* self);
BLT_Result BLT_BaseMediaNode_Pause(BLT_MediaNode* self);
BLT_Result BLT_BaseMediaNode_Resume(BLT_MediaNode* self);
BLT_Result BLT_BaseMediaNode_Seek(BLT_MediaNode* self, 
                                  BLT_SeekMode*  mode,
                                  BLT_SeekPoint* point);

/*----------------------------------------------------------------------
|       template macros
+---------------------------------------------------------------------*/
#define BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(_module_type, _class)    \
BLT_METHOD                                                              \
_module_type##_CreateInstance(BLT_Module*        self,                  \
                              BLT_Core*                core,            \
                              BLT_ModuleParametersType parameters_type, \
                              BLT_AnyConst             parameters,      \
                              const ATX_InterfaceId*   interface_id,    \
                              ATX_Object**             object)          \
{                                                                       \
    if (ATX_INTERFACE_IDS_EQUAL(interface_id,                           \
                                &ATX_INTERFACE_ID__BLT_MediaNode)) {    \
        return _class##_Create(self,                                    \
                               core,                                    \
                               parameters_type,                         \
                               parameters,                              \
                               (BLT_MediaNode**)object);                \
    } else {                                                            \
        return BLT_ERROR_INVALID_INTERFACE;                             \
    }                                                                   \
}                                                                       \

#endif /* _BLT_MEDIA_NODE_H_ */
