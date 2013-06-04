/*
Copyright 1996, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.
*/
/*
 * Copyright 1995 by FUJITSU LIMITED
 * This is source code modified by FUJITSU LIMITED under the Joint
 * Development Agreement for the CDE/Motif PST.
 *
 * Modifier: Takanori Tateno   FUJITSU LIMITED
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <locale.h>
#include <Xlib.h>
#include <Xlibint.h>
#include <Xlcint.h>
#include <XlcPubI.h>
#include <XlcGeneric.h>
#include <XomGeneric.h>

/*
   external symbols
*/
extern FontData read_EncodingInfo();
extern int _xudc_get_codeset();

extern int _xudc_utyderror ;
extern int _xudc_utyerror ;

extern unsigned long _xudc_utyerrno ;

#define _XUDC_ERROR -1


/*
    UDC structure define
*/
typedef struct __XUDCGlyphRegion {
    unsigned long  start;
    unsigned long  end;
} _XUDCGlyphRegion ;

/*
 *  "code" no jyoui "i" byte me wo "unsigned char" toshite kaesu.
 */
static unsigned char getbyte(code,i)
unsigned long code;
int	      i;
{
    unsigned long byteL;
    unsigned char byte;
    byteL = code  >> (8*(3-i));
    byte = (unsigned char)(0x000000ff & byteL);
    return(byte);
}
/*
    get codeset which described by charset_str and locale.
    for examples ...
    locale          :  ja_JP
    charset_str     :  JISX0208.1983-0
*/

_xudc_get_codeset(locale,charset_str,codeset,num_codeset)
char *locale;
char *charset_str;
int  **codeset;
int  *num_codeset;
{
    XLCdRec lcdrec;
    XLCd     lcd;
    XLCdPublicRec xlcdp;
    XPointer rdb;
    int num = 0,count,num_ret=0,i,*ret;
    char **value,buf[128],*ptr;


    _xudc_utyderror = 0;
    _xudc_utyerror = 0;

    if((locale == NULL) || (charset_str == NULL)){
        _xudc_utyerror = 5;
        _xudc_utyderror = 1;
	_xudc_utyerrno = 0x04 ;
        return(_XUDC_ERROR);
    }
    if(codeset == NULL){
        _xudc_utyerror = 5;
        _xudc_utyderror = 2;
	_xudc_utyerrno = 0x04 ;
        return(_XUDC_ERROR);
    }

    /*    create XLCd     */
    xlcdp.pub.siname = locale;
    lcdrec.core = (XLCdCore)&xlcdp;
    lcd = &lcdrec;
        /* create X RDB (X NLS DB) */
    rdb = _XlcCreateLocaleDataBase(lcd);
    if(rdb == NULL){
        _xudc_utyerror = 1;
	_xudc_utyerrno = 0x15 ;
        return(_XUDC_ERROR);
    }

    for(num=0;;num++){
        /* XLC_FONTSET */
        sprintf(buf, "fs%d.font.primary", num);
        _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
        if(count < 1){
            break ;
        }
        for(i=0;i<count;i++){
	    if (strlen(value[i]) >= sizeof(buf))
		continue;
            strcpy(buf,value[i]);
            ptr = (char *)strchr(buf,(int)':');
            *ptr = 0;
            if(!_XlcCompareISOLatin1(charset_str,buf)){
                num_ret += 1;
                if(num_ret == 1){
		    ret = Xmalloc(sizeof(int));
                } else {
		    int *prev_ret = ret;

		    ret = Xrealloc(ret, num_ret * sizeof(int));
		    if (ret == NULL){
			Xfree(prev_ret);
		    }
                }
                if(ret == NULL){
		    _xudc_utyerrno = 0x03 ;
                    return(_XUDC_ERROR);
                }
		ret[num_ret-1]=num;
                break ;
            }
        }
    }
    if(num_ret == 0){
        *num_codeset = 0;
        *codeset = NULL;
        return (0xff);
    }

    *num_codeset = num_ret;
    *codeset = ret;
    return 0;
}

