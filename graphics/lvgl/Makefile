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

-include ./lvgl/lvgl.mk

-include ./ext/Makefile

CSRCS += lv_porting/lv_porting.c
CSRCS += lv_porting/lv_tick_interface.c

ifneq ($(CONFIG_LV_USE_SYSLOG_INTERFACE),)
CSRCS += lv_porting/lv_syslog_interface.c
endif

ifneq ($(CONFIG_LIBUV),)
CSRCS += lv_porting/lv_uv_interface.c
endif

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
ifneq ($(CONFIG_ARM_HAVE_NEON),)
ASRCS += lv_porting/rotate_neon.S
endif
endif

ifneq ($(CONFIG_LV_USE_GPU_INTERFACE),)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../vendor/bes/$(CONFIG_BES_DDK_DIR)/platform/drivers/graphic/gpu/drv/vglite/inc
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../vendor/bes/$(CONFIG_BES_DDK_DIR)/platform/drivers/graphic/gpu
CSRCS += lv_porting/lv_gpu_interface.c
CSRCS += lv_porting/gpu/lv_gpu_decoder.c
CSRCS += lv_porting/gpu/lv_gpu_evoreader.c
CSRCS += lv_porting/gpu/lv_gpu_draw_utils.c
CSRCS += lv_porting/gpu/lv_gpu_draw_layer.c
ifneq ($(CONFIG_LV_COLOR_DEPTH_32),)
CSRCS += lv_porting/gpu/fast_gaussian_blur.c
endif
ifneq ($(CONFIG_ARM_HAVE_MVE),)
CSRCS += lv_porting/gpu/lv_gpu_draw_blend.c
endif

ifneq ($(CONFIG_LV_GPU_USE_CUSTOM_HEAP),)
ifneq ($(CONFIG_LV_MEM_CUSTOM),)
CSRCS += lv_porting/gpu/tlsf.c
endif
endif
ifneq ($(CONFIG_LV_GPU_DRAW_LINE),)
CSRCS += lv_porting/gpu/lv_gpu_draw_line.c
endif
ifneq ($(CONFIG_LV_GPU_DRAW_ARC),)
CSRCS += lv_porting/gpu/lv_gpu_draw_arc.c
endif
ifneq ($(CONFIG_LV_GPU_DRAW_POLYGON),)
CSRCS += lv_porting/gpu/lv_gpu_draw_polygon.c
endif
ifneq ($(CONFIG_LV_GPU_DRAW_RECT),)
CSRCS += lv_porting/gpu/lv_gpu_draw_rect.c
endif
ifneq ($(CONFIG_LV_GPU_DRAW_IMG),)
CSRCS += lv_porting/gpu/lv_gpu_draw_img.c
endif

endif

ifneq ($(CONFIG_LV_USE_DECODER_JPEG_TURBO),)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/libjpeg-turbo/libjpeg-turbo
CSRCS += lv_porting/decoder/jpeg_turbo/lv_jpeg_turbo.c
endif

ifneq ($(CONFIG_LV_USE_DECODER_LODEPNG),)
CSRCS += lv_porting/decoder/lodepng/lodepng.c
CSRCS += lv_porting/decoder/lodepng/lv_lodepng.c
endif

ifneq ($(CONFIG_LV_USE_LIBPNG),)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/libpng
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/libpng/libpng
endif

ifneq ($(CONFIG_LV_USE_FFMPEG),)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/ffmpeg/ffmpeg
endif

ifneq ($(CONFIG_LV_USE_RLOTTIE),)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/rlottie/rlottie/inc
endif

ifneq ($(CONFIG_LV_ATTRIBUTE_FAST_MEM_CUSTOM_SECTION_NAME), "")
CFLAGS   += "-DLV_ATTRIBUTE_FAST_MEM=__attribute__((section(CONFIG_LV_ATTRIBUTE_FAST_MEM_CUSTOM_SECTION_NAME)))"
CXXFLAGS += "-DLV_ATTRIBUTE_FAST_MEM=__attribute__((section(CONFIG_LV_ATTRIBUTE_FAST_MEM_CUSTOM_SECTION_NAME)))"
endif

ifneq ($(CONFIG_LV_MEM_INTERFACE_CUSTOM_SIZE), 0)
CFLAGS += "-DLV_MEM_CUSTOM_ALLOC=lv_mem_custom_alloc"
CFLAGS += "-DLV_MEM_CUSTOM_FREE=lv_mem_custom_free"
CFLAGS += "-DLV_MEM_CUSTOM_REALLOC=lv_mem_custom_realloc"
CSRCS  += lv_porting/lv_mem_interface.c
endif

ifneq ($(CONFIG_LV_ASSERT_HANDLER_INCLUDE), "")
CFLAGS += "-DLV_ASSERT_HANDLER=ASSERT(0);"
endif

include $(APPDIR)/Application.mk
