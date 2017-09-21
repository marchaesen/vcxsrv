/* Auto-generated file, do not edit */
#include "errors.h"
#include <string.h>

static const struct static_extension_info_t extension_BigRequests_info = { // BIG-REQUESTS
	.num_minor = 1,
	.strings_minor = "Enable\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "BigRequests",
};

static const struct static_extension_info_t extension_Composite_info = { // Composite
	.num_minor = 9,
	.strings_minor = "QueryVersion\0RedirectWindow\0RedirectSubwindows\0UnredirectWindow\0UnredirectSubwindows\0CreateRegionFromBorderClip\0NameWindowPixmap\0GetOverlayWindow\0ReleaseOverlayWindow\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Composite",
};

static const struct static_extension_info_t extension_Damage_info = { // DAMAGE
	.num_minor = 5,
	.strings_minor = "QueryVersion\0Create\0Destroy\0Subtract\0Add\0",
	.num_events = 1,
	.strings_events = "Notify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 1,
	.strings_errors = "BadDamage\0",
	.name = "Damage",
};

static const struct static_extension_info_t extension_DPMS_info = { // DPMS
	.num_minor = 8,
	.strings_minor = "GetVersion\0Capable\0GetTimeouts\0SetTimeouts\0Enable\0Disable\0ForceLevel\0Info\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "DPMS",
};

static const struct static_extension_info_t extension_DRI2_info = { // DRI2
	.num_minor = 14,
	.strings_minor = "QueryVersion\0Connect\0Authenticate\0CreateDrawable\0DestroyDrawable\0GetBuffers\0CopyRegion\0GetBuffersWithFormat\0SwapBuffers\0GetMSC\0WaitMSC\0WaitSBC\0SwapInterval\0GetParam\0",
	.num_events = 2,
	.strings_events = "BufferSwapComplete\0InvalidateBuffers\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "DRI2",
};

static const struct static_extension_info_t extension_DRI3_info = { // DRI3
	.num_minor = 6,
	.strings_minor = "QueryVersion\0Open\0PixmapFromBuffer\0BufferFromPixmap\0FenceFromFD\0FDFromFence\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "DRI3",
};

static const struct static_extension_info_t extension_GenericEvent_info = { // Generic Event Extension
	.num_minor = 1,
	.strings_minor = "QueryVersion\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "GenericEvent",
};

static const struct static_extension_info_t extension_Glx_info = { // GLX
	.num_minor = 167,
	.strings_minor = "Unknown (0)\0Render\0RenderLarge\0CreateContext\0DestroyContext\0MakeCurrent\0IsDirect\0QueryVersion\0WaitGL\0WaitX\0CopyContext\0SwapBuffers\0UseXFont\0CreateGLXPixmap\0GetVisualConfigs\0DestroyGLXPixmap\0VendorPrivate\0VendorPrivateWithReply\0QueryExtensionsString\0QueryServerString\0ClientInfo\0GetFBConfigs\0CreatePixmap\0DestroyPixmap\0CreateNewContext\0QueryContext\0MakeContextCurrent\0CreatePbuffer\0DestroyPbuffer\0GetDrawableAttributes\0ChangeDrawableAttributes\0CreateWindow\0DeleteWindow\0SetClientInfoARB\0CreateContextAttribsARB\0SetClientInfo2ARB\0Unknown (36)\0Unknown (37)\0Unknown (38)\0Unknown (39)\0Unknown (40)\0Unknown (41)\0Unknown (42)\0Unknown (43)\0Unknown (44)\0Unknown (45)\0Unknown (46)\0Unknown (47)\0Unknown (48)\0Unknown (49)\0Unknown (50)\0Unknown (51)\0Unknown (52)\0Unknown (53)\0Unknown (54)\0Unknown (55)\0Unknown (56)\0Unknown (57)\0Unknown (58)\0Unknown (59)\0Unknown (60)\0Unknown (61)\0Unknown (62)\0Unknown (63)\0Unknown (64)\0Unknown (65)\0Unknown (66)\0Unknown (67)\0Unknown (68)\0Unknown (69)\0Unknown (70)\0Unknown (71)\0Unknown (72)\0Unknown (73)\0Unknown (74)\0Unknown (75)\0Unknown (76)\0Unknown (77)\0Unknown (78)\0Unknown (79)\0Unknown (80)\0Unknown (81)\0Unknown (82)\0Unknown (83)\0Unknown (84)\0Unknown (85)\0Unknown (86)\0Unknown (87)\0Unknown (88)\0Unknown (89)\0Unknown (90)\0Unknown (91)\0Unknown (92)\0Unknown (93)\0Unknown (94)\0Unknown (95)\0Unknown (96)\0Unknown (97)\0Unknown (98)\0Unknown (99)\0Unknown (100)\0NewList\0EndList\0DeleteLists\0GenLists\0FeedbackBuffer\0SelectBuffer\0RenderMode\0Finish\0PixelStoref\0PixelStorei\0ReadPixels\0GetBooleanv\0GetClipPlane\0GetDoublev\0GetError\0GetFloatv\0GetIntegerv\0GetLightfv\0GetLightiv\0GetMapdv\0GetMapfv\0GetMapiv\0GetMaterialfv\0GetMaterialiv\0GetPixelMapfv\0GetPixelMapuiv\0GetPixelMapusv\0GetPolygonStipple\0GetString\0GetTexEnvfv\0GetTexEnviv\0GetTexGendv\0GetTexGenfv\0GetTexGeniv\0GetTexImage\0GetTexParameterfv\0GetTexParameteriv\0GetTexLevelParameterfv\0GetTexLevelParameteriv\0Unknown (140)\0IsList\0Flush\0AreTexturesResident\0DeleteTextures\0GenTextures\0IsTexture\0GetColorTable\0GetColorTableParameterfv\0GetColorTableParameteriv\0GetConvolutionFilter\0GetConvolutionParameterfv\0GetConvolutionParameteriv\0GetSeparableFilter\0GetHistogram\0GetHistogramParameterfv\0GetHistogramParameteriv\0GetMinmax\0GetMinmaxParameterfv\0GetMinmaxParameteriv\0GetCompressedTexImageARB\0DeleteQueriesARB\0GenQueriesARB\0IsQueryARB\0GetQueryivARB\0GetQueryObjectivARB\0GetQueryObjectuivARB\0",
	.num_events = 2,
	.strings_events = "PbufferClobber\0BufferSwapComplete\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 14,
	.strings_errors = "BadContext\0BadContextState\0BadDrawable\0BadPixmap\0BadContextTag\0BadCurrentWindow\0BadRenderRequest\0BadLargeRequest\0UnsupportedPrivateRequest\0BadFBConfig\0BadPbuffer\0BadCurrentDrawable\0BadWindow\0GLXBadProfileARB\0",
	.name = "Glx",
};

