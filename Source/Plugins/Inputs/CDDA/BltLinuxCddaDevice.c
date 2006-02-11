/*****************************************************************
|
|      Cdda: BltLinuxCddaDevice.c
|
|      Linux Cdda Device Module
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <sys/types.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "Atomix.h"
#include "BltConfig.h"
#include "BltTypes.h"
#include "BltCddaDevice.h"
#include "BltErrors.h"
#include "BltDebug.h"

/*----------------------------------------------------------------------
|       types
+---------------------------------------------------------------------*/
typedef struct {
    int                     fd;
    BLT_CddaTableOfContents toc;
} LinuxCddaDevice;

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_SIMPLE_GET_INTERFACE_IMPLEMENTATION(LinuxCddaDevice)
static const BLT_CddaDeviceInterface LinuxCddaDevice_BLT_CddaDeviceInterface;

/*----------------------------------------------------------------------
|       LinuxCddaDevice_ReadTableOfContents
+---------------------------------------------------------------------*/
static BLT_Result
LinuxCddaDevice_ReadTableOfContents(LinuxCddaDevice* device)
{
    int                   io_result;
    struct cdrom_tochdr   toc_header;
    struct cdrom_tocentry toc_entry;
    BLT_CddaTrackInfo*    track_info;
    BLT_Ordinal           index;

    /* get the toc header */
    io_result = ioctl(device->fd, CDROMREADTOCHDR, &toc_header);
    BLT_Debug("CDROMREADTOCHDR return %d, errno=%s\n", io_result, strerror(errno));
    if (io_result != 0) return BLT_FAILURE;
    device->toc.first_track_index = toc_header.cdth_trk0;
    device->toc.last_track_index = toc_header.cdth_trk1;
    device->toc.track_count = 1 + toc_header.cdth_trk1 - toc_header.cdth_trk0;

    BLT_Debug("LinuxCddaDevice_ReadTableOfContents: first=%d, last=%d\n",
              device->toc.first_track_index, device->toc.last_track_index);

    /* allocate memory for the track infos */
    device->toc.tracks = 
        (BLT_CddaTrackInfo*)ATX_AllocateZeroMemory(sizeof(BLT_CddaTrackInfo) *
                                                   device->toc.track_count);
    if (device->toc.tracks == NULL) return BLT_ERROR_OUT_OF_MEMORY;

    /* get info for each track */
    track_info = device->toc.tracks;
    for (index  = device->toc.first_track_index; 
         index <= device->toc.last_track_index;
         index++, track_info++) {
        ATX_SetMemory(&toc_entry, 0, sizeof(toc_entry));
        toc_entry.cdte_track  = index;
        toc_entry.cdte_format = CDROM_LBA; 
        io_result = ioctl(device->fd, CDROMREADTOCENTRY, &toc_entry);
        if (io_result != 0) return BLT_FAILURE;
        track_info->index = index;
        track_info->address = toc_entry.cdte_addr.lba;
        track_info->type = toc_entry.cdte_ctrl & CDROM_DATA_TRACK ?
            BLT_CDDA_TRACK_TYPE_DATA : BLT_CDDA_TRACK_TYPE_AUDIO;

        /* compute the duration of the previous track */
        if (index != device->toc.first_track_index) {
            track_info[-1].duration.frames = 
                track_info[0].address - track_info[-1].address;
            BLT_Cdda_FramesToMsf(track_info[-1].duration.frames, 
                                 &track_info[-1].duration.msf);
        }
    }

    /* get info for the leadout track to compute last track's duration */
    toc_entry.cdte_track  = CDROM_LEADOUT;
    toc_entry.cdte_format = CDROM_LBA; 
    io_result = ioctl(device->fd, CDROMREADTOCENTRY, &toc_entry);
    if (io_result != 0) return BLT_FAILURE;
    track_info[-1].duration.frames = 
        toc_entry.cdte_addr.lba - track_info[-1].address;
    BLT_Cdda_FramesToMsf(track_info[-1].duration.frames,
                         &track_info[-1].duration.msf);

    track_info = device->toc.tracks;
    for (index  = device->toc.first_track_index; 
         index <= device->toc.last_track_index;
         index++, track_info++) {
        BLT_Debug("track %02d: (%c) addr = %08ld, duration = %08ld [%02d:%02d:%02d]\n",
                  track_info->index,
                  track_info->type == BLT_CDDA_TRACK_TYPE_AUDIO ? 'A' : 'D',
                  track_info->address,
                  track_info->duration.frames,
                  track_info->duration.msf.m,
                  track_info->duration.msf.s,
                  track_info->duration.msf.f);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       LinuxCddaDevice_Create
+---------------------------------------------------------------------*/
static BLT_Result
LinuxCddaDevice_Create(BLT_String name, BLT_CddaDevice* object)
{
    LinuxCddaDevice* device;
    BLT_Result       result;

    BLT_COMPILER_UNUSED(name);

    /* allocate memory for the object */
    device = (LinuxCddaDevice*)ATX_AllocateZeroMemory(sizeof(LinuxCddaDevice));
    if (device == NULL) {
        ATX_CLEAR_OBJECT(object);
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* initialize the object */
    device->fd = open("/dev/cdrom", O_RDONLY | O_NONBLOCK);
    if (device->fd < 0) {
        BLT_Debug("open return %d, errno=%s\n", device->fd, strerror(errno));
        if (errno == ENOENT) {
            result = BLT_ERROR_NO_SUCH_DEVICE;
        } else if (errno == EACCES) {
            result = BLT_ERROR_ACCESS_DENIED;
        } else if (errno == ENOMEDIUM) {
            result = BLT_ERROR_NO_MEDIUM;
        } else {
            result = BLT_FAILURE;
        }
        goto failure;
    }

    /* read the toc */
    result = LinuxCddaDevice_ReadTableOfContents(device);
    if (BLT_FAILED(result)) goto failure;

    /* construct the object reference */
    ATX_INSTANCE(object) = (BLT_CddaDeviceInstance*)device;
    ATX_INTERFACE(object) = &LinuxCddaDevice_BLT_CddaDeviceInterface;

    return BLT_SUCCESS;

failure:
    if (device->fd >= 0) close(device->fd);
    ATX_FreeMemory((void*)device);
    ATX_CLEAR_OBJECT(object);
    return result;
}

/*----------------------------------------------------------------------
|       LinuxCddaDevice_Destroy
+---------------------------------------------------------------------*/
BLT_METHOD
LinuxCddaDevice_Destroy(ATX_DestroyableInstance* instance)
{
    LinuxCddaDevice* device = (LinuxCddaDevice*)instance;

    /* close the device */
    if (device->fd != -1) {
        close(device->fd);
    }

    /* release the toc memory */
    if (device->toc.tracks != NULL) {
        ATX_FreeMemory(device->toc.tracks);
    }

    /* free the memory */
    ATX_FreeMemory((void*)device);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       LinuxCddaDevice_GetTrackInfo
+---------------------------------------------------------------------*/
BLT_METHOD
LinuxCddaDevice_GetTrackInfo(BLT_CddaDeviceInstance* instance,  
                             BLT_Ordinal             index,
                             BLT_CddaTrackInfo*      info)
{
    LinuxCddaDevice* device = (LinuxCddaDevice*)instance;

    /* check that the track is within range */
    if (index < device->toc.first_track_index ||
        index > device->toc.last_track_index) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }

    /* return the info */
    *info = device->toc.tracks[index-device->toc.first_track_index];

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|       LinuxCddaDevice_GetTableOfContents
+---------------------------------------------------------------------*/
BLT_METHOD
LinuxCddaDevice_GetTableOfContents(BLT_CddaDeviceInstance*   instance, 
                                   BLT_CddaTableOfContents** toc)
{
    LinuxCddaDevice* device = (LinuxCddaDevice*)instance;
    *toc = &device->toc;
    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|       LinuxCddaDevice_ReadFrames
+---------------------------------------------------------------------*/
BLT_METHOD
LinuxCddaDevice_ReadFrames(BLT_CddaDeviceInstance* instance,
                           BLT_CddaLba             addr,
                           BLT_Cardinal            count,
                           BLT_Any                 buffer)
{
    LinuxCddaDevice*        device = (LinuxCddaDevice*)instance;
    struct cdrom_read_audio read_audio_cmd;
    int                     io_result;

    /* read audio from the device */
    read_audio_cmd.addr.lba    = addr;
    read_audio_cmd.addr_format = CDROM_LBA;
    read_audio_cmd.nframes     = count;
    read_audio_cmd.buf         = (unsigned char*)buffer;
    io_result = ioctl(device->fd, CDROMREADAUDIO, &read_audio_cmd);
    if (io_result != 0) return BLT_FAILURE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    BLT_CddaDevice interface
+---------------------------------------------------------------------*/
static const BLT_CddaDeviceInterface
LinuxCddaDevice_BLT_CddaDeviceInterface = {
    LinuxCddaDevice_GetInterface,
    LinuxCddaDevice_GetTrackInfo,
    LinuxCddaDevice_GetTableOfContents,
    LinuxCddaDevice_ReadFrames
};

/*----------------------------------------------------------------------
|       ATX_Destroyable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_SIMPLE_DESTROYABLE_INTERFACE(LinuxCddaDevice)

/*----------------------------------------------------------------------
|       interface map
+---------------------------------------------------------------------*/
ATX_BEGIN_SIMPLE_GET_INTERFACE_IMPLEMENTATION(LinuxCddaDevice)
ATX_INTERFACE_MAP_ADD(LinuxCddaDevice, BLT_CddaDevice)
ATX_INTERFACE_MAP_ADD(LinuxCddaDevice, ATX_Destroyable)
ATX_END_SIMPLE_GET_INTERFACE_IMPLEMENTATION(LinuxCddaDevice)

/*----------------------------------------------------------------------
|       BLT_CddaDevice_Create
+---------------------------------------------------------------------*/
BLT_Result 
BLT_CddaDevice_Create(BLT_String name, BLT_CddaDevice* device)
{
    return LinuxCddaDevice_Create(name, device);
}
