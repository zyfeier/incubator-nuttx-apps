############################################################################
# apps/graphics/lvgl/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# LVGL graphic library

LVGL_DIR = .
LVGL_DIR_NAME = lvgl

# Relax format check for LVGL to avoid errors on prinf() use

-include ./ext/Makefile
-include ./lvgl/examples/examples.mk
-include ./lvgl/src/extra/extra.mk
-include ./lvgl/src/core/lv_core.mk
-include ./lvgl/src/draw/lv_draw.mk
-include ./lvgl/src/font/lv_font.mk
-include ./lvgl/src/gpu/lv_gpu.mk
-include ./lvgl/src/hal/lv_hal.mk
-include ./lvgl/src/misc/lv_misc.mk
-include ./lvgl/src/widgets/lv_widgets.mk

CSRCS += lv_porting/lv_tick_interface.c
CSRCS += lv_porting/lv_fs_interface.c

ifneq ($(CONFIG_LV_USE_BUTTON_INTERFACE),)
CSRCS += lv_porting/lv_button_interface.c
endif

ifneq ($(CONFIG_LV_USE_KEYPAD_INTERFACE),)
CSRCS += lv_porting/lv_keypad_interface.c
endif

ifneq ($(CONFIG_LV_USE_TOUCHPAD_INTERFACE),)
CSRCS += lv_porting/lv_touchpad_interface.c
endif

ifneq ($(CONFIG_LV_USE_ENCODER_INTERFACE),)
CSRCS += lv_porting/lv_encoder_interface.c
endif

ifneq ($(CONFIG_LV_USE_LCDDEV_INTERFACE),)
CSRCS += lv_porting/lv_lcddev_interface.c
endif

ifneq ($(CONFIG_LV_USE_FBDEV_INTERFACE),)
CSRCS += lv_porting/lv_fbdev_interface.c
endif

ifneq ($(CONFIG_LV_USE_FREETYPE_INTERFACE),)
CSRCS += lv_porting/lv_freetype_interface.c
endif

ifneq ($(CONFIG_LV_USE_FFMPEG_INTERFACE),)
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/../external/ffmpeg}
CSRCS += lv_porting/lv_ffmpeg_interface.c
endif

# Set up build configuration and environment

WD := ${shell echo $(CURDIR) | sed -e 's/ /\\ /g'}

CONFIG_GRAPH_LVGL_URL ?= "https://github.com/lvgl/lvgl/archive"

LVGL_VERSION = $(patsubst "%",%,$(strip $(CONFIG_LVGL_VERSION)))
LVGL_TARBALL = v$(LVGL_VERSION).zip

LVGL_UNPACKNAME = lvgl
UNPACK ?= unzip -o

LVGL_UNPACKDIR =  $(WD)/$(LVGL_UNPACKNAME)

$(LVGL_TARBALL):
	$(Q) echo "Downloading: $(LVGL_TARBALL)"
	$(Q) curl -O -L $(CONFIG_GRAPH_LVGL_URL)/$(LVGL_TARBALL)

$(LVGL_UNPACKNAME): $(LVGL_TARBALL)
	$(Q) echo "Unpacking: $(LVGL_TARBALL) -> $(LVGL_UNPACKNAME)"
	$(Q) $(UNPACK) $(LVGL_TARBALL)
	$(Q) mv	lvgl-$(LVGL_VERSION) $(LVGL_UNPACKNAME)
	$(Q) touch $(LVGL_UNPACKNAME)

context::# $(LVGL_UNPACKNAME)

distclean::
#	$(call DELDIR, $(LVGL_UNPACKNAME))
#	$(call DELFILE, $(LVGL_TARBALL))

include $(APPDIR)/Application.mk
