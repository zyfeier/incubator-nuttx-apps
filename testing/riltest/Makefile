############################################################################
# apps/testing/riltest/Makefile
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

CXXEXT = .cpp

RIL_TEST_SRC = $(CURDIR)/riltest/ril_testbench/src/
RIL_TEST_INCLUDE = $(CURDIR)/riltest/ril_testbench/include/
RIL_TEST_TOOLS = $(CURDIR)/riltest/ril_testbench/tool
XRIL_SRC = $(CURDIR)/riltest/xril/src

# the following are rules for building ril stub
# NOTE that c builtin program must comes first! why?
# That's because NuttX always compiles C files first for
# builtin app and the first PROGNAME will be extracted and
# assigned as the main entrance for the next builtin app
# no matter its CPP/C. (refer to Application.mk)

ifneq ($(CONFIG_TESTING_RILD_STUB),)

CSRCS += $(wildcard $(XRIL_SRC)/librilutils/src/*.c)
CSRCS += $(wildcard $(CURDIR)/riltest/modem_common/src/*.c)
CSRCS += $(XRIL_SRC)/vendor/src/vendor_stub.c

# libparcel already added by ril test tool
CXXSRCS += $(XRIL_SRC)/libril/src/ril_dispatcher.cpp
CXXSRCS += $(XRIL_SRC)/libril/src/ril_eventman.cpp
CXXSRCS += $(XRIL_SRC)/libril/src/ril_logman.cpp
CXXSRCS += $(XRIL_SRC)/libril/src/ril.cpp

CXXFLAGS += -DRIL_LIB -DLOCAL_TEST
CFLAGS += -DRIL_LIB -DLOCAL_TEST

# path of headers for building RILD
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/modem_common/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/xril/src/librilutils/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/xril/src/libril/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/xril/src/vendor/include

CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/modem_common/include
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/xril/src/librilutils/include
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/testing/riltest/riltest/xril/src/libril/include

PROGNAME  += $(CONFIG_TESTING_RILD_PROGNAME)
PRIORITY  += $(CONFIG_TESTING_RILD_PRIORITY)
STACKSIZE += $(CONFIG_TESTING_RILD_STACKSIZE)
MODULE    += $(CONFIG_TESTING_RILD_STUB)

MAINSRC += $(XRIL_SRC)/rild/src/rild.c

endif

# Rules for building ril test tool

# testcases output path
ifneq ($(CONFIG_RIL_TESTCASES_OUT),"")
RIL_TESTCASES_OUT = $(patsubst "%",%,$(CONFIG_RIL_TESTCASES_OUT))/testcases_gen
else
# default path
RIL_TESTCASES_OUT = $(RIL_TEST_SRC)/default_gen
endif

# testcases configuration path
ifneq ($(CONFIG_RIL_TEST_CONFIGS_PATH),"")
RIL_TEST_CONFIGS_PATH = $(patsubst "%",%,$(CONFIG_RIL_TEST_CONFIGS_PATH))
else
# default path
RIL_TEST_CONFIGS_PATH = $(RIL_TEST_TOOLS)/
endif

# ril test config files
RIL_TEST_CONFIG_JSON = $(RIL_TEST_CONFIGS_PATH)/ril_reqs.json
RIL_TEST_RESP_JSON_OUT = $(RIL_TEST_CONFIGS_PATH)/resp_json_gen

# TODO(Jiawei): which libparcel to use should be configurable after having
# vela's binder lib
CXXSRCS += $(wildcard $(XRIL_SRC)/libparcel/src/*.cpp)

# common sources for building ril testing tool
CXXSRCS += $(wildcard $(RIL_TEST_SRC)/ril_parcel_*.cpp)

# custom sources for building ril testing tool
CSRCS += $(RIL_TESTCASES_OUT)/ril_testcases.c

# common path of headers for building ril test tool

# TODO(Jiawei): libparcel header location should be configurable
LIBPARCEL_HEARDERS := $(APPDIR)/testing/riltest/riltest/xril/src/libparcel/include

# TODO(Jiawei): libril header location should be configurable
LIBRIL_HEADERS := $(APPDIR)/testing/riltest/riltest/xril/include

CFLAGS += ${INCDIR_PREFIX}${LIBPARCEL_HEARDERS}
CFLAGS += ${INCDIR_PREFIX}${LIBRIL_HEADERS}

CXXFLAGS += ${INCDIR_PREFIX}${LIBPARCEL_HEARDERS}
CXXFLAGS += ${INCDIR_PREFIX}${LIBRIL_HEADERS}

# custom headers for building ril testing tool
CFLAGS += ${INCDIR_PREFIX}$(RIL_TESTCASES_OUT)
CFLAGS += ${INCDIR_PREFIX}$(RIL_TEST_INCLUDE)
CXXFLAGS += ${INCDIR_PREFIX}$(RIL_TESTCASES_OUT)
CXXFLAGS += ${INCDIR_PREFIX}$(RIL_TEST_INCLUDE)

# built-in app configuration
PROGNAME  += $(CONFIG_TESTING_RIL_TEST_PROGNAME)
PRIORITY  += $(CONFIG_TESTING_RIL_TEST_PRIORITY)
STACKSIZE += $(CONFIG_TESTING_RIL_TEST_STACKSIZE)
MODULE    += $(CONFIG_TESTING_RIL_TEST)

MAINSRC += $(RIL_TEST_SRC)/rilreq_tool.cpp

depend:: gen_test_cases

$(RIL_TEST_CONFIGS_PATH)/.ril_resp: $(RIL_TEST_TOOLS)/ril_resp_rawdata
	touch $(RIL_TEST_CONFIGS_PATH)/.ril_resp
	mkdir -p $(RIL_TEST_RESP_JSON_OUT)
	$(RIL_TEST_TOOLS)/gen_ril_resps.sh $(RIL_TEST_TOOLS) $(RIL_TEST_RESP_JSON_OUT)

gen_test_cases: $(RIL_TEST_CONFIGS_PATH)/.ril_resp
	mkdir -p $(RIL_TESTCASES_OUT)
	$(RIL_TEST_TOOLS)/gen_ril_testcases.py $(RIL_TEST_CONFIG_JSON) $(RIL_TEST_RESP_JSON_OUT) $(RIL_TESTCASES_OUT)/

$(RIL_TEST_TOOLS)/ril_resp_rawdata: $(RIL_TEST_TOOLS)/ril_resp_rawdata.c $(RIL_TEST_TOOLS)/ril_resp_rawdata_main.c
	$(CC) -m32 -I $(LIBRIL_HEADERS) -o $@ $^

.PHONY: gen_test_cases

distclean::
	$(Q) $(call DELFILE, $(RIL_TEST_TOOLS)/ril_resp_rawdata)
	$(Q) $(call DELFILE, $(RIL_TEST_CONFIGS_PATH)/.ril_resp)
	$(Q) $(call DELDIR, $(RIL_TEST_RESP_JSON_OUT))
	$(Q) $(call DELDIR, $(RIL_TESTCASES_OUT))

include $(APPDIR)/Application.mk
