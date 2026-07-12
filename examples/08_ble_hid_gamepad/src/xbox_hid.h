#ifndef EX08_XBOX_HID_H
#define EX08_XBOX_HID_H

#include "chislink/ble.h"

#include <stdint.h>

#define XBOX_HID_REPORT_BYTES 16u
#define XBOX_HID_RUMBLE_REPORT_BYTES 8u
#define XBOX_HID_RUMBLE_REPORT_ID 0x03u

int xbox_hid_setup(cl_ble_t *ble,
                   cl_ble_gatts_id_t *input_report_chr,
                   cl_ble_gatts_id_t *rumble_report_chr);
void xbox_hid_build_report(uint16_t keys,
                           uint8_t report[XBOX_HID_REPORT_BYTES]);

#endif