static const struct static_extension_info_t extension_Present_info = { // Present
	.num_minor = 5,
	.strings_minor = "QueryVersion\0Pixmap\0NotifyMSC\0SelectInput\0QueryCapabilities\0",
	.num_events = 1,
	.strings_events = "Generic\0",
	.num_xge_events = 4,
	.strings_xge_events = "ConfigureNotify\0CompleteNotify\0IdleNotify\0RedirectNotify\0",
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Present",
};

static const struct static_extension_info_t extension_RandR_info = { // RANDR
	.num_minor = 42,
	.strings_minor = "QueryVersion\0Unknown (1)\0SetScreenConfig\0Unknown (3)\0SelectInput\0GetScreenInfo\0GetScreenSizeRange\0SetScreenSize\0GetScreenResources\0GetOutputInfo\0ListOutputProperties\0QueryOutputProperty\0ConfigureOutputProperty\0ChangeOutputProperty\0DeleteOutputProperty\0GetOutputProperty\0CreateMode\0DestroyMode\0AddOutputMode\0DeleteOutputMode\0GetCrtcInfo\0SetCrtcConfig\0GetCrtcGammaSize\0GetCrtcGamma\0SetCrtcGamma\0GetScreenResourcesCurrent\0SetCrtcTransform\0GetCrtcTransform\0GetPanning\0SetPanning\0SetOutputPrimary\0GetOutputPrimary\0GetProviders\0GetProviderInfo\0SetProviderOffloadSink\0SetProviderOutputSource\0ListProviderProperties\0QueryProviderProperty\0ConfigureProviderProperty\0ChangeProviderProperty\0DeleteProviderProperty\0GetProviderProperty\0",
	.num_events = 2,
	.strings_events = "ScreenChangeNotify\0Notify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 4,
	.strings_errors = "BadOutput\0BadCrtc\0BadMode\0BadProvider\0",
	.name = "RandR",
};

static const struct static_extension_info_t extension_Record_info = { // RECORD
	.num_minor = 8,
	.strings_minor = "QueryVersion\0CreateContext\0RegisterClients\0UnregisterClients\0GetContext\0EnableContext\0DisableContext\0FreeContext\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 1,
	.strings_errors = "BadContext\0",
	.name = "Record",
};

static const struct static_extension_info_t extension_Render_info = { // RENDER
	.num_minor = 37,
	.strings_minor = "QueryVersion\0QueryPictFormats\0QueryPictIndexValues\0Unknown (3)\0CreatePicture\0ChangePicture\0SetPictureClipRectangles\0FreePicture\0Composite\0Unknown (9)\0Trapezoids\0Triangles\0TriStrip\0TriFan\0Unknown (14)\0Unknown (15)\0Unknown (16)\0CreateGlyphSet\0ReferenceGlyphSet\0FreeGlyphSet\0AddGlyphs\0Unknown (21)\0FreeGlyphs\0CompositeGlyphs8\0CompositeGlyphs16\0CompositeGlyphs32\0FillRectangles\0CreateCursor\0SetPictureTransform\0QueryFilters\0SetPictureFilter\0CreateAnimCursor\0AddTraps\0CreateSolidFill\0CreateLinearGradient\0CreateRadialGradient\0CreateConicalGradient\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 5,
	.strings_errors = "PictFormat\0Picture\0PictOp\0GlyphSet\0Glyph\0",
	.name = "Render",
};

static const struct static_extension_info_t extension_Res_info = { // X-Resource
	.num_minor = 6,
	.strings_minor = "QueryVersion\0QueryClients\0QueryClientResources\0QueryClientPixmapBytes\0QueryClientIds\0QueryResourceBytes\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Res",
};

static const struct static_extension_info_t extension_ScreenSaver_info = { // MIT-SCREEN-SAVER
	.num_minor = 6,
	.strings_minor = "QueryVersion\0QueryInfo\0SelectInput\0SetAttributes\0UnsetAttributes\0Suspend\0",
	.num_events = 1,
	.strings_events = "Notify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "ScreenSaver",
};

static const struct static_extension_info_t extension_Shape_info = { // SHAPE
	.num_minor = 9,
	.strings_minor = "QueryVersion\0Rectangles\0Mask\0Combine\0Offset\0QueryExtents\0SelectInput\0InputSelected\0GetRectangles\0",
	.num_events = 1,
	.strings_events = "Notify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Shape",
};

static const struct static_extension_info_t extension_Shm_info = { // MIT-SHM
	.num_minor = 8,
	.strings_minor = "QueryVersion\0Attach\0Detach\0PutImage\0GetImage\0CreatePixmap\0AttachFd\0CreateSegment\0",
	.num_events = 1,
	.strings_events = "Completion\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 1,
	.strings_errors = "BadSeg\0",
	.name = "Shm",
};

static const struct static_extension_info_t extension_Sync_info = { // SYNC
	.num_minor = 20,
	.strings_minor = "Initialize\0ListSystemCounters\0CreateCounter\0SetCounter\0ChangeCounter\0QueryCounter\0DestroyCounter\0Await\0CreateAlarm\0ChangeAlarm\0QueryAlarm\0DestroyAlarm\0SetPriority\0GetPriority\0CreateFence\0TriggerFence\0ResetFence\0DestroyFence\0QueryFence\0AwaitFence\0",
	.num_events = 2,
	.strings_events = "CounterNotify\0AlarmNotify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 2,
	.strings_errors = "Counter\0Alarm\0",
	.name = "Sync",
};

