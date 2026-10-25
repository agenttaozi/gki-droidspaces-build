
# Copyright (C) 2014-2020 NXP Semiconductors, All Rights Reserved.
# Copyright 2020 GOODIX 
#
#This program is free software; you can redistribute it and/or modify
#it under the terms of the GNU General Public License version 2 as
#published by the Free Software Foundation.

# make sure that the environment variables ARCH and CROSS_COMPILE
# are set for your architecture and cross compiler.
# And that KDIR points to the kernel to build against.
#
# e.g.:
# export ARCH=arm
# export CROSS_COMPILE=arm-linux-gnueabihf-
# export KDIR=../linux


# In case of out of tree build, build as a module
# (when build inside kernel we will not enter this directory and this will have no effect)

EXTRA_CFLAGS += -Wenum-conversion -Werror



snd-soc-zte-tfa98xx-objs += tfa98xx.o
snd-soc-zte-tfa98xx-objs += tfa_container.o
snd-soc-zte-tfa98xx-objs += tfa_dsp.o
snd-soc-zte-tfa98xx-objs += tfa_init.o

obj-m += snd-soc-zte-tfa98xx.o