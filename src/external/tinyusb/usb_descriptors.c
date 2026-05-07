// USB descriptors for Mixer CDC ACM console on nRF52840 USB Dongle.
//
// Single configuration, single CDC ACM interface (one virtual serial port).
// VID = Nordic Semiconductor (0x1915); PID is application-defined and chosen
// to not collide with the open DFU bootloader (0x521F).

#include "tusb.h"
#include "nrf.h"
#include <string.h>

#define USB_VID   0x1915
#define USB_PID   0x000A
#define USB_BCD   0x0200  // USB 2.0

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = USB_BCD,

  // Use IAD (Interface Association Descriptor) class codes, required for
  // composite-style CDC ACM that some hosts (Windows) need to enumerate cleanly.
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01,
};

uint8_t const* tud_descriptor_device_cb(void) {
  return (uint8_t const*) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

// Endpoint addresses: bit 7 = direction (0=OUT, 1=IN). Notification EP is interrupt IN.
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

uint8_t const desc_fs_configuration[] = {
  // Configuration: 1 config, total length, attributes (bus-powered), max current 100mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),

  // CDC ACM: itf number, string idx, EP notification address & size, EP data address & size
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
// Index 0: language id (English-US)
// Index 1: manufacturer
// Index 2: product
// Index 3: serial number (will be filled at runtime from FICR.DEVICEID)
// Index 4: CDC interface name
char const* string_desc_arr[] = {
  (const char[]){0x09, 0x04},   // 0: supported language = 0x0409 (English-US)
  "NES Lab / Mixer",            // 1: Manufacturer
  "Mixer Console (PCA10059)",   // 2: Product
  NULL,                          // 3: Serial -- filled from FICR
  "Mixer CDC",                  // 4: CDC interface
};

static uint16_t _desc_str[32];

// FICR-derived serial buffer: 16 hex chars from DEVICEID[0..1]
static char _serial_str[17];

static const char* get_serial_string(void) {
  if (_serial_str[0] != 0) return _serial_str;

  // nRF52840 FICR.DEVICEID is two 32-bit registers giving a unique ID per chip.
  uint32_t id_lo = NRF_FICR->DEVICEID[0];
  uint32_t id_hi = NRF_FICR->DEVICEID[1];
  static const char hex[] = "0123456789ABCDEF";
  for (int i = 0; i < 8; i++) {
    _serial_str[i]     = hex[(id_hi >> ((7 - i) * 4)) & 0xF];
    _serial_str[i + 8] = hex[(id_lo >> ((7 - i) * 4)) & 0xF];
  }
  _serial_str[16] = 0;
  return _serial_str;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;

  uint8_t chr_count = 0;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

    const char* str = string_desc_arr[index];
    if (index == 3) str = get_serial_string();

    chr_count = (uint8_t) strlen(str);
    if (chr_count > 31) chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
  }

  // First word: length (in bytes) and descriptor type
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
