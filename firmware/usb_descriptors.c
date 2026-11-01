#include "tusb.h"
#include "pico/unique_id.h"

static const uint8_t d_kb[] = { TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)), TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2)) };
static const uint8_t d_raw[] = {
  HID_USAGE_PAGE_N(0xFF60, 2), HID_USAGE(0x61), HID_COLLECTION(HID_COLLECTION_APPLICATION),
  HID_USAGE(0x62), HID_LOGICAL_MIN(0), HID_LOGICAL_MAX_N(0xFF, 2), HID_REPORT_SIZE(8), HID_REPORT_COUNT(32),
  HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
  HID_USAGE(0x63), HID_LOGICAL_MIN(0), HID_LOGICAL_MAX_N(0xFF, 2), HID_REPORT_SIZE(8), HID_REPORT_COUNT(32),
  HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
  HID_COLLECTION_END };

static const tusb_desc_device_t dev = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0xC1A0, .idProduct = 0x0016, .bcdDevice = 0x0100,
  .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1 };

#define CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
static const uint8_t cfg[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, CFG_LEN, 0, 100),
  TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(d_kb), 0x81, 16, 1),
  TUD_HID_INOUT_DESCRIPTOR(1, 0, HID_ITF_PROTOCOL_NONE, sizeof(d_raw), 0x02, 0x82, 32, 1) };

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&dev; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t i) { (void)i; return cfg; }
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) { return itf == 0 ? d_kb : d_raw; }

uint16_t const *tud_descriptor_string_cb(uint8_t idx, uint16_t langid) {
  (void)langid; static uint16_t s[32]; const char *str; char ser[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
  uint8_t n;
  if (idx == 0) { s[1] = 0x0409; n = 1; s[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2); return s; }
  if (idx == 1) str = "TM"; else if (idx == 2) str = "Macropad15 RP2350";
  else if (idx == 3) { pico_get_unique_board_id_string(ser, sizeof ser); str = ser; } else return NULL;
  for (n = 0; str[n] && n < 31; n++) s[1 + n] = str[n];
  s[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
  return s;
}
