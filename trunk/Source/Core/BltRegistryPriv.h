/*****************************************************************
|
|      File: BltRegistryPriv.h
|
|      BlueTune - Registry Private API
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_REGISTRY_PRIV_H_
#define _BLT_REGISTRY_PRIV_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltRegistry.h"

/*----------------------------------------------------------------------
|       Registry_Create
+---------------------------------------------------------------------*/
BLT_Result Registry_Create(BLT_Registry** registry);

#endif /* _BLT_REGISTRY_PRIV_H_ */
