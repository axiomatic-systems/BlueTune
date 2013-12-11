/*****************************************************************
|
|   BlueTune - Utilities
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_UTILS_H_
#define _BLT_UTILS_H_

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "AtxStreams.h"

/*----------------------------------------------------------------------
|   BLT_InputStream_Callback_Interface
+---------------------------------------------------------------------*/
typedef struct {
    ATX_Result (*Read)(void*            self,
                       ATX_Any          buffer,
                       ATX_Size         bytes_to_read,
                       ATX_Size*        bytes_read);
    ATX_Result (*Seek)(void* self, ATX_Position  offset);
    ATX_Result (*Tell)(void* self, ATX_Position* offset);
    ATX_Result (*GetSize)(void* self, ATX_LargeSize* size);
    ATX_Result (*GetAvailable)(void* self, ATX_LargeSize* available);
} BLT_InputStream_CallbackInterface;

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
#if defined(__cplusplus)
extern "C" {
#endif

ATX_InputStream*
BLT_InputStreamWrapper_Create(void*                                    stream_instance,
                              const BLT_InputStream_CallbackInterface* stream_interface);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_UTILS_H_ */