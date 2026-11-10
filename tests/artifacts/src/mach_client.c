/*
 * mach_client.c - Real Compiled ARM64 Mach IPC Trap Client for WinIOS
 * Campaign 191-U: STAGE U3
 *
 * Uses official macOS/Darwin <mach/mach.h>, <mach/message.h>, <mach/mach_traps.h>
 * structures & constants, dynamically linked against /usr/lib/libSystem.B.dylib
 * via guest minidyld in EL0, and exercises real XNU Mach trap numbers:
 *   -26 = mach_reply_port
 *   -27 = thread_self_trap
 *   -28 = task_self_trap
 *   -31 = mach_msg_trap
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_traps.h>
#include <mach/message.h>

extern mach_port_t mach_reply_port(void);
extern mach_port_t thread_self_trap(void);

/* Cross-verify ABI layout against public XNU <mach/message.h> */
_Static_assert(sizeof(mach_msg_header_t) == 24, "mach_msg_header_t must be 24 bytes");
_Static_assert(offsetof(mach_msg_header_t, msgh_bits) == 0, "msgh_bits offset");
_Static_assert(offsetof(mach_msg_header_t, msgh_size) == 4, "msgh_size offset");
_Static_assert(offsetof(mach_msg_header_t, msgh_remote_port) == 8, "msgh_remote_port offset");
_Static_assert(offsetof(mach_msg_header_t, msgh_local_port) == 12, "msgh_local_port offset");
_Static_assert(offsetof(mach_msg_header_t, msgh_voucher_port) == 16, "msgh_voucher_port offset");
_Static_assert(offsetof(mach_msg_header_t, msgh_id) == 20, "msgh_id offset");

_Static_assert(MACH_SEND_MSG == 0x00000001u, "MACH_SEND_MSG constant");
_Static_assert(MACH_RCV_MSG == 0x00000002u, "MACH_RCV_MSG constant");
_Static_assert(MACH_MSG_SUCCESS == 0, "MACH_MSG_SUCCESS constant");
_Static_assert(MACH_SEND_INVALID_DEST == 0x10000003, "MACH_SEND_INVALID_DEST constant");
_Static_assert(MACH_RCV_TIMED_OUT == 0x10004003, "MACH_RCV_TIMED_OUT constant");

struct __attribute__((aligned(16))) MachPingMessage {
    mach_msg_header_t hdr;
    char              body[32];
    uint32_t          seq;
    uint32_t          cookie;
};

_Static_assert(sizeof(struct MachPingMessage) == 64, "MachPingMessage size must be 64 bytes");

