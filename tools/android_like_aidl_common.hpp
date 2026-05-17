#ifndef WEBOS_DIRTY_BINDER_ANDROID_LIKE_AIDL_COMMON_HPP
#define WEBOS_DIRTY_BINDER_ANDROID_LIKE_AIDL_COMMON_HPP

#include "android_like_callback_common.hpp"

#include <string>

#define AIDL_LIKE_DESCRIPTOR "webos.dirtybinder.IAidlLikeDemo"
#define AIDL_LIKE_TX_ECHO 1U
#define AIDL_LIKE_TX_ADD 2U
#define AIDL_LIKE_PING 0x50494e47U

/*
 * Android IBinder meta transaction:
 * INTERFACE_TRANSACTION = '_NTF' = 0x5f4e5446
 */
#define AIDL_LIKE_INTERFACE_TRANSACTION 0x5f4e5446U

struct aidl_like_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
};

static inline int aidl_like_read_i32(struct aidl_like_reader *r, int32_t *out) {
    if (!r || !out)
        return -1;

    if (r->pos + sizeof(int32_t) > r->size)
        return -1;

    memcpy(out, r->data + r->pos, sizeof(int32_t));
    r->pos += sizeof(int32_t);
    return 0;
}

static inline int aidl_like_read_string16_ascii(struct aidl_like_reader *r, std::string *out) {
    int32_t len;
    size_t bytes;
    size_t padded;
    size_t i;

    if (!r || !out)
        return -1;

    out->clear();

    if (aidl_like_read_i32(r, &len) != 0)
        return -1;

    if (len < 0)
        return 0;

    bytes = ((size_t)len + 1U) * 2U;
    padded = cb_align4(bytes);

    if (r->pos + padded > r->size)
        return -1;

    for (i = 0; i < (size_t)len; i++) {
        uint8_t lo = r->data[r->pos + i * 2U];
        uint8_t hi = r->data[r->pos + i * 2U + 1U];

        if (hi != 0)
            out->push_back('?');
        else
            out->push_back((char)lo);
    }

    r->pos += padded;
    return 0;
}

static inline int aidl_like_write_no_exception(uint8_t *buf, size_t cap, size_t *pos) {
    return cb_parcel_write_i32(buf, cap, pos, 0);
}

static inline int aidl_like_write_exception(uint8_t *buf, size_t cap, size_t *pos, int32_t ex) {
    return cb_parcel_write_i32(buf, cap, pos, ex);
}

static inline int aidl_like_write_interface_token(uint8_t *buf, size_t cap, size_t *pos) {
    if (cb_parcel_write_i32(buf, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(buf, cap, pos, AIDL_LIKE_DESCRIPTOR);
}

static inline int aidl_like_read_interface_token(struct aidl_like_reader *r) {
    int32_t strict_header = 0;
    std::string descriptor;

    if (aidl_like_read_i32(r, &strict_header) != 0)
        return -1;

    if (aidl_like_read_string16_ascii(r, &descriptor) != 0)
        return -1;

    if (descriptor != AIDL_LIKE_DESCRIPTOR) {
        fprintf(stderr,
                "aidl-like bad descriptor got='%s' expected='%s'\n",
                descriptor.c_str(),
                AIDL_LIKE_DESCRIPTOR);
        return -1;
    }

    return 0;
}

static inline int aidl_like_send_reply_parcel(
    int fd,
    binder_uintptr_t incoming_buffer,
    const uint8_t *reply_data,
    size_t reply_size,
    const char *tag)
{
    uint8_t writebuf[2048];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data reply_tr;

    cmd = BC_FREE_BUFFER;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &incoming_buffer, sizeof(incoming_buffer));

    cmd = BC_REPLY;
    cb_append_u32(&p, cmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = reply_size;
    reply_tr.offsets_size = 0;
    reply_tr.data.ptr.buffer = (binder_uintptr_t)reply_data;
    reply_tr.data.ptr.offsets = 0;

    cb_append_bytes(&p, &reply_tr, sizeof(reply_tr));

    return cb_binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, tag) < 0 ? -1 : 0;
}

static inline int aidl_like_send_exception_reply(
    int fd,
    binder_uintptr_t incoming_buffer,
    int32_t exception_code,
    const char *tag)
{
    uint8_t parcel[128];
    size_t pos = 0;

    if (aidl_like_write_exception(parcel, sizeof(parcel), &pos, exception_code) != 0)
        return -1;

    return aidl_like_send_reply_parcel(fd, incoming_buffer, parcel, pos, tag);
}

#endif
