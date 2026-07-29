# SPDX-License-Identifier: GPL-2.0-or-later

bootblock-y += bootblock.c

romstage-y += gpio.c
romstage-y += pei_data.c
ramstage-$(CONFIG_MAINBOARD_USE_LIBGFXINIT) += gma-mainboard.ads
ramstage-y += pei_data.c
ramstage-y += mainboard.c
ramstage-y += ramstage.c
# Gate on DRIVERS_OPTION_CFR, not DRIVERS_OPTION_CFR_ENABLED. The latter is a
# promptless def_bool that only flips the former's default; DRIVERS_OPTION_CFR
# is the prompted symbol that actually builds the CFR infrastructure cfr.c
# links against, and a user can turn it off independently.
ramstage-$(CONFIG_DRIVERS_OPTION_CFR) += cfr.c
ramstage-$(CONFIG_TPM2_PTT_ACPI_START) += ptt.c

smm-y += smihandler.c

# hda_verb.c is deliberately absent: src/device/Makefile.mk wildcards
# src/mainboard/$(MAINBOARDDIR)/hda_verb.c into ramstage whenever
# AZALIA_HDA_CODEC_SUPPORT is set (it is, via the PCH's HDA device). Listing it
# here would add it a second time.
