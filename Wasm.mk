#
# Copyright (C) 2020 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

INITIAL_MEMORY ?= 65536

# Toolchain setting

WCC = $(WASI_SDK_ROOT)/bin/clang

WCFLAGS = $(filter-out $(ARCHCPUFLAGS) $(ARCHCFLAGS) $(ARCHINCLUDES) $(ARCHDEFINES) $(ARCHOPTIMIZATION) $(EXTRAFLAGS),$(CFLAGS))
WCFLAGS += --sysroot=$(WASI_SDK_ROOT)/share/wasi-sysroot -nostdlib $(MAXOPTIMIZATION)

WLDFLAGS = -z stack-size=$(STACKSIZE) -Wl,--initial-memory=$(INITIAL_MEMORY) -Wl,--export=main -Wl,--export=__main_argc_argv
WLDFLAGS += -Wl,--export=__heap_base -Wl,--export=__data_end
WLDFLAGS += -Wl,--no-entry -Wl,--strip-all -Wl,--allow-undefined

# Targets follow

WBIN = $(PROGNAME).wasm

WSRCS = $(MAINSRC) $(CSRCS)
WOBJS = $(WSRCS:%.c=%.wo)

ifeq ($(V),0)
  Q ?= @
endif

all:: $(WOBJS)

$(WOBJS): %.wo : %.c
	@ echo "WCC: $^"
	$(Q) $(WCC) $(WCFLAGS) -c $^ -o $@

install:: $(WBIN)
	$(Q) install $(WBIN) $(BINDIR)

$(WBIN): $(WOBJS)
	$(Q) $(WCC) $(WOBJS) $(WCFLAGS) $(WLDFLAGS) -o $(WBIN)

clean::
	$(Q) rm -f $(WOBJS)
	$(Q) rm -f $(WBIN)
