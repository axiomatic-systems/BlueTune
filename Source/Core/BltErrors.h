/*****************************************************************
|
|   BlueTune - Error Constants
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Results and Error codes
 */

#ifndef _BLT_ERRORS_H_
#define _BLT_ERRORS_H_

/*----------------------------------------------------------------------
|    error codes
+---------------------------------------------------------------------*/
#define BLT_SUCCESS      0
#define BLT_FAILURE      (-1)

#define BLT_FAILED(result)       ((result) != BLT_SUCCESS)
#define BLT_SUCCEEDED(result)    ((result) == BLT_SUCCESS)

/* import some ATX error codes */
#define BLT_ERROR_OUT_OF_MEMORY         ATX_ERROR_OUT_OF_MEMORY  
#define BLT_ERROR_BASE_GENERAL          ATX_ERROR_BASE_GENERAL
#define BLT_ERROR_OUT_OF_MEMORY         ATX_ERROR_OUT_OF_MEMORY
#define BLT_ERROR_OUT_OF_RESOURCES      ATX_ERROR_OUT_OF_RESOURCES
#define BLT_ERROR_INTERNAL              ATX_ERROR_INTERNAL
#define BLT_ERROR_INVALID_PARAMETERS    ATX_ERROR_INVALID_PARAMETERS
#define BLT_ERROR_INVALID_STATE         ATX_ERROR_INVALID_STATE
#define BLT_ERROR_NOT_IMPLEMENTED       ATX_ERROR_NOT_IMPLEMENTED
#define BLT_ERROR_OUT_OF_RANGE          ATX_ERROR_OUT_OF_RANGE
#define BLT_ERROR_NO_SUCH_FILE          ATX_ERROR_NO_SUCH_FILE
#define BLT_ERROR_ACCESS_DENIED         ATX_ERROR_ACCESS_DENIED
#define BLT_ERROR_BASE_DEVICE           ATX_ERROR_BASE_DEVICE
#define BLT_ERROR_DEVICE_BUSY           ATX_ERROR_DEVICE_BUSY
#define BLT_ERROR_NO_SUCH_DEVICE        ATX_ERROR_NO_SUCH_DEVICE
#define BLT_ERROR_OPEN_FAILED           ATX_ERROR_OPEN_FAILED
#define BLT_ERROR_EOS                   ATX_ERROR_EOS
#define BLT_ERROR_INVALID_INTERFACE     ATX_ERROR_INVALID_INTERFACE
#define BLT_ERROR_NO_MEDIUM             ATX_ERROR_NO_MEDIUM

/* Error base */
#ifndef BLT_ERROR_BASE               
#define BLT_ERROR_BASE                (-40000)
#endif

/* Generic errors */
#define BLT_ERROR_BASE_GENERIC        (BLT_ERROR_BASE-0)
#define BLT_ERROR_PROTOCOL_FAILURE    (BLT_ERROR_BASE_GENERIC-0)

/* Media errors */
#define BLT_ERROR_BASE_MEDIA          (BLT_ERROR_BASE-100)

/* Media Node Errors */
#define BLT_ERROR_BASE_MEDIA_NODE     (BLT_ERROR_BASE-200)

/* Media Port errors */
#define BLT_ERROR_BASE_MEDIA_PORT     (BLT_ERROR_BASE-300)

/* Stream Errors */
#define BLT_ERROR_BASE_STREAM         (BLT_ERROR_BASE-400)

/* Registry errors */
#define BLT_ERROR_BASE_REGISTRY       (BLT_ERROR_BASE-500)

/* Module errors */
#define BLT_ERROR_BASE_MODULE         (BLT_ERROR_BASE-600)

#endif /* _BLT_ERRORS_H_ */
