/*****************************************************************
|
|   BlueTune - Configuration File
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    import some Atomix definitions
+---------------------------------------------------------------------*/
#include "Atomix.h"

#define BLT_COMPILER_UNUSED ATX_COMPILER_UNUSED

#define BLT_CPU_BIG_ENDIAN    ATX_CPU_BIG_ENDIAN
#define BLT_CPU_LITTLE_ENDIAN ATX_CPU_LITTLE_ENDIAN

#define BLT_CONFIG_CPU_BYTE_ORDER ATX_CONFIG_CPU_BYTE_ORDER

/*----------------------------------------------------------------------
|    compiler specifics
+---------------------------------------------------------------------*/
#if defined(_MSC_VER)
#define BLT_DLL_EXPORT __declspec( dllexport )
#if !defined(__cplusplus)
#define inline __inline
#endif
#endif

#if !defined(BLT_DLL_EXPORT)
#define BLT_DLL_EXPORT
#endif
