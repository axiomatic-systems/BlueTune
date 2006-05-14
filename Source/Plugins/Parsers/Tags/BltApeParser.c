/*****************************************************************
|
|   File: BltApeParser.c
|
|   APE TAG Parser Library
|
|   (c) 2002-2004 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltDebug.h"
#include "BltId3Parser.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_APE_TAG_HEADER_SIZE 32
#define BLT_APE_TAG_FOOTER_SIZE 32

#define BLT_APE_TAG_VERSION_OFFSET     8
#define BLT_APE_TAG_SIZE_OFFSET       12
#define BLT_APE_TAG_ITEM_COUNT_OFFSET 16
#define BLT_APE_TAG_FLAGS_OFFSET      20

#define BLT_APE_MAX_ITEM_SIZE         255
#define BLT_APE_MAX_TAG_SIZE          4096

/*----------------------------------------------------------------------
|   BLT_ApeParser_ParseStream
+---------------------------------------------------------------------*/
BLT_Result
BLT_ApeParser_ParseStream(ATX_InputStream* stream, 
                          BLT_Offset       stream_start,
                          BLT_Size         stream_size,
                          BLT_Size*        trailer_size, 
                          ATX_Properties*  properties)
{
    BLT_Size       bytes_read;
    unsigned char  footer[BLT_APE_TAG_FOOTER_SIZE];
    BLT_Flags      global_flags;
    BLT_UInt32     tag_size;
    BLT_UInt32     tag_version;
    BLT_UInt32     item_count;
    unsigned char* tag = NULL;
    BLT_Result     result;

    *trailer_size = 0;

    /* check that we have enough for a footer */
    if (stream_size < BLT_APE_TAG_FOOTER_SIZE) {
        return BLT_FAILURE;
    }

    /* seek to the start of the footer */
    result = ATX_InputStream_Seek(stream, stream_size+stream_start-BLT_APE_TAG_FOOTER_SIZE);
    if (BLT_FAILED(result)) return result;

    /* read the footer */
    result = ATX_InputStream_Read(stream, 
                                  footer, 
                                  BLT_APE_TAG_FOOTER_SIZE, 
                                  &bytes_read);

    /* return if the read failed */
    if (BLT_FAILED(result)) goto end;

    /* check if it is an APE footer */
    if (footer[0] != 'A' ||
        footer[1] != 'P' ||
        footer[2] != 'E' ||
        footer[3] != 'T' ||
        footer[4] != 'A' ||
        footer[5] != 'G' ||
        footer[6] != 'E' ||
        footer[7] != 'X') {
        result = BLT_FAILURE;
        goto end;
    }

    /* read footer fields */
    tag_version = ATX_BytesToInt32Le(&footer[BLT_APE_TAG_VERSION_OFFSET]);
    tag_size = ATX_BytesToInt32Le(&footer[BLT_APE_TAG_SIZE_OFFSET]);
    item_count = ATX_BytesToInt32Le(&footer[BLT_APE_TAG_ITEM_COUNT_OFFSET]);
    global_flags = ATX_BytesToInt32Le(&footer[BLT_APE_TAG_FLAGS_OFFSET]);

    /* check the version */
    if (tag_version != 2000) {
        result = BLT_FAILURE;
        goto end;
    }

    /* check item count */
    if (item_count == 0) {
        result = BLT_FAILURE;
        goto end;
    }

    /* check flags */
    if (global_flags & 1 || global_flags & 2) {
        /* this is not UTF-8 text */
        result = BLT_FAILURE;
        goto end;
    }

    /* check size */
    if (tag_size+BLT_APE_TAG_FOOTER_SIZE >= stream_size ||
        tag_size > BLT_APE_MAX_TAG_SIZE ||
        tag_size < BLT_APE_TAG_FOOTER_SIZE) {
        result = BLT_FAILURE;
        goto end;
    }

    /* seek to the start of the items */
    result = ATX_InputStream_Seek(stream, 
                                  stream_size+stream_start -
                                  tag_size);
    if (BLT_FAILED(result)) goto end;

    /* allocate memory for the tag.                                  */
    /* NOTE: we allocate one more byte, to ensure that the buffer is */
    /* null-terminated                                               */
    tag = (unsigned char*)ATX_AllocateZeroMemory(tag_size+1);
    if (tag == NULL) {
        result = BLT_ERROR_OUT_OF_MEMORY;
        goto end;
    }

    /* read the entire tag */
    result = ATX_InputStream_Read(stream, tag, tag_size, &bytes_read);
    if (BLT_FAILED(result)) goto end;

    /* adjust the tag size to acount for the footer */
    tag_size -= BLT_APE_TAG_FOOTER_SIZE;

    /* parse all tags */
    while (item_count && tag_size) {
        BLT_Size  value_length = ATX_BytesToInt32Le(tag);
        BLT_Flags item_flags = ATX_BytesToInt32Le(tag+4);
        BLT_Size  key_length = ATX_StringLength(tag+8)+1;
        BLT_Size  item_size = key_length + value_length + 8;

        /* check the key and item length */
        if (item_size > tag_size) {
            /* key too long, the tag is corrupted */
            result = BLT_FAILURE;
            goto end;
        }

        /* adjust counters */
        item_count--;
        tag_size -= item_size;

        /* skip non text items */
        if (item_flags & 1 || item_flags & 2) {
            tag += item_size;
            continue;
        }

        /* set a stream property for this item */
        {
            ATX_Object        source = {NULL, NULL};
            char              property_name[BLT_APE_MAX_ITEM_SIZE+32] = "Tags/APE/";
            char              property_value_string[BLT_APE_MAX_ITEM_SIZE+1];
            ATX_PropertyValue property_value;

            /* make the item name */
            ATX_CopyString(property_name+9, &tag[8]);

            /* copy the item value, so we can null-terminate it */
            if (value_length > BLT_APE_MAX_ITEM_SIZE) {
                value_length = BLT_APE_MAX_ITEM_SIZE;
            }
            ATX_CopyMemory(property_value_string, 
                           &tag[8+key_length], value_length);
            property_value_string[value_length] = 0;
            property_value.string = property_value_string;

            /* set the property */
            ATX_Properties_SetProperty(properties,
                                       property_name,
                                       ATX_PROPERTY_TYPE_STRING,
                                       &property_value);
        }        

        /* move to the next item */
        tag += item_size;
    }

    /* compute trailer size */
    if (global_flags & (1<<31)) {
        /* tag has a header */
        *trailer_size = tag_size + BLT_APE_TAG_FOOTER_SIZE;
    } else {
        /* tag has no header */
        *trailer_size = tag_size;
    }

end:
    /* rewind to where we were before parsing */
    ATX_InputStream_Seek(stream, stream_start);

    /* free the tag memory */
    if (tag) ATX_FreeMemory((void*)tag);

    return result;
}
