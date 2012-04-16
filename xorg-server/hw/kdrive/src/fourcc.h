
/*
   This header file contains listings of STANDARD guids for video formats.
   Please do not place non-registered, or incomplete entries in this file.
   A list of some popular fourcc's are at: http://www.webartz.com/fourcc/
   For an explanation of fourcc <-> guid mappings see RFC2361.
*/

#ifndef _XF86_FOURCC_H_
#define _XF86_FOURCC_H_ 1

#define FOURCC_YUY2 0x32595559
#define XVIMAGE_YUY2 \
   { \
	FOURCC_YUY2, \
        XvYUV, \
	LSBFirst, \
	{'Y','U','Y','2', \
	  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
	16, \
	XvPacked, \
	1, \
	0, 0, 0, 0, \
	8, 8, 8, \
	1, 2, 2, \
	1, 1, 1, \
	{'Y','U','Y','V', \
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
	XvTopToBottom \
   }

#define FOURCC_YV12 0x32315659
#define XVIMAGE_YV12 \
   { \
	FOURCC_YV12, \
        XvYUV, \
	LSBFirst, \
	{'Y','V','1','2', \
	  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
	12, \
	XvPlanar, \
	3, \
	0, 0, 0, 0, \
	8, 8, 8, \
	1, 2, 2, \
	1, 2, 2, \
	{'Y','V','U', \
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
	XvTopToBottom \
   }

#define FOURCC_I420 0x30323449
#define XVIMAGE_I420 \
   { \
	FOURCC_I420, \
        XvYUV, \
	LSBFirst, \
	{'I','4','2','0', \
	  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
	12, \
	XvPlanar, \
	3, \
	0, 0, 0, 0, \
	8, 8, 8, \
	1, 2, 2, \
	1, 2, 2, \
	{'Y','U','V', \
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
	XvTopToBottom \
   }

#define FOURCC_UYVY 0x59565955
#define XVIMAGE_UYVY \
   { \
	FOURCC_UYVY, \
        XvYUV, \
	LSBFirst, \
	{'U','Y','V','Y', \
	  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
	16, \
	XvPacked, \
	1, \
	0, 0, 0, 0, \
	8, 8, 8, \
	1, 2, 2, \
	1, 1, 1, \
	{'U','Y','V','Y', \
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
	XvTopToBottom \
   }

#define FOURCC_IA44 0x34344149
#define XVIMAGE_IA44 \
   { \
        FOURCC_IA44, \
        XvYUV, \
        LSBFirst, \
        {'I','A','4','4', \
          0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
        8, \
        XvPacked, \
        1, \
        0, 0, 0, 0, \
        8, 8, 8, \
        1, 1, 1, \
        1, 1, 1, \
        {'A','I', \
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
        XvTopToBottom \
   }

#define FOURCC_AI44 0x34344941
#define XVIMAGE_AI44 \
   { \
        FOURCC_AI44, \
        XvYUV, \
        LSBFirst, \
        {'A','I','4','4', \
          0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
        8, \
        XvPacked, \
        1, \
        0, 0, 0, 0, \
        8, 8, 8, \
        1, 1, 1, \
        1, 1, 1, \
        {'I','A', \
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
        XvTopToBottom \
   }

#endif                          /* _XF86_FOURCC_H_ */