static const struct static_extension_info_t extension_XCMisc_info = { // XC-MISC
	.num_minor = 3,
	.strings_minor = "GetVersion\0GetXIDRange\0GetXIDList\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "XCMisc",
};

static const struct static_extension_info_t extension_Xevie_info = { // XEVIE
	.num_minor = 5,
	.strings_minor = "QueryVersion\0Start\0End\0Send\0SelectInput\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Xevie",
};

static const struct static_extension_info_t extension_XF86Dri_info = { // XFree86-DRI
	.num_minor = 12,
	.strings_minor = "QueryVersion\0QueryDirectRenderingCapable\0OpenConnection\0CloseConnection\0GetClientDriverName\0CreateContext\0DestroyContext\0CreateDrawable\0DestroyDrawable\0GetDrawableInfo\0GetDeviceInfo\0AuthConnection\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "XF86Dri",
};

static const struct static_extension_info_t extension_XF86VidMode_info = { // XFree86-VidModeExtension
	.num_minor = 21,
	.strings_minor = "QueryVersion\0GetModeLine\0ModModeLine\0SwitchMode\0GetMonitor\0LockModeSwitch\0GetAllModeLines\0AddModeLine\0DeleteModeLine\0ValidateModeLine\0SwitchToMode\0GetViewPort\0SetViewPort\0GetDotClocks\0SetClientVersion\0SetGamma\0GetGamma\0GetGammaRamp\0SetGammaRamp\0GetGammaRampSize\0GetPermissions\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 7,
	.strings_errors = "BadClock\0BadHTimings\0BadVTimings\0ModeUnsuitable\0ExtensionDisabled\0ClientNotLocal\0ZoomLocked\0",
	.name = "XF86VidMode",
};

static const struct static_extension_info_t extension_XFixes_info = { // XFIXES
	.num_minor = 33,
	.strings_minor = "QueryVersion\0ChangeSaveSet\0SelectSelectionInput\0SelectCursorInput\0GetCursorImage\0CreateRegion\0CreateRegionFromBitmap\0CreateRegionFromWindow\0CreateRegionFromGC\0CreateRegionFromPicture\0DestroyRegion\0SetRegion\0CopyRegion\0UnionRegion\0IntersectRegion\0SubtractRegion\0InvertRegion\0TranslateRegion\0RegionExtents\0FetchRegion\0SetGCClipRegion\0SetWindowShapeRegion\0SetPictureClipRegion\0SetCursorName\0GetCursorName\0GetCursorImageAndName\0ChangeCursor\0ChangeCursorByName\0ExpandRegion\0HideCursor\0ShowCursor\0CreatePointerBarrier\0DeletePointerBarrier\0",
	.num_events = 2,
	.strings_events = "SelectionNotify\0CursorNotify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 1,
	.strings_errors = "BadRegion\0",
	.name = "XFixes",
};

static const struct static_extension_info_t extension_Xinerama_info = { // XINERAMA
	.num_minor = 6,
	.strings_minor = "QueryVersion\0GetState\0GetScreenCount\0GetScreenSize\0IsActive\0QueryScreens\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Xinerama",
};

static const struct static_extension_info_t extension_Input_info = { // XInputExtension
	.num_minor = 62,
	.strings_minor = "Unknown (0)\0GetExtensionVersion\0ListInputDevices\0OpenDevice\0CloseDevice\0SetDeviceMode\0SelectExtensionEvent\0GetSelectedExtensionEvents\0ChangeDeviceDontPropagateList\0GetDeviceDontPropagateList\0GetDeviceMotionEvents\0ChangeKeyboardDevice\0ChangePointerDevice\0GrabDevice\0UngrabDevice\0GrabDeviceKey\0UngrabDeviceKey\0GrabDeviceButton\0UngrabDeviceButton\0AllowDeviceEvents\0GetDeviceFocus\0SetDeviceFocus\0GetFeedbackControl\0ChangeFeedbackControl\0GetDeviceKeyMapping\0ChangeDeviceKeyMapping\0GetDeviceModifierMapping\0SetDeviceModifierMapping\0GetDeviceButtonMapping\0SetDeviceButtonMapping\0QueryDeviceState\0SendExtensionEvent\0DeviceBell\0SetDeviceValuators\0GetDeviceControl\0ChangeDeviceControl\0ListDeviceProperties\0ChangeDeviceProperty\0DeleteDeviceProperty\0GetDeviceProperty\0XIQueryPointer\0XIWarpPointer\0XIChangeCursor\0XIChangeHierarchy\0XISetClientPointer\0XIGetClientPointer\0XISelectEvents\0XIQueryVersion\0XIQueryDevice\0XISetFocus\0XIGetFocus\0XIGrabDevice\0XIUngrabDevice\0XIAllowEvents\0XIPassiveGrabDevice\0XIPassiveUngrabDevice\0XIListProperties\0XIChangeProperty\0XIDeleteProperty\0XIGetProperty\0XIGetSelectedEvents\0XIBarrierReleasePointer\0",
	.num_events = 17,
	.strings_events = "DeviceValuator\0DeviceKeyPress\0DeviceKeyRelease\0DeviceButtonPress\0DeviceButtonRelease\0DeviceMotionNotify\0DeviceFocusIn\0DeviceFocusOut\0ProximityIn\0ProximityOut\0DeviceStateNotify\0DeviceMappingNotify\0ChangeDeviceNotify\0DeviceKeyStateNotify\0DeviceButtonStateNotify\0DevicePresenceNotify\0DevicePropertyNotify\0",
	.num_xge_events = 27,
	.strings_xge_events = "Unknown (0)\0DeviceChanged\0KeyPress\0KeyRelease\0ButtonPress\0ButtonRelease\0Motion\0Enter\0Leave\0FocusIn\0FocusOut\0Hierarchy\0Property\0RawKeyPress\0RawKeyRelease\0RawButtonPress\0RawButtonRelease\0RawMotion\0TouchBegin\0TouchUpdate\0TouchEnd\0TouchOwnership\0RawTouchBegin\0RawTouchUpdate\0RawTouchEnd\0BarrierHit\0BarrierLeave\0",
	.num_errors = 5,
	.strings_errors = "Device\0Event\0Mode\0DeviceBusy\0Class\0",
	.name = "Input",
};

