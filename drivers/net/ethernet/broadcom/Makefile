#
# Makefile for the Broadcom network device drivers.
#

obj-$(CONFIG_B44) += b44.o
obj-$(CONFIG_BCM63XX_ENET) += bcm63xx_enet.o
obj-$(CONFIG_BCMGENET) += genet/
obj-$(CONFIG_BNX2) += bnx2.o
obj-$(CONFIG_CNIC) += cnic.o
obj-$(CONFIG_BNX2X) += bnx2x/
obj-$(CONFIG_SB1250_MAC) += sb1250-mac.o
obj-$(CONFIG_BGMAC) += bgmac.o
obj-$(CONFIG_SYSTEMPORT) += bcmsysport.o
obj-$(CONFIG_BNXT) += bnxt/

# FIXME: temporarily silence -Warray-bounds on non W=1+ builds
ifndef KBUILD_EXTRA_WARN
CFLAGS_tg3.o += -Wno-array-bounds
endif
