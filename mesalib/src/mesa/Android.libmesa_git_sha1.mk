# Mesa 3-D graphics library
#
# Copyright (C) 2017 Mauro Rossi <issor.oruam@gmail.com>
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

# ----------------------------------------------------------------------
# libmesa_git_sha1
# ----------------------------------------------------------------------

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libmesa_git_sha1

LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-generated-sources-dir)

# dummy.c source file is generated to meet the build system's rules.
LOCAL_GENERATED_SOURCES += $(intermediates)/dummy.c

$(intermediates)/dummy.c:
	@mkdir -p $(dir $@)
	@echo "Gen Dummy: $(PRIVATE_MODULE) <= $(notdir $(@))"
	$(hide) touch $@

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, git_sha1.h)

$(intermediates)/git_sha1.h: $(MESA_TOP)/src/git_sha1.h.in $(wildcard $(MESA_TOP)/.git/logs/HEAD)
	@mkdir -p $(dir $@)
	@echo "GIT-SHA1: $(PRIVATE_MODULE) <= git"
	$(hide) $(MESA_PYTHON2) $(MESA_TOP)/bin/git_sha1_gen.py --output $@

LOCAL_EXPORT_C_INCLUDE_DIRS := $(intermediates)

include $(MESA_COMMON_MK)
include $(BUILD_STATIC_LIBRARY)