static const struct static_extension_info_t extension_xkb_info = { // XKEYBOARD
	.num_minor = 102,
	.strings_minor = "UseExtension\0SelectEvents\0Unknown (2)\0Bell\0GetState\0LatchLockState\0GetControls\0SetControls\0GetMap\0SetMap\0GetCompatMap\0SetCompatMap\0GetIndicatorState\0GetIndicatorMap\0SetIndicatorMap\0GetNamedIndicator\0SetNamedIndicator\0GetNames\0SetNames\0Unknown (19)\0Unknown (20)\0PerClientFlags\0ListComponents\0GetKbdByName\0GetDeviceInfo\0SetDeviceInfo\0Unknown (26)\0Unknown (27)\0Unknown (28)\0Unknown (29)\0Unknown (30)\0Unknown (31)\0Unknown (32)\0Unknown (33)\0Unknown (34)\0Unknown (35)\0Unknown (36)\0Unknown (37)\0Unknown (38)\0Unknown (39)\0Unknown (40)\0Unknown (41)\0Unknown (42)\0Unknown (43)\0Unknown (44)\0Unknown (45)\0Unknown (46)\0Unknown (47)\0Unknown (48)\0Unknown (49)\0Unknown (50)\0Unknown (51)\0Unknown (52)\0Unknown (53)\0Unknown (54)\0Unknown (55)\0Unknown (56)\0Unknown (57)\0Unknown (58)\0Unknown (59)\0Unknown (60)\0Unknown (61)\0Unknown (62)\0Unknown (63)\0Unknown (64)\0Unknown (65)\0Unknown (66)\0Unknown (67)\0Unknown (68)\0Unknown (69)\0Unknown (70)\0Unknown (71)\0Unknown (72)\0Unknown (73)\0Unknown (74)\0Unknown (75)\0Unknown (76)\0Unknown (77)\0Unknown (78)\0Unknown (79)\0Unknown (80)\0Unknown (81)\0Unknown (82)\0Unknown (83)\0Unknown (84)\0Unknown (85)\0Unknown (86)\0Unknown (87)\0Unknown (88)\0Unknown (89)\0Unknown (90)\0Unknown (91)\0Unknown (92)\0Unknown (93)\0Unknown (94)\0Unknown (95)\0Unknown (96)\0Unknown (97)\0Unknown (98)\0Unknown (99)\0Unknown (100)\0SetDebuggingFlags\0",
	.num_events = 1,
	.strings_events = "XKB base event\0",
	.num_xge_events = 12,
	.strings_xge_events = "NewKeyboardNotify\0MapNotify\0StateNotify\0ControlsNotify\0IndicatorStateNotify\0IndicatorMapNotify\0NamesNotify\0CompatMapNotify\0BellNotify\0ActionMessage\0AccessXNotify\0ExtensionDeviceNotify\0",
	.num_errors = 1,
	.strings_errors = "Keyboard\0",
	.name = "xkb",
};

static const struct static_extension_info_t extension_XPrint_info = { // XpExtension
	.num_minor = 25,
	.strings_minor = "PrintQueryVersion\0PrintGetPrinterList\0CreateContext\0PrintSetContext\0PrintGetContext\0PrintDestroyContext\0PrintGetScreenOfContext\0PrintStartJob\0PrintEndJob\0PrintStartDoc\0PrintEndDoc\0PrintPutDocumentData\0PrintGetDocumentData\0PrintStartPage\0PrintEndPage\0PrintSelectInput\0PrintInputSelected\0PrintGetAttributes\0PrintSetAttributes\0PrintGetOneAttributes\0PrintRehashPrinterList\0PrintGetPageDimensions\0PrintQueryScreens\0PrintSetImageResolution\0PrintGetImageResolution\0",
	.num_events = 2,
	.strings_events = "Notify\0AttributNotify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 2,
	.strings_errors = "BadContext\0BadSequence\0",
	.name = "XPrint",
};

static const struct static_extension_info_t extension_SELinux_info = { // SELinux
	.num_minor = 23,
	.strings_minor = "QueryVersion\0SetDeviceCreateContext\0GetDeviceCreateContext\0SetDeviceContext\0GetDeviceContext\0SetWindowCreateContext\0GetWindowCreateContext\0GetWindowContext\0SetPropertyCreateContext\0GetPropertyCreateContext\0SetPropertyUseContext\0GetPropertyUseContext\0GetPropertyContext\0GetPropertyDataContext\0ListProperties\0SetSelectionCreateContext\0GetSelectionCreateContext\0SetSelectionUseContext\0GetSelectionUseContext\0GetSelectionContext\0GetSelectionDataContext\0ListSelections\0GetClientContext\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "SELinux",
};

static const struct static_extension_info_t extension_Test_info = { // XTEST
	.num_minor = 4,
	.strings_minor = "GetVersion\0CompareCursor\0FakeInput\0GrabControl\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "Test",
};

static const struct static_extension_info_t extension_XvMC_info = { // XVideo-MotionCompensation
	.num_minor = 9,
	.strings_minor = "QueryVersion\0ListSurfaceTypes\0CreateContext\0DestroyContext\0CreateSurface\0DestroySurface\0CreateSubpicture\0DestroySubpicture\0ListSubpictureTypes\0",
	.num_events = 0,
	.strings_events = NULL,
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = NULL,
	.name = "XvMC",
};

