#include "android_like_aidl_common.hpp"

#include <string>

#define FACTORY_DESCRIPTOR "webos.dirtybinder.IBinderFactory"
#define CHILD_DESCRIPTOR "webos.dirtybinder.IReturnedChild"

#define FACTORY_TX_GET_CHILD 0x47434844U /* GCHD */
#define CHILD_TX_ECHO 0x43484543U       /* CHEC */
#define FACTORY_PING 0x50494e47U

static const binder_uintptr_t kFactoryServicePtr =
    (binder_uintptr_t)0x4641435453525630ULL; /* FACTSRV0 */

static const binder_uintptr_t kFactoryServiceCookie =
    (binder_uintptr_t)0x46414354434b3030ULL; /* FACTCK00 */

static const binder_uintptr_t kReturnedChildPtr =
    (binder_uintptr_t)0x5245544348494c44ULL; /* RETCHILD */

static const binder_uintptr_t kReturnedChildCookie =
    (binder_uintptr_t)0x52455443484b3030ULL; /* RETCHK00 */

static int write_token(uint8_t *buf, size_t cap, size_t *pos, const char *descriptor) {
    if (cb_parcel_write_i32(buf, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(buf, cap, pos, descriptor);
}

static int read_token(struct aidl_like_reader *r, const char *expected) {
    int32_t strict_header = 0;
    std::string descriptor;

    if (aidl_like_read_i32(r, &strict_header) != 0)
        return -1;

    if (aidl_like_read_string16_ascii(r, &descriptor) != 0)
        return -1;

    if (descriptor != expected) {
        fprintf(stderr,
                "binder-return service: bad descriptor got=%s expected=%s\n",
                descriptor.c_str(),
                expected);
        return -1;
    }

    return 0;
}

static int send_string_reply(
    int fd,
    binder_uintptr_t incoming_buffer,
    int32_t exception_code,
    const char *text,
    const char *tag)
{
    uint8_t reply[1024];
    size_t pos = 0;

    if (cb_parcel_write_i32(reply, sizeof(reply), &pos, exception_code) != 0)
        return -1;

    if (exception_code == 0) {
        if (cb_parcel_write_string16_ascii(reply, sizeof(reply), &pos, text ? text : "") != 0)
            return -1;
    }

    return aidl_like_send_reply_parcel(fd, incoming_buffer, reply, pos, tag);
}

static int send_child_object_reply(int fd, binder_uintptr_t incoming_buffer) {
    uint8_t reply[512];
    size_t pos = 0;
    binder_size_t offsets[1];
    struct flat_binder_object obj;
    uint8_t writebuf[2048];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data reply_tr;

    memset(reply, 0, sizeof(reply));

    if (cb_parcel_write_i32(reply, sizeof(reply), &pos, 0) != 0)
        return -1;

    pos = cb_align8(pos);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = kReturnedChildPtr;
    obj.cookie = kReturnedChildCookie;

    offsets[0] = (binder_size_t)pos;
    memcpy(reply + pos, &obj, sizeof(obj));
    pos += sizeof(obj);
    pos = cb_align8(pos);

    cmd = BC_FREE_BUFFER;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &incoming_buffer, sizeof(incoming_buffer));

    cmd = BC_REPLY;
    cb_append_u32(&p, cmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = pos;
    reply_tr.offsets_size = sizeof(offsets);
    reply_tr.data.ptr.buffer = (binder_uintptr_t)reply;
    reply_tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cb_append_bytes(&p, &reply_tr, sizeof(reply_tr));

    printf("binder-return service: sending child object ptr=0x%" PRIx64 " cookie=0x%" PRIx64 " offset=%llu\n",
           (uint64_t)kReturnedChildPtr,
           (uint64_t)kReturnedChildCookie,
           (unsigned long long)offsets[0]);
    printf("AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT\n");
    fflush(stdout);

    return cb_binder_write_read(fd,
                                writebuf,
                                (size_t)(p - writebuf),
                                NULL,
                                0,
                                "binder-return service child object reply") < 0 ? -1 : 0;
}

static int process_factory_transaction(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;

    if (tr->code == FACTORY_PING) {
        return cb_send_text_reply(fd,
                                  tr->data.ptr.buffer,
                                  0,
                                  "pong",
                                  "binder-return factory ping reply");
    }

    if (tr->code != FACTORY_TX_GET_CHILD) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "binder-return factory unknown reply");
    }

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, FACTORY_DESCRIPTOR) != 0) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "binder-return factory bad-token reply");
    }

    return send_child_object_reply(fd, tr->data.ptr.buffer);
}

static int process_child_transaction(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    std::string msg;
    std::string out;

    printf("binder-return child transaction code=0x%x target_ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           tr->code,
           (uint64_t)tr->target.ptr,
           (uint64_t)tr->cookie);

    if (tr->code == FACTORY_PING) {
        return cb_send_text_reply(fd,
                                  tr->data.ptr.buffer,
                                  0,
                                  "pong",
                                  "binder-return child ping reply");
    }

    if (tr->code != CHILD_TX_ECHO) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "binder-return child unknown reply");
    }

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, CHILD_DESCRIPTOR) != 0 ||
        aidl_like_read_string16_ascii(&r, &msg) != 0) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "binder-return child bad-parcel reply");
    }

    out = "child-echo:" + msg;

    printf("binder-return child echo msg=%s out=%s\n", msg.c_str(), out.c_str());
    printf("AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK\n");
    fflush(stdout);

    return send_string_reply(fd,
                             tr->data.ptr.buffer,
                             0,
                             out.c_str(),
                             "binder-return child echo reply");
}

static int process_transaction(int fd, struct binder_transaction_data *tr) {
    printf("binder-return service BR_TRANSACTION code=0x%x target_ptr=0x%" PRIx64 " cookie=0x%" PRIx64 " data_size=%llu offsets_size=%llu flags=0x%x\n",
           tr->code,
           (uint64_t)tr->target.ptr,
           (uint64_t)tr->cookie,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size,
           tr->flags);

    if (tr->target.ptr == kReturnedChildPtr ||
        tr->cookie == kReturnedChildCookie ||
        tr->code == CHILD_TX_ECHO) {
        return process_child_transaction(fd, tr);
    }

    return process_factory_transaction(fd, tr);
}

static int service_loop(int fd) {
    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;
    int first = 1;

    cb_append_u32(&p, cmd);

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "binder-return service enter looper" : "binder-return service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("binder-return service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (process_transaction(fd, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "binder-return service") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "binder-return service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "binder-return service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.factory";
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("binder-return service");

    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd,
                                  service_name,
                                  kFactoryServicePtr,
                                  kFactoryServiceCookie) != 0)
        return 1;

    printf("AIDL_LIKE_BINDER_RETURN_SERVICE_REGISTERED\n");
    fflush(stdout);

    return service_loop(fd) == 0 ? 0 : 1;
}
