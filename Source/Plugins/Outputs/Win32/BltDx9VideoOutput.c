/*****************************************************************
|
|   Dx9 Video Output Module
|
|   (c) 2002-2008 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltDx9VideoOutput.h"
#include "BltMediaNode.h"
#include "BltMedia.h"
#include "BltPcm.h"
#include "BltCore.h"
#include "BltPacketConsumer.h"
#include "BltMediaPacket.h"
#include "BltPixels.h"
#include "BltTime.h"

#include <windows.h>
#include <d3d9.h>

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.plugins.outputs.win32.dx9")

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_DX9_VIDEO_OUTPUT_WINDOW_CLASS       TEXT("BlueTune Video")
#define BLT_DX9_VIDEO_OUTPUT_WINDOW_NAME        TEXT("BlueTune Video")
#define BLT_DX9_VIDEO_OUTPUT_PICTURE_QUEUE_SIZE 3
#define BLT_DX9_VIDEO_OUTPUT_FVF_CUSTOM_VERTEX (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef struct {
    /* base class */
    ATX_EXTENDS(BLT_BaseModule);
} Dx9VideoOutputModule;

typedef struct {
	BLT_Boolean is_vista_or_later;
} Dx9VideoOutput_PlatformInfo;

typedef struct {
    LPDIRECT3DSURFACE9 d3d_surface;
} Dx9VideoOutput_Picture;

typedef struct {
    /* base class */
    ATX_EXTENDS   (BLT_BaseMediaNode);

    /* interfaces */
    ATX_IMPLEMENTS(BLT_PacketConsumer);
    ATX_IMPLEMENTS(BLT_OutputNode);
    ATX_IMPLEMENTS(BLT_MediaPort);

    /* members */
	Dx9VideoOutput_PlatformInfo platform;
    HWND                        window;
    UINT                        window_width;
    UINT                        window_height;
    UINT                        picture_width;
    UINT                        picture_height;
    Dx9VideoOutput_Picture      pictures[BLT_DX9_VIDEO_OUTPUT_PICTURE_QUEUE_SIZE];
    D3DFORMAT                   d3d_source_format;
    D3DFORMAT                   d3d_target_format;
    HMODULE                     d3d_library;
    LPDIRECT3D9                 d3d_object;
    LPDIRECT3DDEVICE9           d3d_device;
    LPDIRECT3DTEXTURE9          d3d_texture;
    LPDIRECT3DVERTEXBUFFER9     d3d_vertex_buffer;
    BLT_MediaType               expected_media_type;
    BLT_MediaType               media_type;
    
} Dx9VideoOutput;

typedef struct
{
    FLOAT    x,y,z;   /* vertext position    */
    FLOAT    rhw;     /* eye distance        */
    D3DCOLOR dcolor;  /* diffuse color       */
    FLOAT    u, v;    /* texture coordinates */
} BLT_Dx9VideoOutput_CustomVertex;

/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(Dx9VideoOutputModule, BLT_Module)

ATX_DECLARE_INTERFACE_MAP(Dx9VideoOutput, BLT_MediaNode)
ATX_DECLARE_INTERFACE_MAP(Dx9VideoOutput, ATX_Referenceable)
ATX_DECLARE_INTERFACE_MAP(Dx9VideoOutput, BLT_OutputNode)
ATX_DECLARE_INTERFACE_MAP(Dx9VideoOutput, BLT_MediaPort)
ATX_DECLARE_INTERFACE_MAP(Dx9VideoOutput, BLT_PacketConsumer)


/*----------------------------------------------------------------------
|   forward declarations
+---------------------------------------------------------------------*/
static BLT_Result Dx9VideoOutput_Destroy(Dx9VideoOutput* self);
static BLT_Result Dx9VideoOutput_Reshape(Dx9VideoOutput* self,
                                         unsigned int    width,
                                         unsigned int    height);

/*----------------------------------------------------------------------
|   Dx9VideoOutput_GetPlatformInfo
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_GetPlatformInfo(Dx9VideoOutput_PlatformInfo* info)
{
	OSVERSIONINFO os_info;
	os_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	/* default values */
	ATX_SetMemory(info, 0, sizeof(*info));

	/* OS version */
	if (GetVersionEx(&os_info)) {
		if (os_info.dwMajorVersion > 5) {
			info->is_vista_or_later = BLT_TRUE;
		}
	} else {
		ATX_LOG_WARNING("Dx9VideoOutput::GetPlatformInfo - GetVersionEx failed");
		return BLT_FAILURE;
	}

	return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_WindowProc