static Bool gi_to_vgi(gi,vgi,scope)
unsigned long 	gi,*vgi;
FontScope	scope;
{
    if(scope->shift_direction == (unsigned long)'+'){
        gi  -= scope->shift;
    } else {
        gi  += scope->shift;
    }
    if(gi >= scope->start && gi <= scope->end){
	*vgi = gi;
        return(True);
    }
    return(False);
}

static void shift_area(udc,scope)
_XUDCGlyphRegion *udc;
FontScope scope;
{
    if(scope->shift_direction == (unsigned long)'+'){
        udc->start += scope->shift;
        udc->end   += scope->shift;
    } else {
        udc->start -= scope->shift;
        udc->end   -= scope->shift;
    }
}

/*
    get UDC area with glyph index.
    for examples ...
    locale          :  ja_JP
    charset_str     :  JISX0208.1983-0
*/
_XUDCGetUDCGIArea(locale,codeset,charset_str,gr,num_gr)
char *locale;
int codeset;
char *charset_str;
_XUDCGlyphRegion  **gr;
int  *num_gr;
{
    XLCdRec lcdrec;
    XLCd     lcd;
    XLCdPublicRec xlcdp;
    XPointer rdb;
    _XUDCGlyphRegion *udc;
    int num = 0,count,num_ret=0;
    int i,j,k;
    char **value,buf[128],ptr;
    FontData font_data;


    _xudc_utyderror = 0;
    _xudc_utyerror = 0;
    _xudc_utyerrno = 0x00 ;

    if((locale == NULL) || (charset_str == NULL)){
        _xudc_utyerror = 5;
        _xudc_utyderror = 1;
	_xudc_utyerrno = 0x04;
	_xudc_utyerrno |= (0x0b<<8) ;
        return(_XUDC_ERROR);
    }
    if(gr == NULL){
        _xudc_utyerror = 5;
        _xudc_utyderror = 1;
	_xudc_utyerrno = 0x04;
	_xudc_utyerrno |= (0x0b<<8) ;
        return(_XUDC_ERROR);
    }
    if(num_gr == NULL){
        _xudc_utyerror = 5;
        _xudc_utyderror = 2;
	_xudc_utyerrno = 0x04;
	_xudc_utyerrno |= (0x0b<<8) ;
        return(_XUDC_ERROR);
    }

        /* create XLCd */
    xlcdp.pub.siname = locale;
    lcdrec.core = (XLCdCore)&xlcdp;
    lcd = &lcdrec;
        /* create X RDB (X NLS DB) */
    rdb = _XlcCreateLocaleDataBase(lcd);
    if(rdb == NULL){
        _xudc_utyerror = 1;
	_xudc_utyerrno = 0x15 ;
	_xudc_utyerrno |= (0x0b<<8) ;
        return(_XUDC_ERROR);
    }
    udc = NULL;

        /* XLC_FONTSET */
        sprintf(buf, "fs%d.charset.udc_area", codeset-1);
        _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
        if(count > 0){
            udc = Xmalloc(count * sizeof(_XUDCGlyphRegion));
            if(udc == NULL){
		_xudc_utyerrno = 0x03 ;
		_xudc_utyerrno |= (0x0b<<8) ;
		return(_XUDC_ERROR);
            }
            for(i=0;i<count;i++){
                sscanf(value[i],"\\x%lx,\\x%lx", &(udc[i].start), &(udc[i].end));
            }
        }

    *num_gr = count;

        sprintf(buf, "fs%d.font.primary", codeset-1);
        _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
        if(count > 0){
	    font_data = read_EncodingInfo(count,value);
            for(i=0;i<count;i++){
                if( !_XlcCompareISOLatin1(font_data[i].name,charset_str)){
                    for(j=0;j<(*num_gr);j++){
                    	for(k=0;k<font_data[i].scopes_num;k++){
			    if(udc[j].start == font_data[i].scopes[k].start
                                  && font_data[i].scopes[k].shift){
                                shift_area(&udc[j],&(font_data[i].scopes[k]));
                            }
			}
		    }
		}
	    }
        }

    *gr = udc;
    return 0;
}

/*
 *      Code convert wo tomonau UDC area no kakutoku
 *      GetUDCCPArea() / glyph_to_code()
 *
 */

