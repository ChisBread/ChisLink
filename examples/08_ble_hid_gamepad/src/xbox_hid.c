#include "xbox_hid.h"

#include "chislink/gba/hw.h"

#include <string.h>

#define HID_UUID_SERVICE        0x1812u
#define HID_UUID_INFORMATION    0x2a4au
#define HID_UUID_REPORT_MAP     0x2a4bu
#define HID_UUID_CONTROL_POINT  0x2a4cu
#define HID_UUID_REPORT         0x2a4du
#define HID_UUID_PROTOCOL_MODE  0x2a4eu
#define HID_UUID_EXT_REPORT_REF 0x2907u
#define HID_UUID_REPORT_REF     0x2908u

#define DIS_UUID_SERVICE        0x180au
#define DIS_UUID_MANUFACTURER   0x2a29u
#define DIS_UUID_PNP_ID         0x2a50u

#define BAS_UUID_SERVICE        0x180fu
#define BAS_UUID_BATTERY_LEVEL  0x2a19u

#define HID_APPEARANCE_GAMEPAD  0x03c4u
#define HID_REPORT_ID_GAMEPAD   0x01u
#define HID_REPORT_TYPE_INPUT   0x01u
#define HID_REPORT_TYPE_OUTPUT  0x02u
#define HID_HAT_NEUTRAL         0u
#define HID_AXIS_CENTER         0x8000u
#define HID_TRIGGER_MIN         0x0000u

/*
 * Xbox One S 1708-like HID layout, trimmed to input report 1 and output
 * report 3. Descriptor structure follows the public Xbox controller
 * descriptor notes used by ESP32-BLE-BrailleHID (MIT, lemmingDev).
 */
static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, HID_REPORT_ID_GAMEPAD,

    0x09, 0x01, 0xa1, 0x00, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x95,
    0x02, 0x75, 0x10, 0x81, 0x02, 0xc0,

    0x09, 0x01, 0xa1, 0x00, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x95,
    0x02, 0x75, 0x10, 0x81, 0x02, 0xc0,

    0x05, 0x02, 0x09, 0xc5, 0x15, 0x00, 0x26, 0xff,
    0x03, 0x95, 0x01, 0x75, 0x0a, 0x81, 0x02, 0x15,
    0x00, 0x25, 0x00, 0x75, 0x06, 0x95, 0x01, 0x81,
    0x03,

    0x05, 0x02, 0x09, 0xc4, 0x15, 0x00, 0x26, 0xff,
    0x03, 0x95, 0x01, 0x75, 0x0a, 0x81, 0x02, 0x15,
    0x00, 0x25, 0x00, 0x75, 0x06, 0x95, 0x01, 0x81,
    0x03,

    0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08,
    0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14, 0x75,
    0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00, 0x75,
    0x04, 0x95, 0x01, 0x15, 0x00, 0x25, 0x00, 0x81,
    0x03,

    0x05, 0x09, 0x19, 0x01, 0x29, 0x0f, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x0f, 0x81, 0x02,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x03,

    0x05, 0x0c, 0x0a, 0xb2, 0x00, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x15,
    0x00, 0x25, 0x00, 0x75, 0x07, 0x95, 0x01, 0x81,
    0x03,

    0x05, 0x0f, 0x09, 0x21, 0x85, XBOX_HID_RUMBLE_REPORT_ID,
    0xa1, 0x02, 0x09, 0x97, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x04, 0x95, 0x01, 0x91, 0x02, 0x15, 0x00,
    0x25, 0x00, 0x75, 0x04, 0x95, 0x01, 0x91, 0x03,
    0x09, 0x70, 0x15, 0x00, 0x25, 0x64, 0x75, 0x08,
    0x95, 0x04, 0x91, 0x02, 0x09, 0x50, 0x66, 0x01,
    0x10, 0x55, 0x0e, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0x09, 0xa7,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
    0x01, 0x91, 0x02, 0x65, 0x00, 0x55, 0x00, 0x09,
    0x7c, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
    0x95, 0x01, 0x91, 0x02, 0xc0, 0xc0,
};