+---------------------------------------------------------------------*/
static LRESULT CALLBACK 
Dx9VideoOutput_WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message) {
      case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
      }
      case WM_WINDOWPOSCHANGED: {
          ATX_LOG_FINE("Dx9VideoOutput::WindowProc - WM_WINDOWPOSCHANGED");
        break;
      }
    }

    return DefWindowProc(window, message, w_param, l_param);
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_CreateWindow
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_CreateWindow(Dx9VideoOutput* self)
{
    WNDCLASSEX window_class_info;
    HINSTANCE  module_instance = GetModuleHandle(NULL);

    self->window_width  = 720;  /* FIXME */
    self->window_height = 480;  /* FIXME */

    /* register a class for our window */
    ZeroMemory(&window_class_info, sizeof(window_class_info));
    window_class_info.cbSize        = sizeof(WNDCLASSEX);
    window_class_info.style         = 0;
    window_class_info.lpfnWndProc   = Dx9VideoOutput_WindowProc;
    window_class_info.hInstance     = module_instance;
    window_class_info.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class_info.lpszClassName = BLT_DX9_VIDEO_OUTPUT_WINDOW_CLASS;
    window_class_info.lpszMenuName  = NULL;

    if (!RegisterClassEx(&window_class_info)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::SetupWindow - RegisterClassEx failed (%d)",
                          GetLastError());
        return BLT_FAILURE;
    }

    self->window = CreateWindowEx(0,
                                  BLT_DX9_VIDEO_OUTPUT_WINDOW_CLASS,
                                  BLT_DX9_VIDEO_OUTPUT_WINDOW_NAME,
                                  WS_OVERLAPPEDWINDOW|WS_SIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 
                                  self->window_width, self->window_height,
                                  NULL,
                                  NULL,
                                  module_instance,
                                  NULL);
    if (self->window == NULL) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::SetupWindow - CreateWindowEx failed (%d)",
                          GetLastError());
        return BLT_FAILURE;
    }
    ShowWindow(self->window, SW_SHOW);

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_GetFormatName
+---------------------------------------------------------------------*/
static const char* 
Dx9VideoOutput_GetFormatName(D3DFORMAT format)
{
    switch (format) {
        case D3DFMT_UNKNOWN:      return "D3DFMT_UNKNOWN";
        case D3DFMT_R8G8B8:       return "D3DFMT_R8G8B8";
        case D3DFMT_A8R8G8B8:     return "D3DFMT_A8R8G8B8";
        case D3DFMT_X8R8G8B8:     return "D3DFMT_X8R8G8B8";
        case D3DFMT_R5G6B5:       return "D3DFMT_R5G6B5";
        case D3DFMT_X1R5G5B5:     return "D3DFMT_X1R5G5B5";
        case D3DFMT_A1R5G5B5:     return "D3DFMT_A1R5G5B5";
        case D3DFMT_A4R4G4B4:     return "D3DFMT_A4R4G4B4";
        case D3DFMT_R3G3B2:       return "D3DFMT_R3G3B2";
        case D3DFMT_A8:           return "D3DFMT_A8";
        case D3DFMT_A8R3G3B2:     return "D3DFMT_A8R3G3B2";
        case D3DFMT_X4R4G4B4:     return "D3DFMT_X4R4G4B4";
        case D3DFMT_A2B10G10R10:  return "D3DFMT_A2B10G10R10";
        case D3DFMT_A8B8G8R8:     return "D3DFMT_A8B8G8R8";
        case D3DFMT_X8B8G8R8:     return "D3DFMT_X8B8G8R8";
        case D3DFMT_G16R16:       return "D3DFMT_G16R16";
        case D3DFMT_A2R10G10B10:  return "D3DFMT_A2R10G10B10";
        case D3DFMT_A16B16G16R16: return "D3DFMT_A16B16G16R16";
        case D3DFMT_A8P8:         return "D3DFMT_A8P8";
        case D3DFMT_P8:           return "D3DFMT_P8";
        case D3DFMT_L8:           return "D3DFMT_L8";
        case D3DFMT_A8L8:         return "D3DFMT_A8L8";
        case D3DFMT_A4L4:         return "D3DFMT_A4L4";
        case D3DFMT_V8U8:         return "D3DFMT_V8U8";
        case D3DFMT_L6V5U5:       return "D3DFMT_L6V5U5";
        case D3DFMT_X8L8V8U8:     return "D3DFMT_X8L8V8U8";
        case D3DFMT_Q8W8V8U8:     return "D3DFMT_Q8W8V8U8";
        case D3DFMT_V16U16:       return "D3DFMT_V16U16";
        case D3DFMT_A2W10V10U10:  return "D3DFMT_A2W10V10U10";
        case D3DFMT_UYVY:         return "D3DFMT_UYVY";
        case D3DFMT_R8G8_B8G8:    return "D3DFMT_R8G8_B8G8";
        case D3DFMT_YUY2:         return "D3DFMT_YUY2";
        case D3DFMT_G8R8_G8B8:    return "D3DFMT_G8R8_G8B8";
        default:                  return "?";
    }
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_GetDirect3DParams
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_GetDirect3DParams(Dx9VideoOutput*        self, 
                                 D3DPRESENT_PARAMETERS* d3d_params)
{
    D3DDISPLAYMODE d3d_display_mode;
    HRESULT        result;

    /* default values */
    ZeroMemory(d3d_params, sizeof(*d3d_params));

    /* get the adapter's display mode */
    result = IDirect3D9_GetAdapterDisplayMode(self->d3d_object, D3DADAPTER_DEFAULT, &d3d_display_mode);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::GetDirect3DParams - IDirect3D9::GetAdapterDisplayMode() failed (%d)", result);
        return BLT_FAILURE;
    }
    ATX_LOG_FINE_5("Dx9VideoOutput::GetDirect3DParams - display mode size=%d*%d, format=%d:%s, refresh=%d",
                   d3d_display_mode.Width,
                   d3d_display_mode.Height,
                   d3d_display_mode.Format,
                   Dx9VideoOutput_GetFormatName(d3d_display_mode.Format),
                   d3d_display_mode.RefreshRate);

    /* setup the parameters */
    d3d_params->Flags                  = D3DPRESENTFLAG_VIDEO;
    d3d_params->Windowed               = TRUE;
    d3d_params->hDeviceWindow          = self->window;
    d3d_params->BackBufferWidth        = self->window_width;
    d3d_params->BackBufferHeight       = self->window_height;
    d3d_params->SwapEffect             = D3DSWAPEFFECT_COPY;
    d3d_params->MultiSampleType        = D3DMULTISAMPLE_NONE;
    d3d_params->PresentationInterval   = D3DPRESENT_INTERVAL_DEFAULT;
    d3d_params->BackBufferFormat       = D3DFMT_UNKNOWN ;
    d3d_params->BackBufferCount        = 1;
    d3d_params->EnableAutoDepthStencil = FALSE;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_SelectTextureFormat
+---------------------------------------------------------------------*/
static D3DFORMAT 
Dx9VideoOutput_SelectTextureFormat(Dx9VideoOutput* self)
{
    const D3DFORMAT formats[] = { D3DFMT_UYVY, D3DFMT_YUY2, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5 };
    unsigned int    format_count = sizeof(formats)/sizeof(formats[0]);
    unsigned int    i;

    /* select the first available format from the list */
    for (i=0; i<format_count; i++) {
        HRESULT   result;
        D3DFORMAT format = formats[i];
        
        /* ask the device if it can create a surface with that format */
        result = IDirect3D9_CheckDeviceFormat(self->d3d_object, 
                                              D3DADAPTER_DEFAULT,
                                              D3DDEVTYPE_HAL, 
                                              self->d3d_target_format, 
                                              0, 
                                              D3DRTYPE_SURFACE, 
                                              format);
        if (result == D3DERR_NOTAVAILABLE) continue;
        if (FAILED(result)) {
            ATX_LOG_WARNING_1("Dx9VideoOutput::SelectTextureFormat - IDirect3D9::CheckDeviceFormat failed (%d)",
                              result);
            return D3DFMT_UNKNOWN;
        }

        /* check if the device can convert from that format to the target */
        result = IDirect3D9_CheckDeviceFormatConversion(self->d3d_object,
                                                        D3DADAPTER_DEFAULT, 
                                                        D3DDEVTYPE_HAL,
                                                        format, 
                                                        self->d3d_target_format);
        if (result == D3DERR_NOTAVAILABLE) continue;
        if (FAILED(result)) {
            ATX_LOG_WARNING_1("Dx9VideoOutput::SelectTextureFormat - IDirect3D9_CheckDeviceFormatConversion::CheckDeviceFormat failed (%d)",
                              result);
            return D3DFMT_UNKNOWN;
        }

        return format;
    }

    /* not found */
    return D3DFMT_UNKNOWN;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_ResetDevice
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_ResetDevice(Dx9VideoOutput* self)
{
    /* TODO */
    (void)self;
    return BLT_ERROR_NOT_IMPLEMENTED;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_InitializeDirect3D
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_InitializeDirect3D(Dx9VideoOutput* self)
{
    D3DCAPS9              d3d_caps;
    D3DPRESENT_PARAMETERS d3d_params;
    HRESULT               hresult;
    BLT_Result            bresult;
    LPDIRECT3D9 (WINAPI *d3d_factory)(UINT);

    ATX_LOG_FINE("Dx9VideoOutput::InitializeDirect3D - starting");

    /* load the DLL manually, so we can soft-fail if it is not available */
    self->d3d_library = LoadLibrary(TEXT("D3D9.DLL"));
    if (self->d3d_library == NULL) {
        ATX_LOG_FINE("Dx9VideoOutput::InitializeDirect3D - Direct3D 9 not available");
        return BLT_ERROR_NOT_SUPPORTED;
    }

    /* obtain the factory function */
    d3d_factory = (LPDIRECT3D9 (WINAPI *)(UINT))GetProcAddress(self->d3d_library, "Direct3DCreate9");
    if (d3d_factory == NULL) {
        ATX_LOG_WARNING("Dx9VideoOutput::InitializeDirect3D - cannot find Direct3DCreate9 function in D3D9.DLL");
        return BLT_FAILURE;
    }

    /* create the Direct3D object */
    self->d3d_object = d3d_factory(D3D_SDK_VERSION);
    if (self->d3d_object == NULL) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::InitializeDirect3D - Direct3DCreate9(%d) failed", D3D_SDK_VERSION);
        return BLT_FAILURE;
    }

    /* get Direct3D info and capabilities */
    ZeroMemory(&d3d_caps, sizeof(d3d_caps));
    hresult = IDirect3D9_GetDeviceCaps(self->d3d_object, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3d_caps);
    if (FAILED(hresult)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::InitializeDirect3D - IDirect3D9::GetDeviceCaps() failed", hresult);
        return BLT_FAILURE;
    }

    /* get the Direct3D parameters to create the device with */
    bresult = Dx9VideoOutput_GetDirect3DParams(self, &d3d_params);
    if (BLT_FAILED(bresult)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::InitializeDirect3D - Dx9VideoOutput::GetDirect3DParams() failed (%d)", bresult);
        return bresult;
    }

    /* create the Direct3D device */
    hresult = IDirect3D9_CreateDevice(self->d3d_object, 
                                      D3DADAPTER_DEFAULT,
                                      D3DDEVTYPE_HAL, 
                                      self->window,
                                      D3DCREATE_SOFTWARE_VERTEXPROCESSING|
                                      D3DCREATE_MULTITHREADED,
                                      &d3d_params, 
                                      &self->d3d_device);
    if (FAILED(hresult)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::InitializeDirect3D - IDirect3D9::CreateDevice failed (%d)", hresult);
        return BLT_FAILURE;
    }
    
    /* remember the target format */
    self->d3d_target_format = d3d_params.BackBufferFormat;

    /* find the best texture format */
    self->d3d_source_format = Dx9VideoOutput_SelectTextureFormat(self);
    if (self->d3d_source_format == D3DFMT_UNKNOWN) {
        ATX_LOG_WARNING("Dx9VideoOutput::InitializeDirect3D - no compatible format found");
        return BLT_FAILURE;
    }
    ATX_LOG_FINE_2("Dx9VideoOutput::InitializeDirect3D - selected format is %d:%s",
                   self->d3d_source_format,
                   Dx9VideoOutput_GetFormatName(self->d3d_source_format));

    /* set some of the device options */
    IDirect3DDevice9_SetSamplerState(self->d3d_device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(self->d3d_device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(self->d3d_device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(self->d3d_device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_AMBIENT, D3DCOLOR_XRGB(255,255,255));
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_DITHERENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_ALPHATESTENABLE,TRUE);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_ALPHAREF, 0x10);
    IDirect3DDevice9_SetRenderState(self->d3d_device, D3DRS_ALPHAFUNC,D3DCMP_GREATER);
    IDirect3DDevice9_SetTextureStageState(self->d3d_device, 0, D3DTSS_COLOROP,D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(self->d3d_device, 0, D3DTSS_COLORARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(self->d3d_device, 0, D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(self->d3d_device, 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    /* create the vertex buffer */
    hresult = IDirect3DDevice9_CreateVertexBuffer(self->d3d_device,
        4*sizeof(BLT_Dx9VideoOutput_CustomVertex),
        D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
        BLT_DX9_VIDEO_OUTPUT_FVF_CUSTOM_VERTEX,
        D3DPOOL_DEFAULT,
        &self->d3d_vertex_buffer,
        NULL);
    if (FAILED(hresult)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::InitializeDirect3D - IDirect3DDevice9::CreateVertexBuffer failed (%d)", hresult);
        return BLT_FAILURE;
    }

    /* FIXME: temporary */
    Dx9VideoOutput_Reshape(self, 720, 480);

    ATX_LOG_FINE("Dx9VideoOutput::InitializeDirect3D - done");
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_ReleaseDirect3D
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_ReleaseDirect3D(Dx9VideoOutput* self)
{
    if (self->d3d_vertex_buffer) {
        IDirect3DVertexBuffer9_Release(self->d3d_vertex_buffer);
        self->d3d_vertex_buffer = NULL;
    }
    if (self->d3d_device) {
        IDirect3DDevice9_Release(self->d3d_device);
        self->d3d_device = NULL;
    }
    if (self->d3d_object) {
        IDirect3D9_Release(self->d3d_object);
 		self->d3d_object = NULL;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_DestroyPictures
+---------------------------------------------------------------------*/
static void 
Dx9VideoOutput_DestroyPictures(Dx9VideoOutput* self)
{
    unsigned int i;

    for (i=0; i<BLT_DX9_VIDEO_OUTPUT_PICTURE_QUEUE_SIZE; i++) {
        if (self->pictures[i].d3d_surface) {
            IDirect3DSurface9_Release(self->pictures[i].d3d_surface);
            self->pictures[i].d3d_surface = NULL;
        }
    }
    self->picture_width  = 0;
    self->picture_height = 0;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_CreatePictures
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_CreatePictures(Dx9VideoOutput* self,
                              unsigned int    width,
                              unsigned int    height)
{
    unsigned int i;

    /* create new pictures with the right size */
    for (i=0; i<BLT_DX9_VIDEO_OUTPUT_PICTURE_QUEUE_SIZE; i++) {
        HRESULT result;

        result = IDirect3DDevice9_CreateOffscreenPlainSurface(
            self->d3d_device,
            width,
            height,
            self->d3d_source_format,
            D3DPOOL_DEFAULT,
            &self->pictures[i].d3d_surface,
            NULL);
        if (FAILED(result)) {
            ATX_LOG_WARNING_1("Dx9VideoOutput::SetPictureSize - IDirect3DDevice9::CreateOffscreenPlainSurface failed (%d)", result);
            Dx9VideoOutput_DestroyPictures(self);
            return BLT_FAILURE;
        }

        /* clear the surface */
        IDirect3DDevice9_ColorFill(self->d3d_device, 
                                   self->pictures[i].d3d_surface, 
                                   NULL, 
                                   D3DCOLOR_ARGB(0xFF, 0, 0, 0));
    }

    /* remember the size */
    self->picture_width  = width;
    self->picture_height = height;

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_CreateTexture
+---------------------------------------------------------------------*/
static BLT_Result 
Dx9VideoOutput_CreateTexture(Dx9VideoOutput* self)
{
    HRESULT result;

    /* create a texture */
    result = IDirect3DDevice9_CreateTexture(self->d3d_device,
                                            self->picture_width,
                                            self->picture_height,
                                            1, /* levels */
                                            D3DUSAGE_RENDERTARGET,
                                            self->d3d_target_format,
                                            D3DPOOL_DEFAULT,
                                            &self->d3d_texture,
                                            NULL);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::CreateTexture - IDirect3DDevice9::CreateTexture failed (%d)", result);
        return BLT_FAILURE;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_DestroyTexture
+---------------------------------------------------------------------*/
static void 
Dx9VideoOutput_DestroyTexture(Dx9VideoOutput* self)
{
    if (self->d3d_texture) {
        IDirect3DTexture9_Release(self->d3d_texture);
        self->d3d_texture = NULL;
    }
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_Reshape
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_Reshape(Dx9VideoOutput* self,
                       unsigned int    width,
                       unsigned int    height)
{
    float                            f_width  = (float)(width);
    float                            f_height = (float)(height);
    BLT_Dx9VideoOutput_CustomVertex* vertices;
    HRESULT                          result;

    /* check if we have a buffer to work with */
    if (self->d3d_vertex_buffer == NULL) return BLT_SUCCESS;

    /* lock the vertices */
    result = IDirect3DVertexBuffer9_Lock(self->d3d_vertex_buffer, 0, 0, (void **)(&vertices), D3DLOCK_DISCARD);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::Reshape - IDirect3DVertexBuffer9::Lock failed (%d)", result);
        return BLT_FAILURE;
    }

    /* top-left */
    vertices[0].x       = 0.0f;
    vertices[0].y       = 0.0f;
    vertices[0].z       = 0.0f;
    vertices[0].dcolor = D3DCOLOR_ARGB(255, 255, 255, 255);
    vertices[0].rhw     = 1.0f;
    vertices[0].u       = 0.0f;
    vertices[0].v       = 0.0f;

    /* top-right */
    vertices[1].x       = f_width;
    vertices[1].y       = 0.0f;
    vertices[1].z       = 0.0f;
    vertices[1].dcolor = D3DCOLOR_ARGB(255, 255, 255, 255);
    vertices[1].rhw     = 1.0f;
    vertices[1].u       = 1.0f;
    vertices[1].v       = 0.0f;

    /* bottom-right */
    vertices[2].x       = f_width;
    vertices[2].y       = f_height;
    vertices[2].z       = 0.0f;
    vertices[2].dcolor = D3DCOLOR_ARGB(255, 255, 255, 255);
    vertices[2].rhw     = 1.0f;
    vertices[2].u       = 1.0f;
    vertices[2].v       = 1.0f;

    /* bottom-left */
    vertices[3].x       = 0.0f;
    vertices[3].y       = f_height;
    vertices[3].z       = 0.0f;
    vertices[3].dcolor = D3DCOLOR_ARGB(255, 255, 255, 255);
    vertices[3].rhw     = 1.0f;
    vertices[3].u       = 0.0f;
    vertices[3].v       = 1.0f;

    /* unlock the vertices */
    result = IDirect3DVertexBuffer9_Unlock(self->d3d_vertex_buffer);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::Reshape - IDirect3DVertexBuffer9::Lock failed (%d)", result);
        return BLT_FAILURE;
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_RenderPicture
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_RenderPicture(Dx9VideoOutput* self)
{
    Dx9VideoOutput_Picture* picture = &self->pictures[0]; /* FIXME */
    LPDIRECT3DSURFACE9      d3d_dest_surface;
    HRESULT                 result = 0;

    /* check that we have something to render */
    if (self->d3d_texture == NULL ||
        self->d3d_vertex_buffer == NULL || 
        picture->d3d_surface == NULL) {
        return BLT_SUCCESS;
    }

    /* then check if we can still use the device */
    result = IDirect3DDevice9_TestCooperativeLevel(self->d3d_device);
    if (FAILED(result)) {
        if (result == D3DERR_DEVICENOTRESET) {
            /* we need to reset the device */
            BLT_Result bresult = Dx9VideoOutput_ResetDevice(self);
            if (BLT_FAILED(result)) {
                ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - Dx9VideoOutput::ResetDevice failed (%d)", bresult);
                return bresult;
            }
        } else {
            ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::TestCooperativeLevel failed (%d)", result);
            return BLT_FAILURE;
        }
    }

    /* clear the buffers */
    result = IDirect3DDevice9_Clear(self->d3d_device, 
                                    0, 
                                    NULL, 
                                    D3DCLEAR_TARGET, 
                                    D3DCOLOR_XRGB(0, 0, 0), 
                                    1.0f, 
                                    0);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::Clear failed (%d)", result);
        return BLT_FAILURE;
    }

    /* get the destination surface to render onto */
    result = IDirect3DTexture9_GetSurfaceLevel(self->d3d_texture, 0, &d3d_dest_surface);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DTexture9::GetSurfaceLevel failed (%d)", result);
        return BLT_FAILURE;
    }

    /* blit the picture onto the destination surface */
    result = IDirect3DDevice9_StretchRect(self->d3d_device, 
                                          picture->d3d_surface, 
                                          NULL, 
                                          d3d_dest_surface, 
                                          NULL, 
                                          D3DTEXF_NONE);
    IDirect3DSurface9_Release(d3d_dest_surface);
    d3d_dest_surface = NULL;
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DTexture9::Release failed (%d)", result);
        return BLT_FAILURE;
    }

    /* begin */
    result = IDirect3DDevice9_BeginScene(self->d3d_device);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::BeginScene failed (%d)", result);
        return BLT_FAILURE;
    }

    result = IDirect3DDevice9_SetTexture(self->d3d_device, 0, (LPDIRECT3DBASETEXTURE9)self->d3d_texture);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::BeginScene failed (%d)", result);
        goto end;
    }

    result = IDirect3DDevice9_SetStreamSource(self->d3d_device, 0, self->d3d_vertex_buffer, 0, sizeof(BLT_Dx9VideoOutput_CustomVertex));
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::SetStreamSource failed (%d)", result);
        goto end;
    }

    result = IDirect3DDevice9_SetVertexShader(self->d3d_device, NULL);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::SetVertexShader failed (%d)", result);
        goto end;
    }

    result = IDirect3DDevice9_SetFVF(self->d3d_device, BLT_DX9_VIDEO_OUTPUT_FVF_CUSTOM_VERTEX);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::SetFVF failed (%d)", result);
        goto end;
    }

    result = IDirect3DDevice9_DrawPrimitive(self->d3d_device, D3DPT_TRIANGLEFAN, 0, 2);
    if (FAILED(result)) {
        ATX_LOG_WARNING_1("Dx9VideoOutput::RenderPicture - IDirect3DDevice9::DrawPrimitive failed (%d)", result);
        goto end;
    }

end:
    /* we're done with the scene */
    IDirect3DDevice9_EndScene(self->d3d_device);
    if (FAILED(result)) {
        return BLT_FAILURE;
    } else {
        return BLT_SUCCESS;
    }
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_Create
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_Create(BLT_Module*              module,
                      BLT_Core*                core, 
                      BLT_ModuleParametersType parameters_type,
                      BLT_CString              parameters, 
                      BLT_MediaNode**          object)
{
    Dx9VideoOutput*           self;
    BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;
    unsigned long             window_handle = 0;
    BLT_Result                result;

    /* check parameters */
    if (parameters == NULL || 
        parameters_type != BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR ||
        constructor->name == NULL) {
        return BLT_ERROR_INVALID_PARAMETERS;
    }
    if (ATX_StringLength(constructor->name) < 4) {
        ATX_LOG_WARNING_1("invalid name (%s)", constructor->name);
    }

    /* compute the window handle */
    result = ATX_ParseIntegerU(constructor->name+4, &window_handle, ATX_FALSE);
    if (ATX_FAILED(result)) return result;

    /* allocate memory for the object */
    self = ATX_AllocateZeroMemory(sizeof(Dx9VideoOutput));
    if (self == NULL) {
        *object = NULL;
        return BLT_ERROR_OUT_OF_MEMORY;
    }

    /* construct the inherited object */
    BLT_BaseMediaNode_Construct(&ATX_BASE(self, BLT_BaseMediaNode), module, core);

    /* setup the expected media type */
    BLT_MediaType_Init(&self->expected_media_type, BLT_MEDIA_TYPE_ID_VIDEO_RAW);

    /* get some info about the platform we are running on */
	Dx9VideoOutput_GetPlatformInfo(&self->platform);

    /* setup the window to render the video */
    if (window_handle) {
        self->window = (HWND)window_handle;
        self->window_width = 1280;
        self->window_height= 720;
    } else {
        result = Dx9VideoOutput_CreateWindow(self);
        if (BLT_FAILED(result)) goto fail;
    }

    /* initialize Direct3D */
    result = Dx9VideoOutput_InitializeDirect3D(self);
    if (BLT_FAILED(result)) goto fail;

    Dx9VideoOutput_Reshape(self, self->window_width, self->window_height);

    /* setup interfaces */
    ATX_SET_INTERFACE_EX(self, Dx9VideoOutput, BLT_BaseMediaNode, BLT_MediaNode);
    ATX_SET_INTERFACE_EX(self, Dx9VideoOutput, BLT_BaseMediaNode, ATX_Referenceable);
    ATX_SET_INTERFACE   (self, Dx9VideoOutput, BLT_PacketConsumer);
    ATX_SET_INTERFACE   (self, Dx9VideoOutput, BLT_OutputNode);
    ATX_SET_INTERFACE   (self, Dx9VideoOutput, BLT_MediaPort);
    *object = &ATX_BASE_EX(self, BLT_BaseMediaNode, BLT_MediaNode);

    return BLT_SUCCESS;

fail:
    Dx9VideoOutput_Destroy(self);   
    return result;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_Destroy
+---------------------------------------------------------------------*/
static BLT_Result
Dx9VideoOutput_Destroy(Dx9VideoOutput* self)
{
    /* close the window if we have one */

    /* destroy the pictures and texture */
    Dx9VideoOutput_DestroyPictures(self);
    Dx9VideoOutput_DestroyTexture(self);

    /* release Direct3D resources */
    Dx9VideoOutput_ReleaseDirect3D(self);

    /* destruct the inherited object */
    BLT_BaseMediaNode_Destruct(&ATX_BASE(self, BLT_BaseMediaNode));

    /* free the object memory */
    ATX_FreeMemory(self);

    /* unload the DLL */
    if (self->d3d_library) {
        FreeLibrary(self->d3d_library);
    }

    return BLT_SUCCESS;
}
                
/*----------------------------------------------------------------------
|    Dx9VideoOutput_PutPacket
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutput_PutPacket(BLT_PacketConsumer* _self,
                         BLT_MediaPacket*    packet)
{
    Dx9VideoOutput*              self = ATX_SELF(Dx9VideoOutput, BLT_PacketConsumer);
    const BLT_RawVideoMediaType* media_type;

    ATX_LOG_FINEST("Dx9VideoOutput::PutPacket");

    /* check the media type */
    BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
    if (media_type->base.id != BLT_MEDIA_TYPE_ID_VIDEO_RAW) return BLT_ERROR_INVALID_MEDIA_FORMAT;
    if (media_type->format != BLT_PIXEL_FORMAT_YV12) return BLT_ERROR_INVALID_MEDIA_FORMAT;

    if (media_type->width != self->picture_width || media_type->height != self->picture_height) {
        Dx9VideoOutput_DestroyPictures(self);
        Dx9VideoOutput_CreatePictures(self, media_type->width, media_type->height);
        Dx9VideoOutput_DestroyTexture(self);
        Dx9VideoOutput_CreateTexture(self);
    }

    if (self->pictures[0].d3d_surface) {
        HRESULT        result;
        D3DLOCKED_RECT d3d_rect;
        result = IDirect3DSurface9_LockRect(self->pictures[0].d3d_surface, &d3d_rect, NULL, 0);
        if (SUCCEEDED(result)) {
            const unsigned char* pixels = BLT_MediaPacket_GetPayloadBuffer(packet);
            const unsigned char* src_y = pixels+media_type->planes[0].offset;
            const unsigned char* src_u = pixels+media_type->planes[1].offset;
            const unsigned char* src_v = pixels+media_type->planes[2].offset;
            unsigned int row;
            unsigned int col;
        
            if (self->d3d_source_format == D3DFMT_UYVY) {
                unsigned char* dst_line = d3d_rect.pBits;
                for (row=0; row<media_type->height; row++) {
                    unsigned char* dst = dst_line;
                    for (col=0; col<(unsigned int)(media_type->width/2); col++) {
                        *dst++ = src_u[col];
                        *dst++ = src_y[2*col];
                        *dst++ = src_v[col];
                        *dst++ = src_y[2*col+1];
                    }
                    dst_line += d3d_rect.Pitch;
                    src_y += media_type->planes[0].bytes_per_line;
                    if (row&1) {
                        src_u += media_type->planes[1].bytes_per_line;
                        src_v += media_type->planes[2].bytes_per_line;
                    }
                }
            } else if (self->d3d_source_format == D3DFMT_X8R8G8B8) {
                unsigned char* dst_line = d3d_rect.pBits;
                for (row=0; row<media_type->height; row++) {
                    ATX_UInt32* dst = (ATX_UInt32*)dst_line;
                    for (col=0; col<media_type->width; col++) {
                        int r,g,b;
                        int y = src_y[col];
                        int u = src_u[col/2];
                        int v = src_v[col/2];
                        r = (int)(y + (1.370705 * (v-128)));
                        g = (int)(y - (0.698001 * (v-128)) - (0.337633 * (u-128)));
                        b = (int)(y + (1.732446 * (u-128)));
                        if (r > 255) r = 255;
                        if (g > 255) g = 255;
                        if (b > 255) b = 255;
                        if (r < 0) r = 0;
                        if (g < 0) g = 0;
                        if (b < 0) b = 0;

                        *dst++ = r<<16 | g<<8 | b;
                    }
                    dst_line += d3d_rect.Pitch;
                    src_y += media_type->planes[0].bytes_per_line;
                    if (row&1) {
                        src_u += media_type->planes[1].bytes_per_line;
                        src_v += media_type->planes[2].bytes_per_line;
                    }
                }
            }
        }

        result = IDirect3DSurface9_UnlockRect(self->pictures[0].d3d_surface);
        Dx9VideoOutput_RenderPicture(self);
        result = IDirect3DDevice9_Present(self->d3d_device, NULL, NULL, NULL, NULL);
    }

    /* FIXME: temporary */
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    /* queue the picture */
    {
        ATX_TimeInterval sleep = {0,10000000};
        ATX_System_Sleep(&sleep);
    }

    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_QueryMediaType
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutput_QueryMediaType(BLT_MediaPort*        _self,
                              BLT_Ordinal           index,
                              const BLT_MediaType** media_type)
{
    Dx9VideoOutput* self = ATX_SELF(Dx9VideoOutput, BLT_MediaPort);

    if (index == 0) {
        *media_type = (const BLT_MediaType*)&self->expected_media_type;
        return BLT_SUCCESS;
    } else {
        *media_type = NULL;
        return BLT_FAILURE;
    }
}
/*----------------------------------------------------------------------
|   Dx9VideoOutput_GetPortByName
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutput_GetPortByName(BLT_MediaNode*  _self,
                             BLT_CString     name,
                             BLT_MediaPort** port)
{
    Dx9VideoOutput* self = ATX_SELF_EX(Dx9VideoOutput, BLT_BaseMediaNode, BLT_MediaNode);

    if (ATX_StringsEqual(name, "input")) {
        *port = &ATX_BASE(self, BLT_MediaPort);
        return BLT_SUCCESS;
    } else {
        *port = NULL;
        return BLT_ERROR_NO_SUCH_PORT;
    }
}

/*----------------------------------------------------------------------
|   Dx9VideoOutput_Start
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutput_Start(BLT_MediaNode* _self)
{
    Dx9VideoOutput* self = ATX_SELF_EX(Dx9VideoOutput, BLT_BaseMediaNode, BLT_MediaNode);
    (void)self;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   Dx9VideoOutput_Stop
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutput_Stop(BLT_MediaNode*  _self)
{
    Dx9VideoOutput* self = ATX_SELF_EX(Dx9VideoOutput, BLT_BaseMediaNode, BLT_MediaNode);
    (void)self;
    return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|    Dx9VideoOutput_GetStatus
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutput_GetStatus(BLT_OutputNode*       _self,
                         BLT_OutputNodeStatus* status)
{
    Dx9VideoOutput* self = ATX_SELF(Dx9VideoOutput, BLT_OutputNode);
    BLT_COMPILER_UNUSED(self);
    
    /* default value */
    status->media_time.seconds     = 0;
    status->media_time.nanoseconds = 0;

    status->flags = 0;

	return BLT_SUCCESS;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Dx9VideoOutput)
    ATX_GET_INTERFACE_ACCEPT_EX(Dx9VideoOutput, BLT_BaseMediaNode, BLT_MediaNode)
    ATX_GET_INTERFACE_ACCEPT_EX(Dx9VideoOutput, BLT_BaseMediaNode, ATX_Referenceable)
    ATX_GET_INTERFACE_ACCEPT   (Dx9VideoOutput, BLT_OutputNode)
    ATX_GET_INTERFACE_ACCEPT   (Dx9VideoOutput, BLT_MediaPort)
    ATX_GET_INTERFACE_ACCEPT   (Dx9VideoOutput, BLT_PacketConsumer)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|    BLT_MediaPort interface
+---------------------------------------------------------------------*/
BLT_MEDIA_PORT_IMPLEMENT_SIMPLE_TEMPLATE(Dx9VideoOutput, "input", PACKET, IN)
ATX_BEGIN_INTERFACE_MAP(Dx9VideoOutput, BLT_MediaPort)
    Dx9VideoOutput_GetName,
    Dx9VideoOutput_GetProtocol,
    Dx9VideoOutput_GetDirection,
    Dx9VideoOutput_QueryMediaType
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_PacketConsumer interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Dx9VideoOutput, BLT_PacketConsumer)
    Dx9VideoOutput_PutPacket
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|    BLT_MediaNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(Dx9VideoOutput, BLT_BaseMediaNode, BLT_MediaNode)
    BLT_BaseMediaNode_GetInfo,
    Dx9VideoOutput_GetPortByName,
    BLT_BaseMediaNode_Activate,
    BLT_BaseMediaNode_Deactivate,
    Dx9VideoOutput_Start,
    Dx9VideoOutput_Stop,
    BLT_BaseMediaNode_Pause,
    BLT_BaseMediaNode_Resume,
    BLT_BaseMediaNode_Seek
ATX_END_INTERFACE_MAP_EX

/*----------------------------------------------------------------------
|    BLT_OutputNode interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(Dx9VideoOutput, BLT_OutputNode)
    Dx9VideoOutput_GetStatus
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(Dx9VideoOutput, 
                                         BLT_BaseMediaNode, 
                                         reference_count)

/*----------------------------------------------------------------------
|   Dx9VideoOutputModule_Probe
+---------------------------------------------------------------------*/
BLT_METHOD
Dx9VideoOutputModule_Probe(BLT_Module*              self, 
                           BLT_Core*                core,
                           BLT_ModuleParametersType parameters_type,
                           BLT_AnyConst             parameters,
                           BLT_Cardinal*            match)
{
    BLT_COMPILER_UNUSED(self);
    BLT_COMPILER_UNUSED(core);

    switch (parameters_type) {
      case BLT_MODULE_PARAMETERS_TYPE_MEDIA_NODE_CONSTRUCTOR: {
        BLT_MediaNodeConstructor* constructor = (BLT_MediaNodeConstructor*)parameters;

        /* the input protocol should be PACKET and the */
        /* output protocol should be NONE              */
        if ((constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_ANY &&
             constructor->spec.input.protocol  != BLT_MEDIA_PORT_PROTOCOL_PACKET) ||
            (constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_ANY &&
             constructor->spec.output.protocol != BLT_MEDIA_PORT_PROTOCOL_NONE)) {
            return BLT_FAILURE;
        }

        /* the input type should be unknown, or video/raw */
        if (!(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_VIDEO_RAW) &&
            !(constructor->spec.input.media_type->id == BLT_MEDIA_TYPE_ID_UNKNOWN)) {
            return BLT_FAILURE;
        }

        /* the name should be 'dx9:<n>' */
        if (constructor->name == NULL || 
            !ATX_StringsEqualN(constructor->name, "dx9:", 4)) {
            return BLT_FAILURE;
        }

        /* always an exact match, since we only respond to our name */
        *match = BLT_MODULE_PROBE_MATCH_EXACT;

        return BLT_SUCCESS;
      }    
      break;

      default:
        break;
    }

    return BLT_FAILURE;
}

/*----------------------------------------------------------------------
|   GetInterface implementation
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(Dx9VideoOutputModule)
    ATX_GET_INTERFACE_ACCEPT_EX(Dx9VideoOutputModule, BLT_BaseModule, BLT_Module)
    ATX_GET_INTERFACE_ACCEPT_EX(Dx9VideoOutputModule, BLT_BaseModule, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION

/*----------------------------------------------------------------------
|   node factory
+---------------------------------------------------------------------*/
BLT_MODULE_IMPLEMENT_SIMPLE_MEDIA_NODE_FACTORY(Dx9VideoOutputModule, Dx9VideoOutput)

/*----------------------------------------------------------------------
|   BLT_Module interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP_EX(Dx9VideoOutputModule, BLT_BaseModule, BLT_Module)
    BLT_BaseModule_GetInfo,
    BLT_BaseModule_Attach,
    Dx9VideoOutputModule_CreateInstance,
    Dx9VideoOutputModule_Probe
ATX_END_INTERFACE_MAP

/*----------------------------------------------------------------------
|   ATX_Referenceable interface
+---------------------------------------------------------------------*/
#define Dx9VideoOutputModule_Destroy(x) \
    BLT_BaseModule_Destroy((BLT_BaseModule*)(x))

ATX_IMPLEMENT_REFERENCEABLE_INTERFACE_EX(Dx9VideoOutputModule, 
                                         BLT_BaseModule,
                                         reference_count)

/*----------------------------------------------------------------------
|   module object
+---------------------------------------------------------------------*/
BLT_Result 
BLT_Dx9VideoOutputModule_GetModuleObject(BLT_Module** object)
{
    if (object == NULL) return BLT_ERROR_INVALID_PARAMETERS;

    return BLT_BaseModule_Create("Dx9 Video Output", NULL, 0, 
                                 &Dx9VideoOutputModule_BLT_ModuleInterface,
                                 &Dx9VideoOutputModule_ATX_ReferenceableInterface,
                                 object);
}
