###
# Copyright (C) Fourier Semiconductor Inc. 2016-2020. All rights reserved.
#

# TOPLEVEL=$(PWD)/..
# EXTRA_CFLAGS += -I$(TOPLEVEL)

# EXTRA_CFLAGS += -DDEBUG -DFSM_DEBUG
EXTRA_CFLAGS += -Wall -Werror
MODFLAGS = -fno-pic
CFLAGS_MODULE = $(MODFLAGS)
AFLAGS_MODULE = $(MODFLAGS)

SPRD_SOUND_TREE := sound/soc/codecs/
ccflags-y += -I$(srctree)/$(SPRD_SOUND_TREE)/

snd-soc-fs1815-objs := \
        fsm_core.o \
        fs1815.o \
        fsm_i2c.o \
        fsm_firmware.o \
        fsm_misc.o \
        fsm_sysfs.o

obj-m += snd-soc-fs1815.o


