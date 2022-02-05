############################################################################
# apps/system/libuv/Makefile
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
-include ./ext/Makefile

LIBUV_PATCHS ?= $(sort $(wildcard $(LIBUV_DIR)/000*.patch))

LIBUV_VERSION  = 1.41.0
LIBUV_UNPACK   = libuv
LIBUV_TARBALL  = v$(LIBUV_VERSION).zip
LIBUV_URL_BASE = https://github.com/libuv/libuv/archive/refs/tags/
LIBUV_URL      = $(LIBUV_URL_BASE)/$(LIBUV_TARBALL)


$(LIBUV_TARBALL):
	@echo "Downloading: $(LIBUV_TARBALL)"
	$(Q) curl -L $(LIBUV_URL)

$(LIBUV_UNPACK): $(LIBUV_TARBALL)
	@echo "Unpacking: $(LIBUV_TARBALL) -> $(LIBUV_UNPACK)"
	$(call DELDIR, $(LIBUV_UNPACK))
	$(Q) unzip $(LIBUV_TARBALL)
	$(Q) move libuv-$(LIBUV_VERSION) $(LIBUV_UNPACK)
	$(Q) cat $(LIBUV_PATCHS) | \
		patch -s -N -d $(LIBUV_UNPACK) -p1

$(LIBUV_UNPACK)/.patch: $(LIBUV_UNPACK)
	$(Q) touch $(LIBUV_UNPACK)/.patch

# Build libuv library

CFLAGS += -I$(LIBUV_UNPACK)/src
CFLAGS += -I$(LIBUV_UNPACK)/src/unix
CFLAGS += -I$(LIBUV_UNPACK)/test
CFLAGS += -Wno-shadow
CFLAGS += -DDEF_THREADPOOL_SIZE=CONFIG_LIBUV_THREADPOOL_SIZE

VPATH += $(LIBUV_UNPACK)/src
VPATH += $(LIBUV_UNPACK)/src/unix
VPATH += $(LIBUV_UNPACK)/test

DEPPATH += --dep-path $(LIBUV_UNPACK)/src
DEPPATH += --dep-path $(LIBUV_UNPACK)/src/unix
DEPPATH += --dep-path $(LIBUV_UNPACK)/test

CSRCS += core.c
CSRCS += poll.c
CSRCS += loop.c
CSRCS += thread.c
CSRCS += no-proctitle.c
CSRCS += posix-hrtime.c
CSRCS += posix-poll.c
CSRCS += uv-data-getter-setters.c
CSRCS += version.c
CSRCS += idna.c
CSRCS += no-fsevents.c
CSRCS += uv-common.c
CSRCS += strscpy.c
CSRCS += random-devurandom.c
CSRCS += random.c
CSRCS += nuttx.c
CSRCS += tty.c
CSRCS += loop-watcher.c
CSRCS += signal.c
CSRCS += stream.c
CSRCS += threadpool.c
CSRCS += async.c
CSRCS += pipe.c
CSRCS += fs.c
CSRCS += fs-poll.c
CSRCS += timer.c
CSRCS += process.c
CSRCS += sysinfo-loadavg.c
CSRCS += sysinfo-memory.c

ifneq ($(CONFIG_NET),)
CSRCS += getaddrinfo.c
CSRCS += inet.c
endif

ifneq ($(CONFIG_NET_TCP),)
CSRCS += tcp.c
endif

ifneq ($(CONFIG_NET_UDP),)
CSRCS += udp.c
endif

context::# $(LIBUV_UNPACK)/.patch

distclean::
#	$(call DELDIR, $(LIBUV_UNPACK))
#	$(call DELFILE, $(LIBUV_TARBALL))

include $(APPDIR)/Application.mk
