// USB CDC ACM console backend for nRF52840 USB Dongle (PCA10059), used by Mixer.
//
// Provides:
//   - mixer_usb_cdc_init(): initialize TinyUSB device stack and USBD peripheral
//   - mixer_usb_cdc_task(): pump TinyUSB device task (call regularly from main loop)
//   - mixer_usb_cdc_putchar(): blocking byte-write into the CDC TX FIFO with \n -> \r\n
//   - mixer_usb_cdc_getchar(): blocking byte-read from CDC RX FIFO
//   - USBD_IRQHandler / POWER_CLOCK_IRQHandler: ISR shims into TinyUSB
//
// Mixer is a strict-timing protocol (slot length ~us). To minimise jitter on the
// radio/timer ISRs, we run the USB ISR at the lowest priority (NVIC priority 7
// on Cortex-M4 with 3-bit priority fields). printf() output is flushed lazily
// from the FIFO by tud_task() during the round-idle window in main.c.

#include "tusb.h"
#include "nrf.h"
#include "gpi/platform_spec.h"
#include "gpi/platform.h"

#include <stdint.h>
#include <stdbool.h>

// TinyUSB hook declared in dcd_nrf5x.c. We must call this when VBUS-related
// power events fire so the stack can raise/lower the USB pull-up.
extern void tusb_hal_nrf_power_event(uint32_t event);

// Mirrors the values nrfx_power assigns; matches the comment in dcd_nrf5x.c.
enum {
  MIXER_USB_EVT_DETECTED = 0,
  MIXER_USB_EVT_REMOVED  = 1,
  MIXER_USB_EVT_READY    = 2,
};

void mixer_usb_cdc_init(void)
{
  // Enable USB regulator (5V->3.3V) inside POWER block.
  // The regulator must come up before USBD is enabled.
  NRF_POWER->DCDCEN = 1;

  // Configure POWER USB-detect interrupts so we are notified of VBUS state
  // transitions. TinyUSB will call USBD ENABLE/DISABLE based on these.
  NRF_POWER->INTENSET =
      POWER_INTENSET_USBDETECTED_Msk |
      POWER_INTENSET_USBREMOVED_Msk  |
      POWER_INTENSET_USBPWRRDY_Msk;

  // Lowest possible NVIC priority for USB-related interrupts so they never
  // preempt Mixer's radio/timer slot ISRs.
  NVIC_SetPriority(POWER_CLOCK_IRQn, 7);
  NVIC_SetPriority(USBD_IRQn,        7);
  NVIC_EnableIRQ(POWER_CLOCK_IRQn);

  // If VBUS is already present at boot (Dongle is plugged in), prime TinyUSB.
  if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) {
    tusb_hal_nrf_power_event(MIXER_USB_EVT_DETECTED);
  }
  if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_OUTPUTRDY_Msk) {
    tusb_hal_nrf_power_event(MIXER_USB_EVT_READY);
  }

  // Initialise the device stack on root-hub port 0.
  tud_init(0);
}

void mixer_usb_cdc_task(void)
{
  tud_task();
}

// Blocking putchar: enqueue into CDC TX FIFO. If FIFO is full, pump tud_task()
// to drain it. Converts '\n' to '\r\n' for terminal sanity.
void mixer_usb_cdc_putchar(char c)
{
  if (!tud_cdc_connected()) {
    // Host has not opened the port yet -- drop the byte to avoid blocking the
    // application before a serial terminal is attached.
    return;
  }

  if (c == '\n') {
    while (tud_cdc_write_char('\r') == 0) tud_task();
  }
  while (tud_cdc_write_char(c) == 0) tud_task();

  // Flush only on newline to amortise USB transfers. The CDC stack also flushes
  // automatically when the internal EP buffer fills.
  if (c == '\n') {
    tud_cdc_write_flush();
  }
}

int mixer_usb_cdc_getchar(void)
{
  while (!tud_cdc_available()) {
    tud_task();
  }
  uint8_t c;
  uint32_t n = tud_cdc_read(&c, 1);
  return (n == 1) ? (int)c : -1;
}

//--------------------------------------------------------------------+
// ISR shims
//--------------------------------------------------------------------+

void USBD_IRQHandler(void)
{
  tud_int_handler(0);
}

// POWER_CLOCK shares one IRQ on nRF52840 -- we must dispatch USB power events
// to TinyUSB while leaving the rest of the POWER/CLOCK domain untouched (Mixer
// platform.c manages HFCLK/LFCLK directly via polled tasks/events).
void POWER_CLOCK_IRQHandler(void)
{
  if (NRF_POWER->EVENTS_USBDETECTED) {
    NRF_POWER->EVENTS_USBDETECTED = 0;
    tusb_hal_nrf_power_event(MIXER_USB_EVT_DETECTED);
  }
  if (NRF_POWER->EVENTS_USBPWRRDY) {
    NRF_POWER->EVENTS_USBPWRRDY = 0;
    tusb_hal_nrf_power_event(MIXER_USB_EVT_READY);
  }
  if (NRF_POWER->EVENTS_USBREMOVED) {
    NRF_POWER->EVENTS_USBREMOVED = 0;
    tusb_hal_nrf_power_event(MIXER_USB_EVT_REMOVED);
  }
}
