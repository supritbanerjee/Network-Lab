#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

/* ---- Ethernet IEEE 802.3 style framing limits ---- */
#define MIN_PAYLOAD   46      /* minimum payload bytes                 */
#define MAX_PAYLOAD   1500    /* maximum payload bytes                 */
#define HEADER_SIZE   16      /* bytes, serialized on the wire         */

/* ---- Error detection schemes ---- */
#define SCHEME_CHECKSUM 0
#define SCHEME_CRC      1

/* ---- Error types injected by the sender (ground truth, for logging) ---- */
#define ERR_NONE          0
#define ERR_SINGLE_BIT     1
#define ERR_TWO_ISOLATED   2
#define ERR_ODD            3
#define ERR_BURST          4

/*
 * Frame header sent before every payload.
 * NOTE: 'error_type' and 'frame_id' are carried only so the sender/receiver
 * can log ground truth vs detection result for your evaluation tables.
 * The receiver's ACCEPT/REJECT decision is based ONLY on recomputing the
 * checksum/CRC over the received (possibly corrupted) payload and comparing
 * it to the 'redundancy' field - exactly like a real receiver would.
 */
typedef struct {
    uint32_t frame_id;
    uint8_t  scheme;      /* SCHEME_CHECKSUM or SCHEME_CRC            */
    uint8_t  crc_type;    /* 8, 10, 16, 32 (ignored for checksum)     */
    uint8_t  error_type;  /* ground-truth error injected, for logging */
    uint8_t  reserved;
    uint16_t payload_len;
    uint16_t reserved2;
    uint32_t redundancy;  /* checksum or CRC value                    */
} FrameHeader;

void header_serialize(const FrameHeader *h, uint8_t buf[HEADER_SIZE]);
void header_deserialize(FrameHeader *h, const uint8_t buf[HEADER_SIZE]);

/* Reliable send/recv over TCP (handle short reads/writes) */
int send_all(int sock, const void *buf, size_t len);
int recv_all(int sock, void *buf, size_t len);

/* ---- Checksum (16-bit, one's complement, IP-style) ---- */
uint16_t checksum16_compute(const uint8_t *data, size_t len);

/* ---- Generic bit-by-bit CRC, width in {8,10,16,32} ---- */
uint32_t crc_compute(const uint8_t *data, size_t len, uint32_t poly, int width);
uint32_t crc_poly_for_type(int crc_type);

/* ---- Error injection (operates on the buffer that will be transmitted) ---- */
void inject_single_bit_error(uint8_t *buf, size_t len);
void inject_two_isolated_bit_errors(uint8_t *buf, size_t len);
void inject_odd_bit_errors(uint8_t *buf, size_t len, int count);
void inject_burst_error(uint8_t *buf, size_t len, int burst_len);
void apply_error(uint8_t *buf, size_t len, int error_type);

const char *scheme_name(int scheme);
const char *error_type_name(int error_type);

#endif