static int
_xudc_gi_to_vgi(lcd,locale,charset_str,codeset,gi,vgi,charsetname,size)
XLCd 	lcd;
char 	*locale;
char 	*charset_str;
int 	codeset;
unsigned long 	gi;
unsigned long 	*vgi;
char    *charsetname;
int	size;
{
    _XUDCGlyphRegion *udc;
    int num = 0,count,num_ret=0;
    int i,j,k;
    char **value,buf[128],ptr;
    FontData font_data;


    sprintf(buf, "fs%d.charset.name", codeset-1);
    _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
    if(count > 0){
        strcpy(charsetname,value[0]);
    }
    if (count >= size)
	return False;
    sprintf(buf, "fs%d.font.primary", codeset-1);
    _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
    if(count > 0){
	font_data = read_EncodingInfo(count,value);
        for(i=0;i<count;i++){
            if( !_XlcCompareISOLatin1(font_data[i].name,charset_str)){
                for(k=0;k<font_data[i].scopes_num;k++){
	            if( gi_to_vgi(gi,vgi,&(font_data[i].scopes[k])) == True){
			return(True);
                    }
	        }
	    }
        }
    }
/*
    free_fontdata(font_data);
*/
    *vgi = gi;
    return(True);
}

Bool non_standard(lcd,charset)
XLCd 	lcd;
XlcCharSet 	charset;
{
    char buf[256];
    int count,i;
    char **value;
    if(charset->ct_sequence == NULL){
            return(False);
    }
    for(i=0;;i++){
	sprintf(buf, "csd%d.charset_name", i);
	_XlcGetLocaleDataBase(lcd, "XLC_CHARSET_DEFINE", buf, &value, &count);
	if(count > 0){
	    if(!_XlcNCompareISOLatin1(value[0],
			charset->name,strlen(value[0])) ){
		return(True);
	    }
	} else {
            return(False);
	}
    }
}

static Bool
make_none_standard(from,charset,src,size)
char *from;
XlcCharSet 	charset;
char *src;
int size;
{
    int name_len,seq_len,rest_len,i;
    name_len = 2 + strlen(charset->encoding_name) + 1;
    seq_len = strlen(charset->ct_sequence);
    rest_len = strlen(charset->encoding_name) + 1 + strlen(src);
    if (name_len + seq_len + strlen(src) >= size || rest_len >= 0x4000)
	return False;
    strcpy(from,charset->ct_sequence);
    from[seq_len]    = (rest_len >> 7) + 128;
    from[seq_len+1]  = (rest_len & 0x7f) + 128;
    strcpy(&from[seq_len + 2],charset->encoding_name);
    from[seq_len+name_len-1]  = 0x02;  /* STX */
    strcpy(&from[seq_len + name_len],src);
    return True;
}
int
_xudc_glyph_to_code(locale,charset_str,codeset,glyph_index,codepoint)
char 	*locale;
char 	*charset_str;
int 	codeset;
unsigned long 	glyph_index;
unsigned long 	*codepoint;
{
    XLCd 	lcd;
    unsigned char *from; int	from_left;
    unsigned char *to  ; int 	to_left = 10;
    unsigned char *dst;
    unsigned char byte;
    unsigned long from32[25];
    unsigned long to32[25];
    int	     i,j;
    char charsetname[256],src[10];
    XlcConv 	conv;
    XlcCharSet 	charset;
    XPointer args[2];

    from = (unsigned char *)from32;
    to   = (unsigned char *)to32;
    dst  = (unsigned char *)to32;

    memset(dst,0,25);

    lcd = (XLCd)_XlcGenericLoader(locale);

    if (!_xudc_gi_to_vgi(lcd,locale,charset_str,codeset,
	glyph_index,&glyph_index,charsetname,sizeof(charsetname)))
	return(_XUDC_ERROR);

    for(i=0,j=0;i<4;i++){
	byte = getbyte(glyph_index,i);
	if(byte){
	    src[j] = byte;
            j ++;
	}
    }
    src[j] = 0;


    /* get charset */
/*
    sprintf(tmp,"%s%s",charset_str,":GL");
    charset_name = strdup(tmp);
*/
    charset = _XlcGetCharSet(charsetname);
    if(charset == NULL){
	_xudc_utyerrno = 0x16 ;
	return(_XUDC_ERROR);
    }
    /* make ct */
    if( non_standard(lcd,charset)) {
        if (!make_none_standard(from,charset,src,sizeof(from32)))
	    return(_XUDC_ERROR);
    } else if(charset->ct_sequence){
	if (strlen(charset->ct_sequence) + strlen(src) >= sizeof(from32))
	    return(_XUDC_ERROR);
        sprintf((char *)from,"%s%s",charset->ct_sequence,src);
    } else {
        sprintf((char *)from,"%s\0",src);
    }
    /* compound text -> multi byte */
    conv = _XlcOpenConverter(lcd, XlcNCompoundText, lcd, XlcNMultiByte);
    from_left = strlen((char *)from);
    _XlcConvert(conv,(XPointer *)&from,&from_left,
                     (XPointer *)&to,  &to_left,args,0);
    _XlcCloseConverter(conv);
    _XlcDestroyLC(lcd);

    *codepoint = 0;
    for(i=0;dst[i];i++){
        *codepoint = ((*codepoint << 8) | dst[i]) ;
    }
    return(0);
}

