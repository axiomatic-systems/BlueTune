/*****************************************************************
|
|   Key Manager Interface
|
|   (c) 2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Key Manager Interface
 */

#ifndef _BLT_KEY_MANAGER_H_
#define _BLT_KEY_MANAGER_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "Atomix.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_KEY_MANAGER_PROPERTY  "KeyManager"

/*----------------------------------------------------------------------
|   BLT_KeyManager Interface
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_KeyManager)
ATX_BEGIN_INTERFACE_DEFINITION(BLT_KeyManager)
    BLT_Result (*GetKeyByName)(BLT_KeyManager* self,
                               const char*     name,
                               unsigned char*  key, 
                               unsigned int*   key_size);
ATX_END_INTERFACE_DEFINITION

/*----------------------------------------------------------------------
|   convenience macros
+---------------------------------------------------------------------*/
#define BLT_KeyManager_GetKeyByName(object, name, key, key_size) \
    ATX_INTERFACE(object)->GetKeyByName(object, name, key, key_size)


#endif /* _BLT_KEY_MANAGER_H_ */
