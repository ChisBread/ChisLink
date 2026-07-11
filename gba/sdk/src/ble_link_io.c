#include "chislink/ble_link_io.h"

#include "chislink/proto.h"

#include <string.h>

int cl_ble_link_io_open(cl_ble_t *ble, uint8_t role, const char *name) {
    if (!ble || !name || !name[0]) return -1;
    if (role > CL_BLE_LINK_IO_JOIN) return -1;

    int ret = cl_ble_link_start(ble, role, name);
    if (ret < 0) return ret;

    /* HOST returns 0 (advertising started), JOIN returns handle (>0) */
    return ret;
}

int cl_ble_link_io_send(cl_ble_t *ble,
                        int handle,
                        const void *data,
                        uint32_t length) {
    if (!ble || handle <= 0 || (!data && length)) return -1;

    int ret = cl_ble_link_send(ble, (cl_ble_handle_t)(uint16_t)handle,
                               data, length);
    return ret;
}

int cl_ble_link_io_recv(cl_ble_t *ble,
                        int handle,
                        void *buf,
                        uint32_t capacity) {
    if (!ble || handle <= 0 || !buf || !capacity) return -1;

    cl_ble_event_t ev;
    int ret = cl_ble_event_next(ble, &ev, sizeof(ev.data));
    if (ret < 0) return ret;
    if (ret == 0) return 0;                   /* no event */
    if (ret != CLP_BLE_EVENT_GATTS_WRITE) {
        if (ret != CLP_BLE_EVENT_GATT_DATA ||
            !ble->link_notify_attr ||
            ev.attr_handle != ble->link_notify_attr) {
            return 0;
        }
    } else {
        if (ev.data_length < CLP_BLE_GATTS_WRITE_EVENT_HEADER_BYTES) return 0;
        cl_ble_handle_t peer = (cl_ble_handle_t)((uint16_t)ev.data[0] |
                                                 ((uint16_t)ev.data[1] << 8u));
        if (peer != (cl_ble_handle_t)(uint16_t)handle) return 0;
        uint16_t value_length = (uint16_t)ev.data[2] |
                                ((uint16_t)ev.data[3] << 8u);
        uint8_t preview_length =
            (uint8_t)(ev.data_length - CLP_BLE_GATTS_WRITE_EVENT_HEADER_BYTES);
        uint32_t take = value_length;
        if (take > capacity) take = capacity;
        if (take > preview_length) {
            return cl_ble_gatts_get_value(ble, ev.attr_handle, 0u, buf, take);
        }
        if (take) {
            memmove(ev.data,
                    ev.data + CLP_BLE_GATTS_WRITE_EVENT_HEADER_BYTES,
                    take);
        }
        ev.data_length = (uint8_t)take;
    }

    uint32_t take = ev.data_length;
    if (take > capacity) take = capacity;
    for (uint32_t i = 0; i < take; ++i)
        ((uint8_t *)buf)[i] = ev.data[i];
    return (int)take;
}

int cl_ble_link_io_close(cl_ble_t *ble, int handle) {
    if (!ble) return -1;

    int ret = cl_ble_link_stop(ble, (cl_ble_handle_t)(uint16_t)handle);
    return ret < 0 ? ret : 0;
}