static const uint8_t HID_INFORMATION[] = {
    0x11, 0x01, 0x00, 0x03,
};
static const uint8_t HID_EXTERNAL_REPORT_REF_BATTERY[] = {
    0x19, 0x2a,
};
static const uint8_t HID_INPUT_REPORT_REF[] = {
    HID_REPORT_ID_GAMEPAD, HID_REPORT_TYPE_INPUT,
};
static const uint8_t HID_RUMBLE_REPORT_REF[] = {
    XBOX_HID_RUMBLE_REPORT_ID, HID_REPORT_TYPE_OUTPUT,
};
static const uint8_t HID_PROTOCOL_MODE_REPORT[] = { 1u };
static const uint8_t HID_EMPTY_REPORT[XBOX_HID_REPORT_BYTES] = {
    0x00u, 0x80u, 0x00u, 0x80u, 0x00u, 0x80u, 0x00u, 0x80u,
    0x00u, 0x00u, 0x00u, 0x00u, HID_HAT_NEUTRAL, 0x00u, 0x00u, 0x00u,
};
static const uint8_t HID_EMPTY_RUMBLE_REPORT[XBOX_HID_RUMBLE_REPORT_BYTES] = { 0u };
static const uint16_t HID_ADV_UUIDS[] = { HID_UUID_SERVICE };
static const char DIS_MANUFACTURER[] = "Microsoft";
static const uint8_t DIS_PNP_ID[] = {
    0x02, 0x5e, 0x04, 0xfd, 0x02, 0x08, 0x04,
};
static const uint8_t BAS_LEVEL[] = { 100u };

static void store_le16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8u);
}

static uint8_t hat_from_keys(uint16_t keys) {
    uint8_t up = (keys & CL_GBA_KEY_UP) ? 1u : 0u;
    uint8_t down = (keys & CL_GBA_KEY_DOWN) ? 1u : 0u;
    uint8_t left = (keys & CL_GBA_KEY_LEFT) ? 1u : 0u;
    uint8_t right = (keys & CL_GBA_KEY_RIGHT) ? 1u : 0u;
    if (up && right) return 2u;
    if (down && right) return 4u;
    if (down && left) return 6u;
    if (up && left) return 8u;
    if (up) return 1u;
    if (right) return 3u;
    if (down) return 5u;
    if (left) return 7u;
    return HID_HAT_NEUTRAL;
}

void xbox_hid_build_report(uint16_t keys,
                           uint8_t report[XBOX_HID_REPORT_BYTES]) {
    memset(report, 0, XBOX_HID_REPORT_BYTES);
    store_le16(report + 0u, HID_AXIS_CENTER);
    store_le16(report + 2u, HID_AXIS_CENTER);
    store_le16(report + 4u, HID_AXIS_CENTER);
    store_le16(report + 6u, HID_AXIS_CENTER);
    store_le16(report + 8u, HID_TRIGGER_MIN);
    store_le16(report + 10u, HID_TRIGGER_MIN);
    report[12] = hat_from_keys(keys);

    uint16_t buttons = 0;
    if (keys & CL_GBA_KEY_A) buttons |= 0x0001u;
    if (keys & CL_GBA_KEY_B) buttons |= 0x0002u;
    if (keys & CL_GBA_KEY_L) buttons |= 0x0040u;
    if (keys & CL_GBA_KEY_R) buttons |= 0x0080u;
    if (keys & CL_GBA_KEY_SELECT) buttons |= 0x0400u;
    if (keys & CL_GBA_KEY_START) buttons |= 0x0800u;
    store_le16(report + 13u, buttons);
}

