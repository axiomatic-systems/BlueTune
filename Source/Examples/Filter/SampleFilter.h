/*****************************************************************
|
|   Sample Filter Module
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_SAMPLE_FILTER_H_
#define _BLT_SAMPLE_FILTER_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define SAMPLE_FILTER_MODULE_NAME "com.example.filter.sample"

#define SAMPLE_FILTER_COEFFICIENTS_PROPERTY "sample.filter.coefficients"

/*----------------------------------------------------------------------
|   module
+---------------------------------------------------------------------*/
#if defined(__cplusplus)
extern "C" {
#endif

BLT_Result BLT_SampleFilterModule_GetModuleObject(BLT_Module** module);

#if defined(__cplusplus)
}
#endif

#endif /* _BLT_SAMPLE_FILTER_H_ */
