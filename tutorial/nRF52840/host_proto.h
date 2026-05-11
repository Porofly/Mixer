// Host <-> dongle binary framing for the Mixer tutorial firmware.
//
// Frame on the wire (size_lo first, little-endian):
//   [size_lo][size_hi][type][slot][payload ... (size-2 bytes)]
//
// size counts (type + slot + payload), i.e. size = 2 + payload_len.
// Maximum frame size is bounded by MX_PAYLOAD_SIZE for binary payload frames
// plus a small headroom for log lines.

#ifndef HOST_PROTO_H_
#define HOST_PROTO_H_

#include <stdint.h>

#define HOST_PROTO_VERSION_STRING "mixer-binary-proto v1\n"

// Host -> dongle
#define HP_TYPE_TX_PAYLOAD   0x10u  // payload: MX_PAYLOAD_SIZE bytes (queued for next round)

// Dongle -> host
#define HP_TYPE_RX_PAYLOAD   0x20u  // payload: MX_PAYLOAD_SIZE bytes (decoded from slot)
#define HP_TYPE_ROUND_STATS  0x30u  // payload: 7 x uint32_le (see hp_round_stats_t)
#define HP_TYPE_LOG          0x40u  // payload: ASCII text (no NUL terminator), no inner framing

typedef struct __attribute__((packed)) hp_round_stats_s {
    uint32_t round;
    uint32_t rank;
    uint32_t decoded;
    uint32_t not_decoded;
    uint32_t weak;
    uint32_t wrong;
    uint32_t node_id;  // physical TOS_NODE_ID
} hp_round_stats_t;

#endif  // HOST_PROTO_H_
