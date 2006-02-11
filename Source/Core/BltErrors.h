/*****************************************************************
|
|      File: BltErrors.h
|
|      BlueTune - Error Constants
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune Result and Error codes
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

/* Media errors */
#define BLT_ERROR_BASE_MEDIA          (-5000)

/* Media Node Errors */
#define BLT_ERROR_BASE_MEDIA_NODE     (-5100)

/* Media Port errors */
#define BLT_ERROR_BASE_MEDIA_PORT     (-5200)

/* Stream Errors */
#define BLT_ERROR_BASE_STREAM         (-5300)

/* Registry errors */
#define BLT_ERROR_BASE_REGISTRY       (-5400)

/* Module errors */
#define BLT_ERROR_BASE_MODULE         (-5500)

#endif /* _BLT_ERRORS_H_ */