typedef struct __XUDCCodeRegion {
	unsigned long start,end;
} _XUDCCodeRegion ;

int
_XUDCGetUDCCPArea(locale,codeset,charset_str,cr,num_cr)
char 		*locale;
int 		codeset;
char 		*charset_str;
_XUDCCodeRegion **cr;
int  *num_cr;
{
    int i,num_gr,ret;
    _XUDCGlyphRegion *gr;
    _XUDCCodeRegion  *crr;

    _xudc_utyerror  = 0;
    _xudc_utyderror = 0;

    if(cr == NULL){
        _xudc_utyerror  = 5;
        _xudc_utyderror = 1;
	_xudc_utyerrno = 0x04 ;
	_xudc_utyerrno |= (0x0a<<8) ;
        return(_XUDC_ERROR);
    }
    if(num_cr == NULL){
        _xudc_utyerror  = 5;
        _xudc_utyderror = 2;
	_xudc_utyerrno = 0x04 ;
	_xudc_utyerrno |= (0x0a<<8) ;
        return(_XUDC_ERROR);
    }

    ret = _XUDCGetUDCGIArea(locale,codeset,charset_str,&gr,&num_gr);
    if(ret == _XUDC_ERROR){
	_xudc_utyerrno &= 0xff ;
	_xudc_utyerrno |= (0x0a<<8) ;
	return(ret);
    }

    crr = Xmalloc(num_gr * sizeof(_XUDCCodeRegion));
    if(crr == NULL){
	Xfree(gr);
	_xudc_utyerrno = 0x03 ;
	_xudc_utyerrno |= (0x0a<<8) ;
	return(_XUDC_ERROR);
    }

    for(i=0;i<num_gr;i++){
	ret = _xudc_glyph_to_code(locale,charset_str,codeset,
		gr[i].start, &(crr[i].start));
	if(ret == _XUDC_ERROR){
	    _xudc_utyerrno |= (0x0a<<8) ;
	    Xfree(gr);
	    Xfree(crr);
	    return(ret);
	}
	ret = _xudc_glyph_to_code(locale,charset_str,codeset,
		gr[i].end, &(crr[i].end));
	if(ret == _XUDC_ERROR){
	    _xudc_utyerrno |= (0x0a<<8) ;
	    Xfree(gr);
	    Xfree(crr);
	    return(ret);
	}
    }
    Xfree(gr);
    *cr = crr;
    *num_cr = num_gr;
    return(0);
}

/*
 *    code_to_glyph()
 *
 */
typedef struct __XUDCGIInf {
    char 		*charset_str;
    unsigned long 	glyph_index;
} _XUDCGIInf ;

/*
 *
 *
 */
static Bool vgi_to_gi(gi,vgi,scope)
unsigned long 	*gi,vgi;
FontScope	scope;
{
    if(vgi >= scope->start && vgi <= scope->end){
        if(scope->shift_direction == (unsigned long)'+'){
            *gi = vgi + scope->shift;
        } else {
            *gi = vgi - scope->shift;
        }
        return(True);
    }
    return(False);
}
/*
 *
 *
 */
