// Minimal stub of nrfx_clock.h for TinyUSB nRF5x port.
// Provides only the inline helpers that dcd_nrf5x.c references when
// CFG_TUD_NRF_NRFX_VERSION == 1, implemented via direct CMSIS register access.
//
// This avoids dragging the entire Nordic nrfx HAL into the build.

#ifndef MIXER_NRFX_CLOCK_STUB_H
#define MIXER_NRFX_CLOCK_STUB_H

#include "nrf.h"
#include <stdbool.h>
#include <stdint.h>

// The CMSIS device header bundled with the Mixer tutorial predates the
// USBD.EVENTCAUSE.USBWUALLOWED bit (bit 7), but recent TinyUSB references it.
// Provide the missing definitions so dcd_nrf5x.c builds cleanly.
#ifndef USBD_EVENTCAUSE_USBWUALLOWED_Pos
  #define USBD_EVENTCAUSE_USBWUALLOWED_Pos  (7UL)
  #define USBD_EVENTCAUSE_USBWUALLOWED_Msk  (0x1UL << USBD_EVENTCAUSE_USBWUALLOWED_Pos)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Event/task identifiers -- we only need the symbolic names; the helpers
// translate them into direct register writes below.
typedef enum {
    NRF_CLOCK_EVENT_HFCLKSTARTED = 0,
    NRF_CLOCK_EVENT_LFCLKSTARTED = 1,
} nrf_clock_event_t;

typedef enum {
    NRF_CLOCK_TASK_HFCLKSTART = 0,
    NRF_CLOCK_TASK_HFCLKSTOP  = 1,
    NRF_CLOCK_TASK_LFCLKSTART = 2,
    NRF_CLOCK_TASK_LFCLKSTOP  = 3,
} nrf_clock_task_t;

typedef enum {
    NRF_CLOCK_HFCLK_LOW_ACCURACY  = 0,  // HFINT
    NRF_CLOCK_HFCLK_HIGH_ACCURACY = 1,  // HFXO
} nrf_clock_hfclk_t;

static inline void nrf_clock_event_clear(nrf_clock_event_t event) {
    switch (event) {
        case NRF_CLOCK_EVENT_HFCLKSTARTED: NRF_CLOCK->EVENTS_HFCLKSTARTED = 0; break;
        case NRF_CLOCK_EVENT_LFCLKSTARTED: NRF_CLOCK->EVENTS_LFCLKSTARTED = 0; break;
    }
}

static inline void nrf_clock_task_trigger(nrf_clock_task_t task) {
    switch (task) {
        case NRF_CLOCK_TASK_HFCLKSTART: NRF_CLOCK->TASKS_HFCLKSTART = 1; break;
        case NRF_CLOCK_TASK_HFCLKSTOP:  NRF_CLOCK->TASKS_HFCLKSTOP  = 1; break;
        case NRF_CLOCK_TASK_LFCLKSTART: NRF_CLOCK->TASKS_LFCLKSTART = 1; break;
        case NRF_CLOCK_TASK_LFCLKSTOP:  NRF_CLOCK->TASKS_LFCLKSTOP  = 1; break;
    }
}

static inline bool nrf_clock_hf_is_running(nrf_clock_hfclk_t accuracy) {
    uint32_t stat = NRF_CLOCK->HFCLKSTAT;
    if (!(stat & CLOCK_HFCLKSTAT_STATE_Msk)) {
        return false;
    }
    if (accuracy == NRF_CLOCK_HFCLK_HIGH_ACCURACY) {
        return (stat & CLOCK_HFCLKSTAT_SRC_Msk) ==
               (CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos);
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // MIXER_NRFX_CLOCK_STUB_H