int xbox_hid_setup(cl_ble_t *ble,
                   cl_ble_gatts_id_t *input_report_chr,
                   cl_ble_gatts_id_t *rumble_report_chr) {
    if (!ble || !input_report_chr || !rumble_report_chr) {
        return -1;
    }

    cl_ble_gatts_id_t bas_svc;
    cl_ble_gatts_id_t dis_svc;
    cl_ble_gatts_id_t hid_svc;
    cl_ble_gatts_id_t chr;
    cl_ble_gatts_id_t report_map_chr;
    const cl_ble_security_config_t security = {
        .io_caps = CL_BLE_IO_CAPS_DISPLAY_YESNO,
        .flags = CL_BLE_SECURITY_BOND |
                 CL_BLE_SECURITY_MITM |
                 CL_BLE_SECURITY_SC,
        .our_key_dist = CL_BLE_SECURITY_KEY_DIST_ENC,
        .their_key_dist = CL_BLE_SECURITY_KEY_DIST_ENC,
        .security_level = 2u,
    };
    int ret = cl_ble_security_config(ble, &security);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_reset(ble);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_add_service16(ble, BAS_UUID_SERVICE,
                                     CLP_BLE_GATTS_PRIMARY, &bas_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, bas_svc, BAS_UUID_BATTERY_LEVEL,
                                 CLP_BLE_GATTS_CHR_READ,
                                 BAS_LEVEL, sizeof(BAS_LEVEL), &chr);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_add_service16(ble, DIS_UUID_SERVICE,
                                     CLP_BLE_GATTS_PRIMARY, &dis_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, dis_svc, DIS_UUID_MANUFACTURER,
                                 CLP_BLE_GATTS_CHR_READ,
                                 DIS_MANUFACTURER, sizeof(DIS_MANUFACTURER) - 1u,
                                 &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, dis_svc, DIS_UUID_PNP_ID,
                                 CLP_BLE_GATTS_CHR_READ,
                                 DIS_PNP_ID, sizeof(DIS_PNP_ID), &chr);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_add_service16(ble, HID_UUID_SERVICE,
                                     CLP_BLE_GATTS_PRIMARY, &hid_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_include(ble, hid_svc, bas_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, hid_svc, HID_UUID_INFORMATION,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_READ_ENC,
                                 HID_INFORMATION, sizeof(HID_INFORMATION),
                                 &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, hid_svc, HID_UUID_CONTROL_POINT,
                                 CLP_BLE_GATTS_CHR_WRITE_NO_RSP |
                                 CLP_BLE_GATTS_WRITE_ENC,
                                 NULL, 0u, &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, hid_svc, HID_UUID_REPORT_MAP,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_READ_ENC,
                                 HID_REPORT_MAP, sizeof(HID_REPORT_MAP),
                                 &report_map_chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_desc16(ble, report_map_chr, HID_UUID_EXT_REPORT_REF,
                                  CLP_BLE_GATTS_ATTR_READ,
                                  HID_EXTERNAL_REPORT_REF_BATTERY,
                                  sizeof(HID_EXTERNAL_REPORT_REF_BATTERY),
                                  &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, hid_svc, HID_UUID_PROTOCOL_MODE,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_CHR_WRITE_NO_RSP |
                                 CLP_BLE_GATTS_READ_ENC |
                                 CLP_BLE_GATTS_WRITE_ENC,
                                 HID_PROTOCOL_MODE_REPORT,
                                 sizeof(HID_PROTOCOL_MODE_REPORT), &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, hid_svc, HID_UUID_REPORT,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_CHR_NOTIFY |
                                 CLP_BLE_GATTS_READ_ENC |
                                 CLP_BLE_GATTS_NOTIFY_ENC,
                                 HID_EMPTY_REPORT, sizeof(HID_EMPTY_REPORT),
                                 input_report_chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_desc16(ble, *input_report_chr,
                                  HID_UUID_REPORT_REF,
                                  CLP_BLE_GATTS_ATTR_READ,
                                  HID_INPUT_REPORT_REF,
                                  sizeof(HID_INPUT_REPORT_REF), &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(ble, hid_svc, HID_UUID_REPORT,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_CHR_WRITE |
                                 CLP_BLE_GATTS_CHR_WRITE_NO_RSP |
                                 CLP_BLE_GATTS_READ_ENC |
                                 CLP_BLE_GATTS_WRITE_ENC,
                                 HID_EMPTY_RUMBLE_REPORT,
                                 sizeof(HID_EMPTY_RUMBLE_REPORT),
                                 rumble_report_chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_desc16(ble, *rumble_report_chr,
                                  HID_UUID_REPORT_REF,
                                  CLP_BLE_GATTS_ATTR_READ,
                                  HID_RUMBLE_REPORT_REF,
                                  sizeof(HID_RUMBLE_REPORT_REF), &chr);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_start(ble);
    if (ret < 0) return ret;

    cl_ble_adv_config_t adv;
    memset(&adv, 0, sizeof(adv));
    adv.flags = CLP_BLE_ADV_FLAG_APPEARANCE;
    adv.appearance = HID_APPEARANCE_GAMEPAD;
    adv.uuid16 = HID_ADV_UUIDS;
    adv.uuid16_count = 1u;
    adv.name = "ChisLink Pad";
    return cl_ble_adv_start(ble, &adv);
}
