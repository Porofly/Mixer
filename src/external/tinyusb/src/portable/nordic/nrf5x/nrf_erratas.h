// Minimal stub of nrf_erratas.h for TinyUSB nRF5x port.
//
// Production nRF52840 silicon (rev D and later, IC INFO.VARIANT == 'AAD0' etc.)
// has all USB-related anomalies (166, 171, 187) fixed in hardware. Returning
// false avoids applying the workaround code paths in dcd_nrf5x.c.
//
// If a Dongle with older silicon is encountered the workarounds become
// recommended. We can revisit then; current production stock is rev F.

#ifndef MIXER_NRF_ERRATAS_STUB_H
#define MIXER_NRF_ERRATAS_STUB_H

#include <stdbool.h>

static inline bool nrf52_errata_166(void) { return false; }
static inline bool nrf52_errata_171(void) { return false; }
static inline bool nrf52_errata_187(void) { return false; }

#endif