static Bool
_xudc_vgi_to_gi(lcd,locale,vglyph,glyph,charset,charsetname,size)
XLCd    lcd;
char    *locale;
unsigned long   vglyph;
unsigned long   *glyph;
XlcCharSet	charset;
char    *charsetname;
int	size;
{
    int num = 0,count,num_ret=0;
    int i,j,k;
    char **value,buf[128],ptr;
    FontData font_data;
    CodeSet cs;


    for(i=0;;i++){
        sprintf(buf, "fs%d.charset.name",i);
        _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
        if(count > 0){
            if(!_XlcNCompareISOLatin1(charset->name,value[0],
			strlen(charset->name))){
		break;
	    }
        } else {
	    _xudc_utyerrno = 0x17 ;
	    return(False);
	}
    }
/*
    sprintf(buf, "fs%d.charset.name", codeset-1);
    _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
    if(count > 0){
        strcpy(charsetname,value[0]);
    }
*/
    sprintf(buf, "fs%d.font.primary", i);
    _XlcGetLocaleDataBase(lcd, "XLC_FONTSET", buf, &value, &count);
    if(count > 0){
	font_data = read_EncodingInfo(count,value);
        for(i=0;i<count;i++){
            for(k=0;k<font_data[i].scopes_num;k++){
	        if( vgi_to_gi(glyph,vglyph,&(font_data[i].scopes[k])) == True){
		    if (strlen(font_data[i].name) >= size)
			return(False);
		    strcpy(charsetname,font_data[i].name);
		    return(True);
                }
	    }
        }
    }
    *glyph = vglyph;
    return(True);
}
int
_xudc_code_to_glyph(locale,codepoint,gi,num_gi)
char 		*locale;
unsigned long 	codepoint;
_XUDCGIInf 	**gi;
int 		*num_gi;
{
    XLCd        lcd;
    unsigned char *from; int    from_left;
    unsigned char *to  ; int    to_left = 10;
    unsigned char *dst;
    unsigned char byte;
    unsigned int from32[25];
    unsigned int to32[25];
    int      i,j;
    char charsetname[256],src[10];
    XlcConv     conv;
    XlcCharSet  charset;
    XPointer args[2];
    unsigned long glyph,vglyph;

    from = (unsigned char *)from32;
    to   = (unsigned char *)to32;
    dst  = (unsigned char *)to32;
    memset(dst,0,25);

    lcd = (XLCd)_XlcGenericLoader(locale);

    for(i=0,j=0;i<4;i++){
	byte = getbyte(codepoint,i);
	if(byte){
	    src[j] = byte;
            j ++;
	}
    }
    src[j] = 0;
    sprintf((char *)from,"%s\0",src);
    /* multi byte -> vgi */
    conv = _XlcOpenConverter(lcd, XlcNMultiByte, lcd, XlcNCharSet);
    from_left = strlen((char *)from);
    args[0] = (XPointer) &charset;
    _XlcConvert(conv,(XPointer *)&from,&from_left,
                     (XPointer *)&to,  &to_left,args,1);

    /* vgi -> gi */
    vglyph = 0;
    for(i=0;dst[i];i++){
        vglyph = ((vglyph << 8) | dst[i]) ;
    }
    if(_xudc_vgi_to_gi(lcd,locale,vglyph,&glyph,charset,charsetname,
			sizeof(charsetname))==False){
        _XlcCloseConverter(conv);
	_XlcDestroyLC(lcd);
        *num_gi = 0;
	return(0);
    }

    _XlcCloseConverter(conv);
    _XlcDestroyLC(lcd);

    *gi = Xmalloc(sizeof(_XUDCGIInf));
    if(*gi == NULL){
	_xudc_utyerrno = 0x03 ;
        return(_XUDC_ERROR);
    }
    (*gi)->charset_str = Xmalloc(strlen(charsetname)+1);
    strcpy((*gi)->charset_str,charsetname);
    (*gi)->glyph_index = glyph;
    *num_gi = 1;
    return(0);
}

