// TinyUSB configuration for Mixer on nRF52840 USB Dongle (PCA10059).
// Single CDC ACM interface, device-only, bare-metal (no OS).

#ifndef MIXER_TUSB_CONFIG_H
#define MIXER_TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// MCU and OS selection
#define CFG_TUSB_MCU                OPT_MCU_NRF5X
#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUD_NRF_NRFX_VERSION    1   // Use legacy nrf_clock_*(EVENT) signatures (matches our stub)

// Speed: nRF52840 USBD is full-speed only
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Disable debug logging (no UART available; logging would deadlock)
#define CFG_TUSB_DEBUG              0

// Memory section / alignment for USB DMA buffers
#ifndef CFG_TUSB_MEM_SECTION
  #define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
  #define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

// Device stack: enable
#define CFG_TUD_ENABLED             1

// Endpoint 0 size
#define CFG_TUD_ENDPOINT0_SIZE      64

// Class drivers: enable only CDC
#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

// CDC FIFO sizes (bytes). Mixer printf bursts can be a few hundred bytes per round;
// 256/256 leaves headroom without consuming much RAM.
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      512

// CDC bulk endpoint packet size (full-speed = 64 max)
#define CFG_TUD_CDC_EP_BUFSIZE      64

#ifdef __cplusplus
}
#endif

#endif
