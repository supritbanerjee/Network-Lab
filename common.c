#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* ---------------- serialization (big-endian on the wire) ---------------- */

void header_serialize(const FrameHeader *h, uint8_t buf[HEADER_SIZE]) {
    buf[0]  = (h->frame_id >> 24) & 0xFF;
    buf[1]  = (h->frame_id >> 16) & 0xFF;
    buf[2]  = (h->frame_id >> 8)  & 0xFF;
    buf[3]  =  h->frame_id        & 0xFF;
    buf[4]  = h->scheme;
    buf[5]  = h->crc_type;
    buf[6]  = h->error_type;
    buf[7]  = h->reserved;
    buf[8]  = (h->payload_len >> 8) & 0xFF;
    buf[9]  =  h->payload_len       & 0xFF;
    buf[10] = (h->reserved2 >> 8) & 0xFF;
    buf[11] =  h->reserved2       & 0xFF;
    buf[12] = (h->redundancy >> 24) & 0xFF;
    buf[13] = (h->redundancy >> 16) & 0xFF;
    buf[14] = (h->redundancy >> 8)  & 0xFF;
    buf[15] =  h->redundancy        & 0xFF;
}

void header_deserialize(FrameHeader *h, const uint8_t buf[HEADER_SIZE]) {
    h->frame_id = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                  ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
    h->scheme     = buf[4];
    h->crc_type   = buf[5];
    h->error_type = buf[6];
    h->reserved   = buf[7];
    h->payload_len = ((uint16_t)buf[8] << 8) | buf[9];
    h->reserved2   = ((uint16_t)buf[10] << 8) | buf[11];
    h->redundancy  = ((uint32_t)buf[12] << 24) | ((uint32_t)buf[13] << 16) |
                      ((uint32_t)buf[14] << 8)  |  (uint32_t)buf[15];
}

/* ---------------- reliable TCP send/recv ---------------- */

int send_all(int sock, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, p + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

int recv_all(int sock, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(sock, p + got, len - got, 0);
        if (n == 0) return 0;   /* connection closed cleanly */
        if (n < 0) return -1;   /* error */
        got += (size_t)n;
    }
    return (int)got;
}

/* ---------------- checksum (16-bit, one's complement, IP-style) ---------------- */

uint16_t checksum16_compute(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 1 < len; i += 2) {
        uint16_t word = ((uint16_t)data[i] << 8) | data[i + 1];
        sum += word;
        if (sum & 0x10000) sum = (sum & 0xFFFF) + 1;
    }
    if (i < len) { /* odd trailing byte, pad with zero */
        uint16_t word = (uint16_t)data[i] << 8;
        sum += word;
        if (sum & 0x10000) sum = (sum & 0xFFFF) + 1;
    }
    return (uint16_t)(~sum & 0xFFFF);
}

/* ---------------- generic bit-by-bit CRC ---------------- */

uint32_t crc_poly_for_type(int crc_type) {
    switch (crc_type) {
        case 8:  return 0xD5;        /* x^8+x^7+x^6+x^4+x^2+1 (Bluetooth) */
        case 10: return 0x233;       /* x^10+x^9+x^5+x^4+x+1 (Telecom)    */
        case 16: return 0x8005;      /* x^16+x^15+x^2+1 (USB)             */
        case 32: return 0x04C11DB7UL;/* Ethernet IEEE 802.3               */
        default: return 0;
    }
}

uint32_t crc_compute(const uint8_t *data, size_t len, uint32_t poly, int width) {
    uint32_t crc = 0;
    uint32_t topbit = 1UL << (width - 1);
    uint32_t mask = (width == 32) ? 0xFFFFFFFFUL : ((1UL << width) - 1);

    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint32_t)data[i]) << (width - 8);
        for (int b = 0; b < 8; b++) {
            if (crc & topbit)
                crc = ((crc << 1) ^ poly) & mask;
            else
                crc = (crc << 1) & mask;
        }
    }
    return crc & mask;
}

/* ---------------- error injection ---------------- */

static void flip_bit(uint8_t *buf, size_t len, size_t bitpos) {
    size_t total_bits = len * 8;
    if (total_bits == 0) return;
    bitpos %= total_bits;
    size_t byte = bitpos / 8;
    int bit = bitpos % 8;
    buf[byte] ^= (uint8_t)(1u << bit);
}

void inject_single_bit_error(uint8_t *buf, size_t len) {
    if (len == 0) return;
    size_t total_bits = len * 8;
    size_t pos = (size_t)rand() % total_bits;
    flip_bit(buf, len, pos);
}

void inject_two_isolated_bit_errors(uint8_t *buf, size_t len) {
    if (len == 0) return;
    size_t total_bits = len * 8;
    if (total_bits < 4) { inject_single_bit_error(buf, len); return; }
    size_t p1 = (size_t)rand() % total_bits;
    size_t p2;
    long diff;
    do {
        p2 = (size_t)rand() % total_bits;
        diff = (long)p1 - (long)p2;
        if (diff < 0) diff = -diff;
    } while (diff <= 1); /* isolated => not adjacent, not the same bit */
    flip_bit(buf, len, p1);
    flip_bit(buf, len, p2);
}

void inject_odd_bit_errors(uint8_t *buf, size_t len, int count) {
    if (len == 0) return;
    if (count % 2 == 0) count++;      /* force odd */
    if (count < 3) count = 3;
    size_t total_bits = len * 8;
    if ((size_t)count > total_bits) count = (int)(total_bits | 1);

    uint8_t *used = (uint8_t *)calloc(total_bits, 1);
    int placed = 0;
    while (placed < count && used) {
        size_t pos = (size_t)rand() % total_bits;
        if (used[pos]) continue;
        used[pos] = 1;
        flip_bit(buf, len, pos);
        placed++;
    }
    free(used);
}

void inject_burst_error(uint8_t *buf, size_t len, int burst_len) {
    if (len == 0) return;
    size_t total_bits = len * 8;
    if ((size_t)burst_len > total_bits) burst_len = (int)total_bits;
    if (burst_len < 1) burst_len = 1;
    size_t start = (size_t)rand() % (total_bits - burst_len + 1);

    flip_bit(buf, len, start);                       /* first bit always in error */
    if (burst_len > 1) flip_bit(buf, len, start + burst_len - 1); /* last bit always in error */
    for (int i = 1; i < burst_len - 1; i++) {
        if (rand() % 2) flip_bit(buf, len, start + i); /* bits in between: random */
    }
}

void apply_error(uint8_t *buf, size_t len, int error_type) {
    switch (error_type) {
        case ERR_NONE:         break;
        case ERR_SINGLE_BIT:   inject_single_bit_error(buf, len); break;
        case ERR_TWO_ISOLATED: inject_two_isolated_bit_errors(buf, len); break;
        case ERR_ODD:          inject_odd_bit_errors(buf, len, 3); break;
        case ERR_BURST:        inject_burst_error(buf, len, 8); break;
        default: break;
    }
}

const char *scheme_name(int scheme) {
    return scheme == SCHEME_CHECKSUM ? "CHECKSUM" : "CRC";
}

const char *error_type_name(int error_type) {
    switch (error_type) {
        case ERR_NONE:         return "NONE";
        case ERR_SINGLE_BIT:   return "SINGLE_BIT";
        case ERR_TWO_ISOLATED: return "TWO_ISOLATED";
        case ERR_ODD:          return "ODD";
        case ERR_BURST:        return "BURST";
        default:               return "UNKNOWN";
    }
}
