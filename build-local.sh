#!/bin/sh
export DEVKITPRO=/c/Users/hella/devkit-nin/devkitpro/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$DEVKITARM/bin:/usr/bin:$PATH
cd /c/Users/hella/Documents/Cogs/Nintendont-master || exit 1
exec /usr/bin/make -j4
