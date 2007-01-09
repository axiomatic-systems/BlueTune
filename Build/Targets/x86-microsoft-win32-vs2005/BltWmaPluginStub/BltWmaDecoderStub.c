/****************************************************************
|
|   WMA Decoder Module
|
|   (c) 2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltConfig.h"
#include "BltWmaDecoder.h"

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.decoders.wma")

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_WmaDecoderModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;
    *object = NULL;

    ATX_LOG_WARNING("BLT_WmaDecoderModule_GetModuleObject - this is a stub module, not the real one");

    return BLT_ERROR_NOT_IMPLEMENTED;
}
