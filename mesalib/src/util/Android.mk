# Mesa 3-D graphics library
#
# Copyright (C) 2014 Tomasz Figa <tomasz.figa@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

# ---------------------------------------
# Build libmesa_util
# ---------------------------------------

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(MESA_UTIL_FILES) \
	$(XMLCONFIG_FILES)

LOCAL_C_INCLUDES := \
	external/zlib \
	$(MESA_TOP)/src/mesa \
	$(MESA_TOP)/src/mapi \
	$(MESA_TOP)/src/gallium/include \
	$(MESA_TOP)/src/gallium/auxiliary

LOCAL_SHARED_LIBRARIES := \
	libexpat

LOCAL_MODULE := libmesa_util

# Generated sources

LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-generated-sources-dir)

LOCAL_EXPORT_C_INCLUDE_DIRS := $(intermediates)

UTIL_GENERATED_SOURCES := $(addprefix $(intermediates)/,$(MESA_UTIL_GENERATED_FILES))
LOCAL_GENERATED_SOURCES := $(UTIL_GENERATED_SOURCES)

MESA_DRI_OPTIONS_H := $(intermediates)/xmlpool/options.h
LOCAL_GENERATED_SOURCES += $(MESA_DRI_OPTIONS_H)

#
# Generate options.h from gettext translations.
#

MESA_DRI_OPTIONS_LANGS := de es nl fr sv
POT := $(intermediates)/xmlpool.pot

$(POT): $(LOCAL_PATH)/xmlpool/t_options.h
	@mkdir -p $(dir $@)
	xgettext -L C --from-code utf-8 -o $@ $<

$(intermediates)/xmlpool/%.po: $(LOCAL_PATH)/xmlpool/%.po $(POT)
	lang=$(basename $(notdir $@)); \
	mkdir -p $(dir $@); \
	if [ -f $< ]; then \
		msgmerge -o $@ $^; \
	else \
		msginit -i $(POT) \
			-o $@ \
			--locale=$$lang \
			--no-translator; \
		sed -i -e 's/charset=.*\\n/charset=UTF-8\\n/' $@; \
	fi

PRIVATE_SCRIPT := $(LOCAL_PATH)/xmlpool/gen_xmlpool.py
PRIVATE_LOCALEDIR := $(intermediates)/xmlpool
PRIVATE_TEMPLATE_HEADER := $(LOCAL_PATH)/xmlpool/t_options.h
PRIVATE_MO_FILES := $(MESA_DRI_OPTIONS_LANGS:%=$(intermediates)/xmlpool/%/LC_MESSAGES/options.mo)

LOCAL_GENERATED_SOURCES += $(PRIVATE_MO_FILES)

$(LOCAL_GENERATED_SOURCES): PRIVATE_PYTHON := $(MESA_PYTHON2)

$(PRIVATE_MO_FILES): $(intermediates)/xmlpool/%/LC_MESSAGES/options.mo: $(intermediates)/xmlpool/%.po
	mkdir -p $(dir $@)
	msgfmt -o $@ $<

$(UTIL_GENERATED_SOURCES): PRIVATE_CUSTOM_TOOL = $(PRIVATE_PYTHON) $^ > $@
$(UTIL_GENERATED_SOURCES): $(intermediates)/%.c: $(LOCAL_PATH)/%.py
	$(transform-generated-source)

$(MESA_DRI_OPTIONS_H): PRIVATE_CUSTOM_TOOL = $(PRIVATE_PYTHON) $^ $(PRIVATE_TEMPLATE_HEADER) \
		$(PRIVATE_LOCALEDIR) $(MESA_DRI_OPTIONS_LANGS) > $@
$(MESA_DRI_OPTIONS_H): $(PRIVATE_SCRIPT) $(PRIVATE_TEMPLATE_HEADER) $(PRIVATE_MO_FILES)
	$(transform-generated-source)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
