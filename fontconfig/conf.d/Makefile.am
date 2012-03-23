# 
#  fontconfig/conf.d/Makefile.am
# 
#  Copyright © 2005 Keith Packard
# 
#  Permission to use, copy, modify, distribute, and sell this software and its
#  documentation for any purpose is hereby granted without fee, provided that
#  the above copyright notice appear in all copies and that both that
#  copyright notice and this permission notice appear in supporting
#  documentation, and that the name of the author(s) not be used in
#  advertising or publicity pertaining to distribution of the software without
#  specific, written prior permission.  The authors make no
#  representations about the suitability of this software for any purpose.  It
#  is provided "as is" without express or implied warranty.
# 
#  THE AUTHOR(S) DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
#  EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
#  DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
#  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
#  PERFORMANCE OF THIS SOFTWARE.

DOC_FILES= \
	README

CONF_FILES = \
	10-autohint.conf \
	10-no-sub-pixel.conf \
	10-sub-pixel-bgr.conf \
	10-sub-pixel-rgb.conf \
	10-sub-pixel-vbgr.conf \
	10-sub-pixel-vrgb.conf \
	10-unhinted.conf \
	11-lcdfilter-default.conf \
	11-lcdfilter-legacy.conf \
	11-lcdfilter-light.conf \
	20-fix-globaladvance.conf \
	20-unhint-small-vera.conf \
	25-unhint-nonlatin.conf \
	30-urw-aliases.conf \
	30-metric-aliases.conf \
	40-nonlatin.conf \
	45-latin.conf \
	49-sansserif.conf \
	50-user.conf \
	51-local.conf \
	60-latin.conf \
	65-fonts-persian.conf \
	65-khmer.conf \
	65-nonlatin.conf \
	69-unifont.conf \
	70-no-bitmaps.conf \
	70-yes-bitmaps.conf \
	80-delicious.conf \
	90-synthetic.conf

CONF_LINKS = \
	20-fix-globaladvance.conf \
	20-unhint-small-vera.conf \
	30-urw-aliases.conf \
	30-metric-aliases.conf \
	40-nonlatin.conf \
	45-latin.conf \
	49-sansserif.conf \
	50-user.conf \
	51-local.conf \
	60-latin.conf \
	65-fonts-persian.conf \
	65-nonlatin.conf \
	69-unifont.conf \
	80-delicious.conf \
	90-synthetic.conf

EXTRA_DIST = $(CONF_FILES) $(DOC_FILES)

configdir=$(CONFDIR)
confavaildir=$(configdir)/conf.avail
confddir=$(configdir)/conf.d

confavail_DATA=$(CONF_FILES)
confd_DATA=$(DOC_FILES)

install-data-local:
	mkdir -p ${DESTDIR}${confddir}
	@(echo cd ${DESTDIR}${confddir}; \
	 cd ${DESTDIR}${confddir}; \
	 for i in ${CONF_LINKS}; do \
		$(RM) $$i; \
		echo $(RM) $$i";" ln -s ../conf.avail/$$i .; \
		ln -s ../conf.avail/$$i .; \
	 done)
uninstall-local:
	@(echo cd ${DESTDIR}${confddir}; \
	 cd ${DESTDIR}${confddir}; \
	 for i in ${CONF_LINKS}; do \
		echo $(RM) $$i; \
		$(RM) $$i; \
	 done)
