/*****************************************************************
|
|      Cdda: BltCddaDevice.c
|
|      Cdda Device Library
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltCddaDevice.h"
#include "BltDebug.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_CDDA_BURST_SIZE 4

/*----------------------------------------------------------------------
|       types
+---------------------------------------------------------------------*/
struct BLT_CddaTrack {
    BLT_Cardinal      reference_count;
    BLT_CddaDevice    device;
    BLT_CddaTrackInfo info;
    BLT_Offset        offset;
    BLT_Size          size;
    struct {
        BLT_Offset    offset;
        BLT_Size      size;
        unsigned char data[BLT_CDDA_BURST_SIZE*BLT_CDDA_FRAME_SIZE];
    }                 cache;
};

/*----------------------------------------------------------------------
|       interface constants
+---------------------------------------------------------------------*/
const ATX_InterfaceId ATX_INTERFACE_ID__BLT_CddaDevice = {0x0201, 0x0001};

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(BLT_CddaTrack)
static const ATX_InputStreamInterface BLT_CddaTrack_ATX_InputStreamInterface;

/*----------------------------------------------------------------------
|       BLT_Cdda_FramesToMsf
+---------------------------------------------------------------------*/
void
BLT_Cdda_FramesToMsf(BLT_CddaLba frames, BLT_CddaMsf* msf)
{
    msf->f = frames % 75;
    frames = (frames - msf->f)/75;
    msf->s = frames % 60;
    msf->m = (frames - msf->s)/60;
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_Create
+---------------------------------------------------------------------*/
BLT_Result
BLT_CddaTrack_Create(BLT_CddaDevice* device,
                     BLT_Ordinal     index,
                     BLT_CddaTrack** track)
{
    BLT_Result result;

    /* allocate memory for the object */
    *track = (BLT_CddaTrack*)ATX_AllocateZeroMemory(sizeof(BLT_CddaTrack));
    if (*track == NULL) {
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* initialize the object */
    (*track)->device = *device;

    /* get the track info */
    result = BLT_CddaDevice_GetTrackInfo(device, index, &(*track)->info);
    if (BLT_FAILED(result)) goto failure;

    /* compute track size */
    (*track)->size = (*track)->info.duration.frames * BLT_CDDA_FRAME_SIZE;
    
    return BLT_SUCCESS;

 failure:
    BLT_CddaTrack_Destroy(*track);
    *track = NULL;
    return result;
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_Destroy
+---------------------------------------------------------------------*/
BLT_Result 
BLT_CddaTrack_Destroy(BLT_CddaTrack* track)
{
    ATX_FreeMemory((void*)track);
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_GetStream
+---------------------------------------------------------------------*/
BLT_Result 
BLT_CddaTrack_GetStream(BLT_CddaTrack* track, ATX_InputStream* stream)
{
    /* the ATX_InputStream interface is implemented directly */
    ATX_INSTANCE(stream)  = (ATX_InputStreamInstance*)track;
    ATX_INTERFACE(stream) = &BLT_CddaTrack_ATX_InputStreamInterface;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_Read
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_Read(ATX_InputStreamInstance* instance, 
                   ATX_Any                  buffer,
                   ATX_Size                 bytes_to_read,
                   ATX_Size*                bytes_read)
{
    BLT_CddaTrack* track = (BLT_CddaTrack*)instance;
    BLT_Size       bytes_left_to_read;
    unsigned char* out = buffer;
    
    /* truncate to the max size we can read */
    if (track->offset + bytes_to_read > track->size) {
        bytes_to_read = track->size - track->offset;
    }

    /* default return values */
    if (bytes_read) *bytes_read = 0;

    /* if there is nothing to read, we've reached the end */
    if (bytes_to_read == 0) {
        return BLT_ERROR_EOS;
    }

    /* read all bytes through the cache */
    bytes_left_to_read = bytes_to_read;
    while (bytes_left_to_read != 0) {
        BLT_Size in_cache;
        if (track->offset >= track->cache.offset &&
            (BLT_Size)track->offset < 
            track->cache.offset + track->cache.size) {
            /* there is cached data available */
            in_cache = track->cache.offset + track->cache.size - track->offset;
            if (in_cache > bytes_left_to_read) {
                in_cache = bytes_left_to_read;
            }

            /* copy data from the cache */
            ATX_CopyMemory(out, 
                           &track->cache.data
                           [track->offset-track->cache.offset],
                           in_cache);
            out                += in_cache;
            track->offset      += in_cache;
            bytes_left_to_read -= in_cache;
            /*BLT_Debug(">>> got %d bytes from cache (%d to read)\n", 
              in_cache, bytes_left_to_read);*/
        } else {
            /* refill the cache */
            BLT_Offset   frame_offset;
            BLT_CddaLba  frame_addr;
            BLT_Cardinal burst = BLT_CDDA_BURST_SIZE;
            BLT_Result   result;

            /* compute frame addr and offset */
            frame_offset = track->offset / BLT_CDDA_FRAME_SIZE;
            frame_addr = track->info.address + frame_offset;

            /* make sure that the burst is not too big */
            if (burst > track->info.duration.frames - frame_offset) {
                burst = track->info.duration.frames - frame_offset;
            }

            /* read the frames from the device */
            result = BLT_CddaDevice_ReadFrames(&track->device,
                                               frame_addr,
                                               burst,
                                               track->cache.data);
                                               
            if (BLT_FAILED(result)) return result;

            /* update counters */
            track->cache.offset = frame_offset * BLT_CDDA_FRAME_SIZE;
            track->cache.size   = burst * BLT_CDDA_FRAME_SIZE;
        }
    }

    /* return the number of bytes read */
    if (bytes_read) *bytes_read = bytes_to_read;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_Seek
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_Seek(ATX_InputStreamInstance* instance, ATX_Offset offset)
{
    BLT_CddaTrack* track = (BLT_CddaTrack*)instance;

    /* align offset to 4 bytes */
    offset -= offset%4;

    /* update the track offset */
    if ((BLT_Size)offset <= track->size) {
        track->offset = offset;
        return BLT_SUCCESS;
    } else {
        return BLT_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_Read
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_Tell(ATX_InputStreamInstance* instance, ATX_Offset* offset)
{
    BLT_CddaTrack* track = (BLT_CddaTrack*)instance;
    *offset = track->offset;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_GetSize
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_GetSize(ATX_InputStreamInstance* instance, ATX_Size* size)
{
    BLT_CddaTrack* track = (BLT_CddaTrack*)instance;
    *size = track->size;
    return BLT_SUCCESS;    
}

/*----------------------------------------------------------------------
|       BLT_CddaTrack_GetAvailable
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_GetAvailable(ATX_InputStreamInstance* instance,
                           ATX_Size*                available)
{
    BLT_CddaTrack* track = (BLT_CddaTrack*)instance;
    *available = track->size - track->offset;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    ATX_InputStream interface
+---------------------------------------------------------------------*/
static const ATX_InputStreamInterface
BLT_CddaTrack_ATX_InputStreamInterface = {
    BLT_CddaTrack_GetInterface,
    BLT_CddaTrack_Read,
    BLT_CddaTrack_Seek,
    BLT_CddaTrack_Tell,
    BLT_CddaTrack_GetSize,
    BLT_CddaTrack_GetAvailable
};

/*----------------------------------------------------------------------
|    BLT_CddaTrack_AddReference
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_AddReference(ATX_ReferenceableInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    /* this is faked out */
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_CddaTrack_Release
+---------------------------------------------------------------------*/
ATX_METHOD
BLT_CddaTrack_Release(ATX_ReferenceableInstance* instance)
{
    BLT_COMPILER_UNUSED(instance);
    /* this is faked out */
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    ATX_Referenceable interface
+---------------------------------------------------------------------*/
/* we need a fake ATX_Referenceable interface so that we look like */
/* a compliant stream interface                                    */
static const ATX_ReferenceableInterface
BLT_CddaTrack_ATX_ReferenceableInterface = {
    BLT_CddaTrack_GetInterface,
    BLT_CddaTrack_AddReference,
    BLT_CddaTrack_Release
};

/*----------------------------------------------------------------------
|       interface map
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(BLT_CddaTrack)
ATX_INTERFACE_MAP_ADD(BLT_CddaTrack, ATX_Referenceable)
ATX_INTERFACE_MAP_ADD(BLT_CddaTrack, ATX_InputStream)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(BLT_CddaTrack)
