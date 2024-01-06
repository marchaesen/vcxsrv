#
# Copyright (c) 2010, 2022, Oracle and/or its affiliates.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

appmandir = $(APP_MAN_DIR)
libmandir = $(LIB_MAN_DIR)
libman_PRE=$(all_shadows:=.man) \
	XpmCreateBuffer.man	\
	XpmCreateData.man	\
	XpmCreateImage.man	\
	XpmCreatePixmap.man	\
	XpmCreateXpmImage.man	\
	XpmMisc.man		\
	XpmRead.man		\
	XpmWrite.man

libman_DATA = $(libman_PRE:man=$(LIB_MAN_SUFFIX))

appman_PRE = sxpm.man cxpm.man
appman_DATA = $(appman_PRE:man=$(APP_MAN_SUFFIX))

EXTRA_DIST = $(appman_PRE) $(libman_PRE)
CLEANFILES = $(appman_DATA) $(libman_DATA)
SUFFIXES = .$(APP_MAN_SUFFIX) .$(LIB_MAN_SUFFIX) .man

all_shadows = \
         $(xpmcreatebuffer_shadows)  \
         $(xpmcreatedata_shadows)  \
         $(xpmcreateimage_shadows)  \
         $(xpmcreatepixmap_shadows)  \
         $(xpmcreatexpmimage_shadows)  \
         $(xpmmisc_shadows)  \
         $(xpmread_shadows)  \
         $(xpmwrite_shadows)

xpmcreatebuffer_shadows = \
	XpmCreateBufferFromImage  \
	XpmCreateBufferFromPixmap  \
	XpmCreateBufferFromXpmImage
xpmcreatedata_shadows = \
	XpmCreateDataFromImage  \
	XpmCreateDataFromPixmap  \
	XpmCreateDataFromXpmImage
xpmcreateimage_shadows = \
	XpmCreateImageFromBuffer  \
	XpmCreateImageFromData  \
	XpmCreateImageFromXpmImage
xpmcreatepixmap_shadows = \
	XpmCreatePixmapFromBuffer  \
	XpmCreatePixmapFromData  \
	XpmCreatePixmapFromXpmImage
xpmcreatexpmimage_shadows = \
	XpmCreateXpmImageFromBuffer  \
	XpmCreateXpmImageFromData  \
	XpmCreateXpmImageFromImage  \
	XpmCreateXpmImageFromPixmap
xpmmisc_shadows = \
	XpmLibraryVersion  \
	XpmGetErrorString  \
	XpmFree  \
	XpmFreeExtensions  \
	XpmAttributesSize  \
	XpmFreeAttributes  \
	XpmFreeXpmImage  \
	XpmFreeXpmInfo
xpmread_shadows = \
	XpmReadFileToImage  \
	XpmReadFileToPixmap  \
	XpmReadFileToBuffer  \
	XpmReadFileToData  \
	XpmReadFileToXpmImage

xpmwrite_shadows = \
	XpmWriteFileFromPixmap  \
	XpmWriteFileFromImage  \
	XpmWriteFileFromXpmImage  \
	XpmWriteFileFromBuffer

# String replacements in MAN_SUBSTS now come from xorg-macros.m4 via configure
.man.$(APP_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@

.man.$(LIB_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@