static const struct static_extension_info_t extension_Xv_info = { // XVideo
	.num_minor = 20,
	.strings_minor = "QueryExtension\0QueryAdaptors\0QueryEncodings\0GrabPort\0UngrabPort\0PutVideo\0PutStill\0GetVideo\0GetStill\0StopVideo\0SelectVideoNotify\0SelectPortNotify\0QueryBestSize\0SetPortAttribute\0GetPortAttribute\0QueryPortAttributes\0ListImageFormats\0QueryImageAttributes\0PutImage\0ShmPutImage\0",
	.num_events = 2,
	.strings_events = "VideoNotify\0PortNotify\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 3,
	.strings_errors = "BadPort\0BadEncoding\0BadControl\0",
	.name = "Xv",
};

const struct static_extension_info_t xproto_info = { // xproto
	.num_minor = 0,
	.strings_minor = "Unknown (0)\0CreateWindow\0ChangeWindowAttributes\0GetWindowAttributes\0DestroyWindow\0DestroySubwindows\0ChangeSaveSet\0ReparentWindow\0MapWindow\0MapSubwindows\0UnmapWindow\0UnmapSubwindows\0ConfigureWindow\0CirculateWindow\0GetGeometry\0QueryTree\0InternAtom\0GetAtomName\0ChangeProperty\0DeleteProperty\0GetProperty\0ListProperties\0SetSelectionOwner\0GetSelectionOwner\0ConvertSelection\0SendEvent\0GrabPointer\0UngrabPointer\0GrabButton\0UngrabButton\0ChangeActivePointerGrab\0GrabKeyboard\0UngrabKeyboard\0GrabKey\0UngrabKey\0AllowEvents\0GrabServer\0UngrabServer\0QueryPointer\0GetMotionEvents\0TranslateCoordinates\0WarpPointer\0SetInputFocus\0GetInputFocus\0QueryKeymap\0OpenFont\0CloseFont\0QueryFont\0QueryTextExtents\0ListFonts\0ListFontsWithInfo\0SetFontPath\0GetFontPath\0CreatePixmap\0FreePixmap\0CreateGC\0ChangeGC\0CopyGC\0SetDashes\0SetClipRectangles\0FreeGC\0ClearArea\0CopyArea\0CopyPlane\0PolyPoint\0PolyLine\0PolySegment\0PolyRectangle\0PolyArc\0FillPoly\0PolyFillRectangle\0PolyFillArc\0PutImage\0GetImage\0PolyText8\0PolyText16\0ImageText8\0ImageText16\0CreateColormap\0FreeColormap\0CopyColormapAndFree\0InstallColormap\0UninstallColormap\0ListInstalledColormaps\0AllocColor\0AllocNamedColor\0AllocColorCells\0AllocColorPlanes\0FreeColors\0StoreColors\0StoreNamedColor\0QueryColors\0LookupColor\0CreateCursor\0CreateGlyphCursor\0FreeCursor\0RecolorCursor\0QueryBestSize\0QueryExtension\0ListExtensions\0ChangeKeyboardMapping\0GetKeyboardMapping\0ChangeKeyboardControl\0GetKeyboardControl\0Bell\0ChangePointerControl\0GetPointerControl\0SetScreenSaver\0GetScreenSaver\0ChangeHosts\0ListHosts\0SetAccessControl\0SetCloseDownMode\0KillClient\0RotateProperties\0ForceScreenSaver\0SetPointerMapping\0GetPointerMapping\0SetModifierMapping\0GetModifierMapping\0Unknown (120)\0Unknown (121)\0Unknown (122)\0Unknown (123)\0Unknown (124)\0Unknown (125)\0Unknown (126)\0NoOperation\0Unknown (128)\0Unknown (129)\0Unknown (130)\0Unknown (131)\0Unknown (132)\0Unknown (133)\0Unknown (134)\0Unknown (135)\0Unknown (136)\0Unknown (137)\0Unknown (138)\0Unknown (139)\0Unknown (140)\0Unknown (141)\0Unknown (142)\0Unknown (143)\0Unknown (144)\0Unknown (145)\0Unknown (146)\0Unknown (147)\0Unknown (148)\0Unknown (149)\0Unknown (150)\0Unknown (151)\0Unknown (152)\0Unknown (153)\0Unknown (154)\0Unknown (155)\0Unknown (156)\0Unknown (157)\0Unknown (158)\0Unknown (159)\0Unknown (160)\0Unknown (161)\0Unknown (162)\0Unknown (163)\0Unknown (164)\0Unknown (165)\0Unknown (166)\0Unknown (167)\0Unknown (168)\0Unknown (169)\0Unknown (170)\0Unknown (171)\0Unknown (172)\0Unknown (173)\0Unknown (174)\0Unknown (175)\0Unknown (176)\0Unknown (177)\0Unknown (178)\0Unknown (179)\0Unknown (180)\0Unknown (181)\0Unknown (182)\0Unknown (183)\0Unknown (184)\0Unknown (185)\0Unknown (186)\0Unknown (187)\0Unknown (188)\0Unknown (189)\0Unknown (190)\0Unknown (191)\0Unknown (192)\0Unknown (193)\0Unknown (194)\0Unknown (195)\0Unknown (196)\0Unknown (197)\0Unknown (198)\0Unknown (199)\0Unknown (200)\0Unknown (201)\0Unknown (202)\0Unknown (203)\0Unknown (204)\0Unknown (205)\0Unknown (206)\0Unknown (207)\0Unknown (208)\0Unknown (209)\0Unknown (210)\0Unknown (211)\0Unknown (212)\0Unknown (213)\0Unknown (214)\0Unknown (215)\0Unknown (216)\0Unknown (217)\0Unknown (218)\0Unknown (219)\0Unknown (220)\0Unknown (221)\0Unknown (222)\0Unknown (223)\0Unknown (224)\0Unknown (225)\0Unknown (226)\0Unknown (227)\0Unknown (228)\0Unknown (229)\0Unknown (230)\0Unknown (231)\0Unknown (232)\0Unknown (233)\0Unknown (234)\0Unknown (235)\0Unknown (236)\0Unknown (237)\0Unknown (238)\0Unknown (239)\0Unknown (240)\0Unknown (241)\0Unknown (242)\0Unknown (243)\0Unknown (244)\0Unknown (245)\0Unknown (246)\0Unknown (247)\0Unknown (248)\0Unknown (249)\0Unknown (250)\0Unknown (251)\0Unknown (252)\0Unknown (253)\0Unknown (254)\0Unknown (255)\0",
	.num_events = 0,
	.strings_events = "Unknown (0)\0Unknown (1)\0KeyPress\0KeyRelease\0ButtonPress\0ButtonRelease\0MotionNotify\0EnterNotify\0LeaveNotify\0FocusIn\0FocusOut\0KeymapNotify\0Expose\0GraphicsExposure\0NoExposure\0VisibilityNotify\0CreateNotify\0DestroyNotify\0UnmapNotify\0MapNotify\0MapRequest\0ReparentNotify\0ConfigureNotify\0ConfigureRequest\0GravityNotify\0ResizeRequest\0CirculateNotify\0CirculateRequest\0PropertyNotify\0SelectionClear\0SelectionRequest\0SelectionNotify\0ColormapNotify\0ClientMessage\0MappingNotify\0GeGeneric\0Unknown (36)\0Unknown (37)\0Unknown (38)\0Unknown (39)\0Unknown (40)\0Unknown (41)\0Unknown (42)\0Unknown (43)\0Unknown (44)\0Unknown (45)\0Unknown (46)\0Unknown (47)\0Unknown (48)\0Unknown (49)\0Unknown (50)\0Unknown (51)\0Unknown (52)\0Unknown (53)\0Unknown (54)\0Unknown (55)\0Unknown (56)\0Unknown (57)\0Unknown (58)\0Unknown (59)\0Unknown (60)\0Unknown (61)\0Unknown (62)\0Unknown (63)\0Unknown (64)\0Unknown (65)\0Unknown (66)\0Unknown (67)\0Unknown (68)\0Unknown (69)\0Unknown (70)\0Unknown (71)\0Unknown (72)\0Unknown (73)\0Unknown (74)\0Unknown (75)\0Unknown (76)\0Unknown (77)\0Unknown (78)\0Unknown (79)\0Unknown (80)\0Unknown (81)\0Unknown (82)\0Unknown (83)\0Unknown (84)\0Unknown (85)\0Unknown (86)\0Unknown (87)\0Unknown (88)\0Unknown (89)\0Unknown (90)\0Unknown (91)\0Unknown (92)\0Unknown (93)\0Unknown (94)\0Unknown (95)\0Unknown (96)\0Unknown (97)\0Unknown (98)\0Unknown (99)\0Unknown (100)\0Unknown (101)\0Unknown (102)\0Unknown (103)\0Unknown (104)\0Unknown (105)\0Unknown (106)\0Unknown (107)\0Unknown (108)\0Unknown (109)\0Unknown (110)\0Unknown (111)\0Unknown (112)\0Unknown (113)\0Unknown (114)\0Unknown (115)\0Unknown (116)\0Unknown (117)\0Unknown (118)\0Unknown (119)\0Unknown (120)\0Unknown (121)\0Unknown (122)\0Unknown (123)\0Unknown (124)\0Unknown (125)\0Unknown (126)\0Unknown (127)\0Unknown (128)\0Unknown (129)\0Unknown (130)\0Unknown (131)\0Unknown (132)\0Unknown (133)\0Unknown (134)\0Unknown (135)\0Unknown (136)\0Unknown (137)\0Unknown (138)\0Unknown (139)\0Unknown (140)\0Unknown (141)\0Unknown (142)\0Unknown (143)\0Unknown (144)\0Unknown (145)\0Unknown (146)\0Unknown (147)\0Unknown (148)\0Unknown (149)\0Unknown (150)\0Unknown (151)\0Unknown (152)\0Unknown (153)\0Unknown (154)\0Unknown (155)\0Unknown (156)\0Unknown (157)\0Unknown (158)\0Unknown (159)\0Unknown (160)\0Unknown (161)\0Unknown (162)\0Unknown (163)\0Unknown (164)\0Unknown (165)\0Unknown (166)\0Unknown (167)\0Unknown (168)\0Unknown (169)\0Unknown (170)\0Unknown (171)\0Unknown (172)\0Unknown (173)\0Unknown (174)\0Unknown (175)\0Unknown (176)\0Unknown (177)\0Unknown (178)\0Unknown (179)\0Unknown (180)\0Unknown (181)\0Unknown (182)\0Unknown (183)\0Unknown (184)\0Unknown (185)\0Unknown (186)\0Unknown (187)\0Unknown (188)\0Unknown (189)\0Unknown (190)\0Unknown (191)\0Unknown (192)\0Unknown (193)\0Unknown (194)\0Unknown (195)\0Unknown (196)\0Unknown (197)\0Unknown (198)\0Unknown (199)\0Unknown (200)\0Unknown (201)\0Unknown (202)\0Unknown (203)\0Unknown (204)\0Unknown (205)\0Unknown (206)\0Unknown (207)\0Unknown (208)\0Unknown (209)\0Unknown (210)\0Unknown (211)\0Unknown (212)\0Unknown (213)\0Unknown (214)\0Unknown (215)\0Unknown (216)\0Unknown (217)\0Unknown (218)\0Unknown (219)\0Unknown (220)\0Unknown (221)\0Unknown (222)\0Unknown (223)\0Unknown (224)\0Unknown (225)\0Unknown (226)\0Unknown (227)\0Unknown (228)\0Unknown (229)\0Unknown (230)\0Unknown (231)\0Unknown (232)\0Unknown (233)\0Unknown (234)\0Unknown (235)\0Unknown (236)\0Unknown (237)\0Unknown (238)\0Unknown (239)\0Unknown (240)\0Unknown (241)\0Unknown (242)\0Unknown (243)\0Unknown (244)\0Unknown (245)\0Unknown (246)\0Unknown (247)\0Unknown (248)\0Unknown (249)\0Unknown (250)\0Unknown (251)\0Unknown (252)\0Unknown (253)\0Unknown (254)\0Unknown (255)\0",
	.num_xge_events = 0,
	.strings_xge_events = NULL,
	.num_errors = 0,
	.strings_errors = "Unknown (0)\0Request\0Value\0Window\0Pixmap\0Atom\0Cursor\0Font\0Match\0Drawable\0Access\0Alloc\0Colormap\0GContext\0IDChoice\0Name\0Length\0Implementation\0Unknown (18)\0Unknown (19)\0Unknown (20)\0Unknown (21)\0Unknown (22)\0Unknown (23)\0Unknown (24)\0Unknown (25)\0Unknown (26)\0Unknown (27)\0Unknown (28)\0Unknown (29)\0Unknown (30)\0Unknown (31)\0Unknown (32)\0Unknown (33)\0Unknown (34)\0Unknown (35)\0Unknown (36)\0Unknown (37)\0Unknown (38)\0Unknown (39)\0Unknown (40)\0Unknown (41)\0Unknown (42)\0Unknown (43)\0Unknown (44)\0Unknown (45)\0Unknown (46)\0Unknown (47)\0Unknown (48)\0Unknown (49)\0Unknown (50)\0Unknown (51)\0Unknown (52)\0Unknown (53)\0Unknown (54)\0Unknown (55)\0Unknown (56)\0Unknown (57)\0Unknown (58)\0Unknown (59)\0Unknown (60)\0Unknown (61)\0Unknown (62)\0Unknown (63)\0Unknown (64)\0Unknown (65)\0Unknown (66)\0Unknown (67)\0Unknown (68)\0Unknown (69)\0Unknown (70)\0Unknown (71)\0Unknown (72)\0Unknown (73)\0Unknown (74)\0Unknown (75)\0Unknown (76)\0Unknown (77)\0Unknown (78)\0Unknown (79)\0Unknown (80)\0Unknown (81)\0Unknown (82)\0Unknown (83)\0Unknown (84)\0Unknown (85)\0Unknown (86)\0Unknown (87)\0Unknown (88)\0Unknown (89)\0Unknown (90)\0Unknown (91)\0Unknown (92)\0Unknown (93)\0Unknown (94)\0Unknown (95)\0Unknown (96)\0Unknown (97)\0Unknown (98)\0Unknown (99)\0Unknown (100)\0Unknown (101)\0Unknown (102)\0Unknown (103)\0Unknown (104)\0Unknown (105)\0Unknown (106)\0Unknown (107)\0Unknown (108)\0Unknown (109)\0Unknown (110)\0Unknown (111)\0Unknown (112)\0Unknown (113)\0Unknown (114)\0Unknown (115)\0Unknown (116)\0Unknown (117)\0Unknown (118)\0Unknown (119)\0Unknown (120)\0Unknown (121)\0Unknown (122)\0Unknown (123)\0Unknown (124)\0Unknown (125)\0Unknown (126)\0Unknown (127)\0Unknown (128)\0Unknown (129)\0Unknown (130)\0Unknown (131)\0Unknown (132)\0Unknown (133)\0Unknown (134)\0Unknown (135)\0Unknown (136)\0Unknown (137)\0Unknown (138)\0Unknown (139)\0Unknown (140)\0Unknown (141)\0Unknown (142)\0Unknown (143)\0Unknown (144)\0Unknown (145)\0Unknown (146)\0Unknown (147)\0Unknown (148)\0Unknown (149)\0Unknown (150)\0Unknown (151)\0Unknown (152)\0Unknown (153)\0Unknown (154)\0Unknown (155)\0Unknown (156)\0Unknown (157)\0Unknown (158)\0Unknown (159)\0Unknown (160)\0Unknown (161)\0Unknown (162)\0Unknown (163)\0Unknown (164)\0Unknown (165)\0Unknown (166)\0Unknown (167)\0Unknown (168)\0Unknown (169)\0Unknown (170)\0Unknown (171)\0Unknown (172)\0Unknown (173)\0Unknown (174)\0Unknown (175)\0Unknown (176)\0Unknown (177)\0Unknown (178)\0Unknown (179)\0Unknown (180)\0Unknown (181)\0Unknown (182)\0Unknown (183)\0Unknown (184)\0Unknown (185)\0Unknown (186)\0Unknown (187)\0Unknown (188)\0Unknown (189)\0Unknown (190)\0Unknown (191)\0Unknown (192)\0Unknown (193)\0Unknown (194)\0Unknown (195)\0Unknown (196)\0Unknown (197)\0Unknown (198)\0Unknown (199)\0Unknown (200)\0Unknown (201)\0Unknown (202)\0Unknown (203)\0Unknown (204)\0Unknown (205)\0Unknown (206)\0Unknown (207)\0Unknown (208)\0Unknown (209)\0Unknown (210)\0Unknown (211)\0Unknown (212)\0Unknown (213)\0Unknown (214)\0Unknown (215)\0Unknown (216)\0Unknown (217)\0Unknown (218)\0Unknown (219)\0Unknown (220)\0Unknown (221)\0Unknown (222)\0Unknown (223)\0Unknown (224)\0Unknown (225)\0Unknown (226)\0Unknown (227)\0Unknown (228)\0Unknown (229)\0Unknown (230)\0Unknown (231)\0Unknown (232)\0Unknown (233)\0Unknown (234)\0Unknown (235)\0Unknown (236)\0Unknown (237)\0Unknown (238)\0Unknown (239)\0Unknown (240)\0Unknown (241)\0Unknown (242)\0Unknown (243)\0Unknown (244)\0Unknown (245)\0Unknown (246)\0Unknown (247)\0Unknown (248)\0Unknown (249)\0Unknown (250)\0Unknown (251)\0Unknown (252)\0Unknown (253)\0Unknown (254)\0Unknown (255)\0",
	.name = "xproto",
};