int main(void) {
    mach_port_t task_cached = mach_task_self();
    mach_port_t task_raw    = task_self_trap();
    mach_port_t thread_p    = thread_self_trap();
    mach_port_t service_p   = mach_reply_port();
    mach_port_t reply_p     = mach_reply_port();

    printf("MACH_PORTS task=%u,%u thread=%u svc=%u reply=%u\n",
           task_cached, task_raw, thread_p, service_p, reply_p);

    if (task_cached == MACH_PORT_NULL || task_cached != task_raw ||
        thread_p == MACH_PORT_NULL || service_p == MACH_PORT_NULL ||
        reply_p == MACH_PORT_NULL || service_p == reply_p) {
        puts("PORT_ALLOC_FAIL");
        return 1;
    }

    /* 1. Direct SVC #0x80 with X16 = -28 checks identically to libSystem wrapper */
    mach_port_t inline_task = MACH_PORT_NULL;
    {
        register uint64_t x0 __asm__("x0") = 0;
        register int64_t  x16 __asm__("x16") = -28;
        __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
        inline_task = (mach_port_t)x0;
    }
    if (inline_task != task_raw) {
        puts("INLINE_TRAP_MISMATCH");
        return 2;
    }

    /* 2. Send Mach Ping request with reply port right */
    struct MachPingMessage req;
    memset(&req, 0, sizeof(req));
    req.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port = service_p;
    req.hdr.msgh_local_port  = reply_p;
    req.hdr.msgh_voucher_port = MACH_PORT_NULL;
    req.hdr.msgh_id          = 0x4D414348; /* "MACH" */
    strcpy(req.body, "PING_FROM_ARM64_GUEST");
    req.seq    = 42u;
    req.cookie = 0xCAFEBABEu;

    mach_msg_return_t kr = mach_msg(
        &req.hdr, MACH_SEND_MSG, sizeof(req), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS) {
        printf("SEND_FAIL 0x%x\n", (unsigned)kr);
        return 3;
    }

    /* 3. Receive request on service_p and verify translated reply right */
    struct MachPingMessage rcv;
    memset(&rcv, 0, sizeof(rcv));
    kr = mach_msg(
        &rcv.hdr, MACH_RCV_MSG, 0, sizeof(rcv), service_p, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS) {
        printf("RCV_FAIL 0x%x\n", (unsigned)kr);
        return 4;
    }

    if (rcv.hdr.msgh_id != 0x4D414348 ||
        rcv.hdr.msgh_size != sizeof(req) ||
        rcv.hdr.msgh_remote_port == MACH_PORT_NULL ||
        rcv.seq != 42u ||
        rcv.cookie != 0xCAFEBABEu ||
        strcmp(rcv.body, "PING_FROM_ARM64_GUEST") != 0) {
        puts("PAYLOAD_MISMATCH");
        return 5;
    }

    /* 4. Reply back via translated msgh_remote_port and receive on reply_p */
    struct MachPingMessage rep;
    memset(&rep, 0, sizeof(rep));
    rep.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    rep.hdr.msgh_size        = sizeof(rep);
    rep.hdr.msgh_remote_port = rcv.hdr.msgh_remote_port;
    rep.hdr.msgh_local_port  = MACH_PORT_NULL;
    rep.hdr.msgh_id          = 0x5245504C; /* "REPL" */
    strcpy(rep.body, "PONG_TO_REPLY_PORT");
    rep.seq    = rcv.seq + 1u;
    rep.cookie = 0x12345678u;

    kr = mach_msg(
        &rep.hdr, MACH_SEND_MSG, sizeof(rep), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS) {
        printf("REPLY_SEND_FAIL 0x%x\n", (unsigned)kr);
        return 6;
    }

    struct MachPingMessage rep_rcv;
    memset(&rep_rcv, 0, sizeof(rep_rcv));
    kr = mach_msg(
        &rep_rcv.hdr, MACH_RCV_MSG, 0, sizeof(rep_rcv), reply_p, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS ||
        rep_rcv.hdr.msgh_id != 0x5245504C ||
        rep_rcv.seq != 43u ||
        strcmp(rep_rcv.body, "PONG_TO_REPLY_PORT") != 0) {
        printf("REPLY_RCV_FAIL 0x%x\n", (unsigned)kr);
        return 7;
    }

    printf("ROUNDTRIP_OK seq=%u body=%s\n", rep_rcv.seq, rep_rcv.body);

    /* 5. Verify genuine XNU error return codes without carry-flag corruption */
    struct MachPingMessage bad;
    memset(&bad, 0, sizeof(bad));
    bad.hdr.msgh_size = sizeof(bad.hdr);
    bad.hdr.msgh_remote_port = 0xDEAD0001u;
    mach_msg_return_t err_dest = mach_msg(
        &bad.hdr, MACH_SEND_MSG, sizeof(bad.hdr), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    mach_msg_return_t err_empty = mach_msg(
        &bad.hdr, MACH_RCV_MSG, 0, sizeof(bad), reply_p, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    printf("ERR_CODES dest=0x%x empty=0x%x\n", (unsigned)err_dest, (unsigned)err_empty);
    if (err_dest != MACH_SEND_INVALID_DEST || err_empty != MACH_RCV_TIMED_OUT) {
        puts("ERR_CODE_MISMATCH");
        return 8;
    }

    puts("MACH_CLIENT_PASS");
    return 0;
}
