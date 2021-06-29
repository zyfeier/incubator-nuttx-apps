############################################################################
# apps/system/dmalloc/Makefile
#
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
#
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_EXECINFO_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_STDARG_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_STDIO_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_STDLIB_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_STRING_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_STRINGS_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_SIGNAL_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_SYS_CYGWIN_H=0}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_SYS_MMAN_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_SYS_TYPES_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_UNISTD_H=1}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_W32API_WINBASE_H=0}
CFLAGS += ${shell $(DEFINE) "$(CC)" HAVE_W32API_WINDEF_H=0}
CFLAGS += ${shell $(INCDIR) "$(CC)" $(TOPDIR)/mm/dmalloc}

CSRCS = $(TOPDIR)/mm/dmalloc/dmalloc/dmalloc_argv.c
MAINSRC = $(TOPDIR)/mm/dmalloc/dmalloc/dmalloc.c

PROGNAME = $(CONFIG_SYSTEM_DMALLOC_PROGNAME)
PRIORITY = $(CONFIG_SYSTEM_DMALLOC_PRIORITY)
STACKSIZE = $(CONFIG_SYSTEM_DMALLOC_STACKSIZE)
MODULE = $(CONFIG_SYSTEM_DMALLOC)

include $(APPDIR)/Application.mk