int register_extensions(xcb_errors_context_t *ctx, xcb_connection_t *conn)
{
	xcb_query_extension_cookie_t cookies[30];
	int ret = 0;
	cookies[0] = xcb_query_extension_unchecked(conn, strlen("BIG-REQUESTS"), "BIG-REQUESTS");
	cookies[1] = xcb_query_extension_unchecked(conn, strlen("Composite"), "Composite");
	cookies[2] = xcb_query_extension_unchecked(conn, strlen("DAMAGE"), "DAMAGE");
	cookies[3] = xcb_query_extension_unchecked(conn, strlen("DPMS"), "DPMS");
	cookies[4] = xcb_query_extension_unchecked(conn, strlen("DRI2"), "DRI2");
	cookies[5] = xcb_query_extension_unchecked(conn, strlen("DRI3"), "DRI3");
	cookies[6] = xcb_query_extension_unchecked(conn, strlen("Generic Event Extension"), "Generic Event Extension");
	cookies[7] = xcb_query_extension_unchecked(conn, strlen("GLX"), "GLX");
	cookies[8] = xcb_query_extension_unchecked(conn, strlen("Present"), "Present");
	cookies[9] = xcb_query_extension_unchecked(conn, strlen("RANDR"), "RANDR");
	cookies[10] = xcb_query_extension_unchecked(conn, strlen("RECORD"), "RECORD");
	cookies[11] = xcb_query_extension_unchecked(conn, strlen("RENDER"), "RENDER");
	cookies[12] = xcb_query_extension_unchecked(conn, strlen("X-Resource"), "X-Resource");
	cookies[13] = xcb_query_extension_unchecked(conn, strlen("MIT-SCREEN-SAVER"), "MIT-SCREEN-SAVER");
	cookies[14] = xcb_query_extension_unchecked(conn, strlen("SHAPE"), "SHAPE");
	cookies[15] = xcb_query_extension_unchecked(conn, strlen("MIT-SHM"), "MIT-SHM");
	cookies[16] = xcb_query_extension_unchecked(conn, strlen("SYNC"), "SYNC");
	cookies[17] = xcb_query_extension_unchecked(conn, strlen("XC-MISC"), "XC-MISC");
	cookies[18] = xcb_query_extension_unchecked(conn, strlen("XEVIE"), "XEVIE");
	cookies[19] = xcb_query_extension_unchecked(conn, strlen("XFree86-DRI"), "XFree86-DRI");
	cookies[20] = xcb_query_extension_unchecked(conn, strlen("XFree86-VidModeExtension"), "XFree86-VidModeExtension");
	cookies[21] = xcb_query_extension_unchecked(conn, strlen("XFIXES"), "XFIXES");
	cookies[22] = xcb_query_extension_unchecked(conn, strlen("XINERAMA"), "XINERAMA");
	cookies[23] = xcb_query_extension_unchecked(conn, strlen("XInputExtension"), "XInputExtension");
	cookies[24] = xcb_query_extension_unchecked(conn, strlen("XKEYBOARD"), "XKEYBOARD");
	cookies[25] = xcb_query_extension_unchecked(conn, strlen("XpExtension"), "XpExtension");
	cookies[26] = xcb_query_extension_unchecked(conn, strlen("SELinux"), "SELinux");
	cookies[27] = xcb_query_extension_unchecked(conn, strlen("XTEST"), "XTEST");
	cookies[28] = xcb_query_extension_unchecked(conn, strlen("XVideo-MotionCompensation"), "XVideo-MotionCompensation");
	cookies[29] = xcb_query_extension_unchecked(conn, strlen("XVideo"), "XVideo");
	ret |= register_extension(ctx, conn, cookies[0], &extension_BigRequests_info);
	ret |= register_extension(ctx, conn, cookies[1], &extension_Composite_info);
	ret |= register_extension(ctx, conn, cookies[2], &extension_Damage_info);
	ret |= register_extension(ctx, conn, cookies[3], &extension_DPMS_info);
	ret |= register_extension(ctx, conn, cookies[4], &extension_DRI2_info);
	ret |= register_extension(ctx, conn, cookies[5], &extension_DRI3_info);
	ret |= register_extension(ctx, conn, cookies[6], &extension_GenericEvent_info);
	ret |= register_extension(ctx, conn, cookies[7], &extension_Glx_info);
	ret |= register_extension(ctx, conn, cookies[8], &extension_Present_info);
	ret |= register_extension(ctx, conn, cookies[9], &extension_RandR_info);
	ret |= register_extension(ctx, conn, cookies[10], &extension_Record_info);
	ret |= register_extension(ctx, conn, cookies[11], &extension_Render_info);
	ret |= register_extension(ctx, conn, cookies[12], &extension_Res_info);
	ret |= register_extension(ctx, conn, cookies[13], &extension_ScreenSaver_info);
	ret |= register_extension(ctx, conn, cookies[14], &extension_Shape_info);
	ret |= register_extension(ctx, conn, cookies[15], &extension_Shm_info);
	ret |= register_extension(ctx, conn, cookies[16], &extension_Sync_info);
	ret |= register_extension(ctx, conn, cookies[17], &extension_XCMisc_info);
	ret |= register_extension(ctx, conn, cookies[18], &extension_Xevie_info);
	ret |= register_extension(ctx, conn, cookies[19], &extension_XF86Dri_info);
	ret |= register_extension(ctx, conn, cookies[20], &extension_XF86VidMode_info);
	ret |= register_extension(ctx, conn, cookies[21], &extension_XFixes_info);
	ret |= register_extension(ctx, conn, cookies[22], &extension_Xinerama_info);
	ret |= register_extension(ctx, conn, cookies[23], &extension_Input_info);
	ret |= register_extension(ctx, conn, cookies[24], &extension_xkb_info);
	ret |= register_extension(ctx, conn, cookies[25], &extension_XPrint_info);
	ret |= register_extension(ctx, conn, cookies[26], &extension_SELinux_info);
	ret |= register_extension(ctx, conn, cookies[27], &extension_Test_info);
	ret |= register_extension(ctx, conn, cookies[28], &extension_XvMC_info);
	ret |= register_extension(ctx, conn, cookies[29], &extension_Xv_info);
	return ret;
}
