#!/usr/bin/python
from itertools import *
import sys,os,difflib

FilesStr=r"""
libX11/include/X11/cursorfont.h                   X11/cursorfont.h
libX11/include/X11/Xlocale.h                      X11/Xlocale.h
libX11/include/X11/XKBlib.h                       X11/XKBlib.h
libX11/include/X11/Xcms.h                         X11/Xcms.h
libX11/include/X11/Xregion.h                      X11/Xregion.h
libX11/include/X11/ImUtil.h                       X11/ImUtil.h
libX11/include/X11/Xlib.h                         X11/Xlib.h
libX11/include/X11/Xresource.h                    X11/Xresource.h
libX11/include/X11/Xlib-xcb.h                     X11/Xlib-xcb.h
libX11/include/X11/Xutil.h                        X11/Xutil.h
libX11/include/X11/Xlibint.h                      X11/Xlibint.h

libXt/include/X11/CallbackI.h                     X11/CallbackI.h
libXt/include/X11/Composite.h                     X11/Composite.h
libXt/include/X11/CompositeP.h                    X11/CompositeP.h
libXt/include/X11/ConstrainP.h                    X11/ConstrainP.h
libXt/include/X11/Constraint.h                    X11/Constraint.h
libXt/include/X11/ConvertI.h                      X11/ConvertI.h
libXt/include/X11/Core.h                          X11/Core.h
libXt/include/X11/CoreP.h                         X11/CoreP.h
libXt/include/X11/CreateI.h                       X11/CreateI.h
libXt/include/X11/EventI.h                        X11/EventI.h
libXt/include/X11/HookObjI.h                      X11/HookObjI.h
libXt/include/X11/InitialI.h                      X11/InitialI.h
libXt/include/X11/Intrinsic.h                     X11/Intrinsic.h
libXt/include/X11/IntrinsicI.h                    X11/IntrinsicI.h
libXt/include/X11/IntrinsicP.h                    X11/IntrinsicP.h
libXt/include/X11/Object.h                        X11/Object.h
libXt/include/X11/ObjectP.h                       X11/ObjectP.h
libXt/include/X11/PassivGraI.h                    X11/PassivGraI.h
libXt/include/X11/RectObj.h                       X11/RectObj.h
libXt/include/X11/RectObjP.h                      X11/RectObjP.h
libXt/include/X11/ResConfigP.h                    X11/ResConfigP.h
libXt/include/X11/ResourceI.h                     X11/ResourceI.h
libXt/include/X11/SelectionI.h                    X11/SelectionI.h
libXt/include/X11/ShellI.h                        X11/ShellI.h
libXt/include/X11/ShellP.h                        X11/ShellP.h
libXt/include/X11/ThreadsI.h                      X11/ThreadsI.h
libXt/include/X11/TranslateI.h                    X11/TranslateI.h
libXt/include/X11/VarargsI.h                      X11/VarargsI.h
libXt/include/X11/Vendor.h                        X11/Vendor.h
libXt/include/X11/VendorP.h                       X11/VendorP.h
libXt/include/X11/Xtos.h                          X11/Xtos.h

libXext/include/X11/extensions/dpms.h             X11/extensions/dpms.h
libXext/include/X11/extensions/extutil.h          X11/extensions/extutil.h
libXext/include/X11/extensions/MITMisc.h          X11/extensions/MITMisc.h
libXext/include/X11/extensions/multibuf.h         X11/extensions/multibuf.h
libXext/include/X11/extensions/security.h         X11/extensions/security.h
libXext/include/X11/extensions/shape.h            X11/extensions/shape.h
libXext/include/X11/extensions/sync.h             X11/extensions/sync.h
libXext/include/X11/extensions/Xag.h              X11/extensions/Xag.h
libXext/include/X11/extensions/Xcup.h             X11/extensions/Xcup.h
libXext/include/X11/extensions/Xdbe.h             X11/extensions/Xdbe.h
libXext/include/X11/extensions/XEVI.h             X11/extensions/XEVI.h
libXext/include/X11/extensions/Xext.h             X11/extensions/Xext.h
libXext/include/X11/extensions/Xge.h              X11/extensions/Xge.h
libXext/include/X11/extensions/XLbx.h             X11/extensions/XLbx.h
libXext/include/X11/extensions/XShm.h             X11/extensions/XShm.h
libXext/include/X11/extensions/xtestext1.h        X11/extensions/xtestext1.h

libXrender/include/X11/extensions/xrender.h       X11/extensions/xrender.h

libXau/include/X11/Xauth.h                        X11/Xauth.h

libXdmcp/include/X11/Xdmcp.h                      X11/Xdmcp.h

libxcb/src/xcbext.h                               include/xcb/xcbext.h
libxcb/src/render.h                               include/xcb/render.h
libxcb/src/xcb.h                                  include/xcb/xcb.h
libxcb/src/xc_misc.h                              include/xcb/xc_misc.h
libxcb/src/bigreq.h                               include/xcb/bigreq.h
libxcb/src/xproto.h                               include/xcb/xproto.h
libxcb/src/composite.h                            include/xcb/composite.h
libxcb/src/xfixes.h                               include/xcb/xfixes.h
libxcb/src/xcb_windefs.h                          include/xcb/xcb_windefs.h
libxcb/src/shape.h                                include/xcb/shape.h
libxcb/src/xcb_icccm.h                            include/xcb/xcb_icccm.h
libxcb/src/xcb_aux.h                              include/xcb/xcb_aux.h
libxcb/src/xcb_ewmh.h                             include/xcb/xcb_ewmh.h

xcb-util-errors/src/xcb_errors.h                  include/xcb/xcb_errors.h

mesalib/include/gl/glext.h                        gl/glext.h
mesalib/include/gl/glx.h                          gl/glx.h
mesalib/include/gl/glxext.h                       gl/glxext.h
mesalib/include/gl/internal/dri_interface.h       gl/internal/dri_interface.h

mesalib/include/c99_compat.h                      include/c99_compat.h
mesalib/include/no_extern_c.h                     include/no_extern_c.h

xorg-server/hw/xwin/glx/wglext.h                  mesalib/include/GL/wglext.h

mesalib/src/mapi/glapi/glapi.h                      xorg-server/glx/glapi.h
mesalib/src/util/macros.h                           xorg-server/glx/util/macros.h
mesalib/src/mapi/glapi/glapitable.h                 xorg-server/glx/glapitable.h
mesalib/src/mapi/glapi/gen/remap_helper.h           xorg-server/glx/remap_helper.h
mesalib/src/mesa/main/remap_helper.h                xorg-server/glx/remap_helper.h
mesalib/src/mapi/glapi/gen/glprocs.h                xorg-server/glx/glprocs.h
mesalib/src/mapi/glapi/gen/dispatch.h               xorg-server/glx/dispatch.h
mesalib/src/mapi/glapi/gen/dispatch.h               mesalib/src/mesa/main/dispatch.h
mesalib/src/mapi/glapi/gen/indirect_dispatch.c      xorg-server/glx/indirect_dispatch.c
mesalib/src/mapi/glapi/gen/indirect_dispatch.h      xorg-server/glx/indirect_dispatch.h
mesalib/src/mapi/glapi/gen/indirect_dispatch_swap.c xorg-server/glx/indirect_dispatch_swap.c
mesalib/src/mapi/glapi/gen/indirect_reqsize.c       xorg-server/glx/indirect_reqsize.c
mesalib/src/mapi/glapi/gen/indirect_reqsize.h       xorg-server/glx/indirect_reqsize.h
mesalib/src/mapi/glapi/gen/indirect_size.h          xorg-server/glx/indirect_size.h
mesalib/src/mapi/glapi/gen/indirect_size_get.c      xorg-server/glx/indirect_size_get.c
mesalib/src/mapi/glapi/gen/indirect_size_get.h      xorg-server/glx/indirect_size_get.h
mesalib/src/mapi/glapi/gen/indirect_table.c         xorg-server/glx/indirect_table.c
mesalib/src/mapi/glapi/gen/glfunctions.h            xorg-server/glx/glfunctions.h

libXaw/include/X11/Xaw/AllWidgets.h               X11/Xaw/AllWidgets.h
libXaw/include/X11/Xaw/AsciiSink.h                X11/Xaw/AsciiSink.h
libXaw/include/X11/Xaw/AsciiSinkP.h               X11/Xaw/AsciiSinkP.h
libXaw/include/X11/Xaw/AsciiSrc.h                 X11/Xaw/AsciiSrc.h
libXaw/include/X11/Xaw/AsciiSrcP.h                X11/Xaw/AsciiSrcP.h
libXaw/include/X11/Xaw/AsciiText.h                X11/Xaw/AsciiText.h
libXaw/include/X11/Xaw/AsciiTextP.h               X11/Xaw/AsciiTextP.h
libXaw/include/X11/Xaw/Box.h                      X11/Xaw/Box.h
libXaw/include/X11/Xaw/BoxP.h                     X11/Xaw/BoxP.h
libXaw/include/X11/Xaw/Cardinals.h                X11/Xaw/Cardinals.h
libXaw/include/X11/Xaw/Command.h                  X11/Xaw/Command.h
libXaw/include/X11/Xaw/CommandP.h                 X11/Xaw/CommandP.h
libXaw/include/X11/Xaw/Dialog.h                   X11/Xaw/Dialog.h
libXaw/include/X11/Xaw/DialogP.h                  X11/Xaw/DialogP.h
libXaw/include/X11/Xaw/Form.h                     X11/Xaw/Form.h
libXaw/include/X11/Xaw/FormP.h                    X11/Xaw/FormP.h
libXaw/include/X11/Xaw/Grip.h                     X11/Xaw/Grip.h
libXaw/include/X11/Xaw/GripP.h                    X11/Xaw/GripP.h
libXaw/include/X11/Xaw/Label.h                    X11/Xaw/Label.h
libXaw/include/X11/Xaw/LabelP.h                   X11/Xaw/LabelP.h
libXaw/include/X11/Xaw/List.h                     X11/Xaw/List.h
libXaw/include/X11/Xaw/ListP.h                    X11/Xaw/ListP.h
libXaw/include/X11/Xaw/MenuButton.h               X11/Xaw/MenuButton.h
libXaw/include/X11/Xaw/MenuButtoP.h               X11/Xaw/MenuButtoP.h
libXaw/include/X11/Xaw/MultiSink.h                X11/Xaw/MultiSink.h
libXaw/include/X11/Xaw/MultiSinkP.h               X11/Xaw/MultiSinkP.h
libXaw/include/X11/Xaw/MultiSrc.h                 X11/Xaw/MultiSrc.h
libXaw/include/X11/Xaw/MultiSrcP.h                X11/Xaw/MultiSrcP.h
libXaw/include/X11/Xaw/Paned.h                    X11/Xaw/Paned.h
libXaw/include/X11/Xaw/PanedP.h                   X11/Xaw/PanedP.h
libXaw/include/X11/Xaw/Panner.h                   X11/Xaw/Panner.h
libXaw/include/X11/Xaw/PannerP.h                  X11/Xaw/PannerP.h
libXaw/include/X11/Xaw/Porthole.h                 X11/Xaw/Porthole.h
libXaw/include/X11/Xaw/PortholeP.h                X11/Xaw/PortholeP.h
libXaw/include/X11/Xaw/Repeater.h                 X11/Xaw/Repeater.h
libXaw/include/X11/Xaw/RepeaterP.h                X11/Xaw/RepeaterP.h
libXaw/include/X11/Xaw/Reports.h                  X11/Xaw/Reports.h
libXaw/include/X11/Xaw/Scrollbar.h                X11/Xaw/Scrollbar.h
libXaw/include/X11/Xaw/ScrollbarP.h               X11/Xaw/ScrollbarP.h
libXaw/include/X11/Xaw/Simple.h                   X11/Xaw/Simple.h
libXaw/include/X11/Xaw/SimpleMenP.h               X11/Xaw/SimpleMenP.h
libXaw/include/X11/Xaw/SimpleMenu.h               X11/Xaw/SimpleMenu.h
libXaw/include/X11/Xaw/SimpleP.h                  X11/Xaw/SimpleP.h
libXaw/include/X11/Xaw/Sme.h                      X11/Xaw/Sme.h
libXaw/include/X11/Xaw/SmeBSB.h                   X11/Xaw/SmeBSB.h
libXaw/include/X11/Xaw/SmeBSBP.h                  X11/Xaw/SmeBSBP.h
libXaw/include/X11/Xaw/SmeLine.h                  X11/Xaw/SmeLine.h
libXaw/include/X11/Xaw/SmeLineP.h                 X11/Xaw/SmeLineP.h
libXaw/include/X11/Xaw/SmeP.h                     X11/Xaw/SmeP.h
libXaw/include/X11/Xaw/StripCharP.h               X11/Xaw/StripCharP.h
libXaw/include/X11/Xaw/StripChart.h               X11/Xaw/StripChart.h
libXaw/include/X11/Xaw/Template.c                 X11/Xaw/Template.c
libXaw/include/X11/Xaw/Template.h                 X11/Xaw/Template.h
libXaw/include/X11/Xaw/TemplateP.h                X11/Xaw/TemplateP.h
libXaw/include/X11/Xaw/Text.h                     X11/Xaw/Text.h
libXaw/include/X11/Xaw/TextP.h                    X11/Xaw/TextP.h
libXaw/include/X11/Xaw/TextSink.h                 X11/Xaw/TextSink.h
libXaw/include/X11/Xaw/TextSinkP.h                X11/Xaw/TextSinkP.h
libXaw/include/X11/Xaw/TextSrc.h                  X11/Xaw/TextSrc.h
libXaw/include/X11/Xaw/TextSrcP.h                 X11/Xaw/TextSrcP.h
libXaw/include/X11/Xaw/Tip.h                      X11/Xaw/Tip.h
libXaw/include/X11/Xaw/TipP.h                     X11/Xaw/TipP.h
libXaw/include/X11/Xaw/Toggle.h                   X11/Xaw/Toggle.h
libXaw/include/X11/Xaw/ToggleP.h                  X11/Xaw/ToggleP.h
libXaw/include/X11/Xaw/Tree.h                     X11/Xaw/Tree.h
libXaw/include/X11/Xaw/TreeP.h                    X11/Xaw/TreeP.h
libXaw/include/X11/Xaw/VendorEP.h                 X11/Xaw/VendorEP.h
libXaw/include/X11/Xaw/Viewport.h                 X11/Xaw/Viewport.h
libXaw/include/X11/Xaw/ViewportP.h                X11/Xaw/ViewportP.h
libXaw/include/X11/Xaw/XawImP.h                   X11/Xaw/XawImP.h
libXaw/include/X11/Xaw/XawInit.h                  X11/Xaw/XawInit.h
libXmu/include/X11/Xmu/Atoms.h                    X11/Xmu/Atoms.h
libXmu/include/X11/Xmu/CharSet.h                  X11/Xmu/CharSet.h
libXmu/include/X11/Xmu/CloseHook.h                X11/Xmu/CloseHook.h
libXmu/include/X11/Xmu/Converters.h               X11/Xmu/Converters.h
libXmu/include/X11/Xmu/CurUtil.h                  X11/Xmu/CurUtil.h
libXmu/include/X11/Xmu/CvtCache.h                 X11/Xmu/CvtCache.h
libXmu/include/X11/Xmu/DisplayQue.h               X11/Xmu/DisplayQue.h
libXmu/include/X11/Xmu/Drawing.h                  X11/Xmu/Drawing.h
libXmu/include/X11/Xmu/Editres.h                  X11/Xmu/Editres.h
libXmu/include/X11/Xmu/EditresP.h                 X11/Xmu/EditresP.h
libXmu/include/X11/Xmu/Error.h                    X11/Xmu/Error.h
libXmu/include/X11/Xmu/ExtAgent.h                 X11/Xmu/ExtAgent.h
libXmu/include/X11/Xmu/Initer.h                   X11/Xmu/Initer.h
libXmu/include/X11/Xmu/Lookup.h                   X11/Xmu/Lookup.h
libXmu/include/X11/Xmu/Misc.h                     X11/Xmu/Misc.h
libXmu/include/X11/Xmu/StdCmap.h                  X11/Xmu/StdCmap.h
libXmu/include/X11/Xmu/StdSel.h                   X11/Xmu/StdSel.h
libXmu/include/X11/Xmu/SysUtil.h                  X11/Xmu/SysUtil.h
libXmu/include/X11/Xmu/WhitePoint.h               X11/Xmu/WhitePoint.h
libXmu/include/X11/Xmu/WidgetNode.h               X11/Xmu/WidgetNode.h
libXmu/include/X11/Xmu/WinUtil.h                  X11/Xmu/WinUtil.h
libXmu/include/X11/Xmu/Xct.h                      X11/Xmu/Xct.h
libXmu/include/X11/Xmu/Xmu.h                      X11/Xmu/Xmu.h

libxkbfile/include/X11/extensions/XKBfile.h       X11/extensions/XKBfile.h
libxkbfile/include/X11/extensions/XKM.h           X11/extensions/XKM.h
libxkbfile/include/X11/extensions/XKBbells.h      X11/extensions/XKBbells.h
libxkbfile/include/X11/extensions/XKBconfig.h     X11/extensions/XKBconfig.h
libxkbfile/include/X11/extensions/XKBrules.h      X11/extensions/XKBrules.h
libxkbfile/include/X11/extensions/XKMformat.h     X11/extensions/XKMformat.h

libXfixes/include/X11/extensions/Xfixes.h         X11/extensions/Xfixes.h

libXfont2/include/X11/fonts/fntfil.h              X11/fonts/fntfil.h
libXfont2/include/X11/fonts/fontmisc.h            X11/fonts/fontmisc.h
libXfont2/include/X11/fonts/libxfont2.h           X11/fonts/libxfont2.h
libXfont2/include/X11/fonts/bufio.h               X11/fonts/bufio.h


xorg-server/hw/xwin/glx/gl.xml                    mesalib/src/mapi/glapi/registry/gl.xml
"""

Files=FilesStr.split()

Diff=False

# do linefeed independent compare
def CompareFiles(f1name, f2name):
  try:
    if len(sys.argv)>1:
      print "Comparing",f1name,f2name
    f1 = open(f1name, 'rU')
    f2 = open(f2name, 'rU')

    a = f1.readlines(); f1.close()
    b = f2.readlines(); f2.close()
    for line in difflib.ndiff(a, b):
      if line[0]!=' ':
        print f1name,"and",f2name,"are different"
        return True

    return False
  except:
    if not os.path.exists(f1name): print "\n",f1name,"does not exist\n"
    if not os.path.exists(f2name): print "\n",f2name,"does not exist\n"
    return True

for SrcFile,DestFile in izip(Files[0::2], Files[1::2]):
  Diff |= CompareFiles(SrcFile, DestFile)

if not Diff:
  print "All Files are the same"
