/*****************************************************************
|
|   SBC Parser Module
|
|   (c) 2002-2016 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_SBC_PARSER_H_
#define _BLT_SBC_PARSER_H_

/**
 * @ingroup plugin_modules
 * @ingroup plugin_parser_modules
 * @defgroup sbc_parser_module SBC Parser Module
 * Plugin module that creates media nodes that parse SBC
 * encoded streams.
 * These media nodes expect a byte stream with SBC encoded data and produce
 * packets with SBC encoded frames.
 * @{ 
 */

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|   module
+---------------------------------------------------------------------*/
BLT_Result BLT_SbcParserModule_GetModuleObject(BLT_Module** module);

/** @} */

#endif /* _BLT_SBC_PARSER_H_ */
