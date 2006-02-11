/*****************************************************************
|
|      File: BltByteStreamProvider.h
|
|      BlueTune - InputStreamProvider & OutputStreamProvider
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_BYTE_STREAM_PROVIDER_H_
#define _BLT_BYTE_STREAM_PROVIDER_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltTypes.h"
#include "BltMedia.h"

/*----------------------------------------------------------------------
|       BLT_InputStreamProvider
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_InputStreamProvider)
ATX_BEGIN_INTERFACE_DEFINITION(BLT_InputStreamProvider)
    BLT_Result (*GetStream)(BLT_InputStreamProviderInstance* instance,
                            ATX_InputStream*                 stream);
ATX_END_INTERFACE_DEFINITION(BLT_InputStreamProvider)

/*----------------------------------------------------------------------
|       convenience macros
+---------------------------------------------------------------------*/
#define BLT_InputStreamProvider_GetStream(object, stream) \
ATX_INTERFACE(object)->GetStream(ATX_INSTANCE(object), stream)


/*----------------------------------------------------------------------
|       BLT_OutputStreamProvider
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE(BLT_OutputStreamProvider)
ATX_BEGIN_INTERFACE_DEFINITION(BLT_OutputStreamProvider)
    BLT_Result (*GetStream)(BLT_OutputStreamProviderInstance* instance,
                            ATX_OutputStream*                 stream,
                            const BLT_MediaType*              media_type);
ATX_END_INTERFACE_DEFINITION(BLT_OutputStreamProvider)

/*----------------------------------------------------------------------
|       convenience macros
+---------------------------------------------------------------------*/
#define BLT_OutputStreamProvider_GetStream(object, stream, media_type) \
ATX_INTERFACE(object)->GetStream(ATX_INSTANCE(object), stream, media_type)

#endif /* _BLT_BYTE_STREAM_PROVIDER_H_ */
