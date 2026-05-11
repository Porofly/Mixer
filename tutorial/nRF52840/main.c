/***************************************************************************************************
 *
 *  Mixer tutorial firmware for nRF52840 USB dongle (PCA10059), modified for a
 *  binary host protocol (see host_proto.h).
 *
 *  Host <-> dongle framing:
 *    [size_lo][size_hi][type][slot][payload...]   (size = 2 + payload_len)
 *
 *  Frames emitted by the dongle:
 *    HP_TYPE_LOG          one-line boot banner
 *    HP_TYPE_ROUND_STATS  one per finished round
 *    HP_TYPE_RX_PAYLOAD   one per successfully decoded slot
 *
 *  Frames accepted from the host:
 *    HP_TYPE_TX_PAYLOAD   queues a payload for `slot` on the next round
 *                         (slots not owned by this node are silently ignored)
 *
 *  Original copyright: NES Lab, TU Dresden — see Mixer/LICENSE.
 *
 **************************************************************************************************/

#include "gpi/trace.h"

#define TRACE_INFO  GPI_TRACE_MSG_TYPE_INFO

#ifndef GPI_TRACE_BASE_SELECTION
  #define GPI_TRACE_BASE_SELECTION  GPI_TRACE_LOG_STANDARD | GPI_TRACE_LOG_PROGRAM_FLOW
#endif
GPI_TRACE_CONFIG(main, GPI_TRACE_BASE_SELECTION);

#include "mixer/mixer.h"
#include "gpi/tools.h"
#include "gpi/platform.h"
#include "gpi/interrupts.h"
#include "gpi/clocks.h"
#include "gpi/olf.h"
#include GPI_PLATFORM_PATH(radio.h)

#include "host_proto.h"

#include <nrf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void     mixer_usb_cdc_task(void);
extern bool     mixer_usb_cdc_connected(void);
extern uint32_t mixer_usb_cdc_available(void);
extern uint32_t mixer_usb_cdc_read(void *buffer, uint32_t bufsize);
extern void     mixer_usb_cdc_write_raw(const void *buffer, uint32_t bufsize);
extern void     mixer_usb_cdc_flush(void);

#if __has_include("node_id.h")
#  include "node_id.h"
#endif
#ifndef BUILD_NODE_ID
#  define BUILD_NODE_ID 0
#endif
uint16_t __attribute__((section(".data"))) TOS_NODE_ID = BUILD_NODE_ID;

static uint8_t node_id;          // logical (index into nodes[])
static uint32_t round_counter;
static uint32_t msgs_decoded, msgs_not_decoded, msgs_weak, msgs_wrong;

// TX queue: one pending payload per slot, "valid" flag per slot.
// Host writes TX_PAYLOAD frames at any time during the idle window; the next
// round picks them up and clears the valid flag. Marked volatile because the
// idle-window poller path and the round-start path observe the same memory
// across function-call boundaries; the compiler is free to assume non-volatile
// memory cannot change between calls.
static volatile uint8_t tx_queue[MX_GENERATION_SIZE][MX_PAYLOAD_SIZE];
static volatile uint8_t tx_queue_valid[MX_GENERATION_SIZE];

//==================================================================================================
// Binary frame emission
//==================================================================================================

static void hp_emit_frame(uint8_t type, uint8_t slot, const void *payload, uint16_t payload_len)
{
    uint8_t hdr[4];
    hdr[0] = (uint8_t)(2 + payload_len);          // size_lo
    hdr[1] = (uint8_t)((2 + payload_len) >> 8);   // size_hi
    hdr[2] = type;
    hdr[3] = slot;
    mixer_usb_cdc_write_raw(hdr, sizeof(hdr));
    if (payload_len > 0) {
        mixer_usb_cdc_write_raw(payload, payload_len);
    }
    mixer_usb_cdc_flush();
}

static void hp_emit_log(const char *s)
{
    uint16_t len = 0;
    while (s[len] != '\0') len++;
    hp_emit_frame(HP_TYPE_LOG, 0, s, len);
}

//==================================================================================================
// Host RX: parse incoming frames during the idle window
//==================================================================================================

// State machine for the host->dongle stream. Bounded; if we ever see a frame
// larger than HP_RX_MAX we just resync by dropping bytes.
#define HP_RX_MAX  (2 + MX_PAYLOAD_SIZE)  // type + slot + max payload

typedef enum {
    HP_RX_WAIT_SIZE_LO,
    HP_RX_WAIT_SIZE_HI,
    HP_RX_PAYLOAD,
} hp_rx_state_t;

static struct {
    hp_rx_state_t state;
    uint16_t expected;
    uint16_t have;
    uint8_t  buf[HP_RX_MAX];
} hp_rx = { HP_RX_WAIT_SIZE_LO, 0, 0, {0} };

static uint32_t hp_rx_frames_total = 0;
static uint32_t hp_rx_frames_tx_ok = 0;

static void hp_rx_handle_frame(const uint8_t *frame, uint16_t len)
{
    hp_rx_frames_total++;
    if (len < 2) return;
    uint8_t type = frame[0];
    uint8_t slot = frame[1];
    const uint8_t *payload = &frame[2];
    uint16_t payload_len = len - 2;

    switch (type) {
        case HP_TYPE_TX_PAYLOAD:
            if (slot < MX_GENERATION_SIZE && payload_len == MX_PAYLOAD_SIZE) {
                for (uint16_t k = 0; k < MX_PAYLOAD_SIZE; ++k) {
                    tx_queue[slot][k] = payload[k];
                }
                tx_queue_valid[slot] = 1;
                hp_rx_frames_tx_ok++;
            }
            break;
        default:
            break;
    }
}

static void hp_rx_feed(uint8_t b)
{
    switch (hp_rx.state) {
        case HP_RX_WAIT_SIZE_LO:
            hp_rx.expected = b;
            hp_rx.state = HP_RX_WAIT_SIZE_HI;
            break;
        case HP_RX_WAIT_SIZE_HI:
            hp_rx.expected |= ((uint16_t)b) << 8;
            if (hp_rx.expected < 2 || hp_rx.expected > HP_RX_MAX) {
                // Implausible -- drop and resync.
                hp_rx.state = HP_RX_WAIT_SIZE_LO;
            } else {
                hp_rx.have = 0;
                hp_rx.state = HP_RX_PAYLOAD;
            }
            break;
        case HP_RX_PAYLOAD:
            hp_rx.buf[hp_rx.have++] = b;
            if (hp_rx.have == hp_rx.expected) {
                hp_rx_handle_frame(hp_rx.buf, hp_rx.expected);
                hp_rx.state = HP_RX_WAIT_SIZE_LO;
            }
            break;
    }
}

static void hp_pump_host_rx(void)
{
    // Pump TinyUSB (also services TX) and drain any pending bytes.
    mixer_usb_cdc_task();
    if (!mixer_usb_cdc_connected()) return;
    while (mixer_usb_cdc_available()) {
        uint8_t b;
        if (mixer_usb_cdc_read(&b, 1) == 1) {
            hp_rx_feed(b);
        } else {
            break;
        }
    }
}

//==================================================================================================
// Mixer wrapper
//==================================================================================================

static void initialization(void)
{
    gpi_platform_init();
    gpi_int_enable();

    // RNG seed source -- start now, harvest later.
    NRF_RNG->INTENCLR = BV_BY_NAME(RNG_INTENCLR_VALRDY, Clear);
    NRF_RNG->CONFIG = BV_BY_NAME(RNG_CONFIG_DERCEN, Enabled);
    NRF_RNG->TASKS_START = 1;

    SysTick->LOAD = -1u;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    gpi_radio_init(MX_PHY_MODE);
    gpi_radio_set_tx_power(gpi_radio_dbm_to_power_level(MX_TX_PWR_DBM));
    switch (MX_PHY_MODE) {
        case BLE_1M:
        case BLE_2M:
        case BLE_125k:
        case BLE_500k:
            gpi_radio_set_channel(39);
            gpi_radio_ble_set_access_address(~0x8E89BED6);
            break;
        case IEEE_802_15_4:
            gpi_radio_set_channel(26);
            break;
        default:
            // Bad config -- spin.
            while (1) {}
    }

    if (TOS_NODE_ID == 0) {
        // No BUILD_NODE_ID -- can't run. Spin forever; LOG will tell the host once connected.
        while (!mixer_usb_cdc_connected()) { mixer_usb_cdc_task(); }
        hp_emit_log("FATAL: BUILD_NODE_ID is 0\n");
        while (1) { mixer_usb_cdc_task(); }
    }

    NRF_RNG->TASKS_STOP = 1;
    uint8_t rng_value = BV_BY_VALUE(RNG_VALUE_VALUE, NRF_RNG->VALUE);
    uint32_t rng_seed = rng_value * gpi_mulu_16x16(TOS_NODE_ID, gpi_tick_fast_native());
    mixer_rand_seed(rng_seed);

    for (node_id = 0; node_id < NUM_ELEMENTS(nodes); node_id++) {
        if (nodes[node_id] == TOS_NODE_ID) break;
    }
    if (node_id >= NUM_ELEMENTS(nodes)) {
        while (!mixer_usb_cdc_connected()) { mixer_usb_cdc_task(); }
        hp_emit_log("FATAL: TOS_NODE_ID not in nodes[]\n");
        while (1) { mixer_usb_cdc_task(); }
    }

    // Wait for host to attach so the banner isn't lost in the boot enumeration race.
    while (!mixer_usb_cdc_connected()) { mixer_usb_cdc_task(); }
    hp_emit_log(HOST_PROTO_VERSION_STRING);
}

int main(void)
{
    Gpi_Hybrid_Tick t_ref;
    unsigned int i;

    initialization();

    t_ref = gpi_tick_hybrid();

    for (round_counter = 1; 1; round_counter++) {
        mixer_init(node_id);

#if MX_WEAK_ZEROS
        mixer_set_weak_release_slot(WEAK_RELEASE_SLOT);
        mixer_set_weak_return_msg((void *)-1);
#endif

        // Push queued host payloads into mixer for slots we own. When the
        // host has nothing queued we still send a zero-padded payload so the
        // network stays synchronised even when no application traffic is
        // active (Mixer relies on every assigned slot transmitting).
        static const uint8_t zero_payload[MX_PAYLOAD_SIZE] = {0};
        uint8_t scratch[MX_PAYLOAD_SIZE];
        for (i = 0; i < MX_GENERATION_SIZE; i++) {
            if (payload_distribution[i] != TOS_NODE_ID) continue;
            if (tx_queue_valid[i]) {
                // copy volatile queue into a non-volatile scratch so we can
                // hand a plain uint8_t* to the mixer API.
                for (uint16_t k = 0; k < MX_PAYLOAD_SIZE; ++k) scratch[k] = tx_queue[i][k];
                tx_queue_valid[i] = 0;
                mixer_write(i, scratch, MX_PAYLOAD_SIZE);
            } else {
                mixer_write(i, (void *)zero_payload, MX_PAYLOAD_SIZE);
            }
        }

        mixer_arm(
            ((MX_INITIATOR_ID == TOS_NODE_ID) ? MX_ARM_INITIATOR : 0) |
            ((1 == round_counter) ? MX_ARM_INFINITE_SCAN : 0));

        if (MX_INITIATOR_ID == TOS_NODE_ID) {
            t_ref += 3 * MX_SLOT_LENGTH;
        }

        // Idle window: service host RX so TX_PAYLOAD frames can land before next round.
        while (gpi_tick_compare_hybrid(gpi_tick_hybrid(), t_ref) < 0) {
            hp_pump_host_rx();
        }

        t_ref = mixer_start();

        // Round in progress -- do NOT touch USB.
        while (gpi_tick_compare_hybrid(gpi_tick_hybrid(), t_ref) < 0);

        // Evaluate received slots and emit RX_PAYLOAD frames.
        msgs_decoded = msgs_not_decoded = msgs_weak = msgs_wrong = 0;
        uint32_t rank = 0;
        for (i = 0; i < MX_GENERATION_SIZE; i++) {
            if (mixer_stat_slot(i) >= 0) rank++;
            void *p = mixer_read(i);
            if (p == NULL) {
                msgs_not_decoded++;
            } else if (p == (void *)-1) {
                msgs_weak++;
            } else {
                msgs_decoded++;
                hp_emit_frame(HP_TYPE_RX_PAYLOAD, (uint8_t)i, p, MX_PAYLOAD_SIZE);
            }
        }

        // ROUND_STATS at the end so the host can correlate stats with the
        // RX_PAYLOAD frames it just received.
        hp_round_stats_t st = {
            .round       = round_counter,
            .rank        = rank,
            .decoded     = msgs_decoded,
            .not_decoded = msgs_not_decoded,
            .weak        = msgs_weak,
            .wrong       = msgs_wrong,
            .node_id     = TOS_NODE_ID,
        };
        hp_emit_frame(HP_TYPE_ROUND_STATS, 0, &st, sizeof(st));

        // Debug: also emit a tiny LOG line summarising host-RX activity. We
        // only emit one every 8 rounds to avoid drowning the host link.
        if ((round_counter & 0x7) == 0) {
            char buf[64];
            int n = 0;
            // simple manual itoa to avoid pulling printf back in
            n += 0; // placeholder
            const char *s1 = "hp_rx total=";
            for (const char *p = s1; *p && n < (int)sizeof(buf); ++p) buf[n++] = *p;
            // uint32 to decimal
            uint32_t v = hp_rx_frames_total;
            char tmp[12]; int tn = 0;
            if (v == 0) tmp[tn++] = '0';
            while (v > 0) { tmp[tn++] = '0' + (v % 10); v /= 10; }
            while (tn-- > 0 && n < (int)sizeof(buf)) buf[n++] = tmp[tn];
            const char *s2 = " tx_ok=";
            for (const char *p = s2; *p && n < (int)sizeof(buf); ++p) buf[n++] = *p;
            v = hp_rx_frames_tx_ok; tn = 0;
            if (v == 0) tmp[tn++] = '0';
            while (v > 0) { tmp[tn++] = '0' + (v % 10); v /= 10; }
            while (tn-- > 0 && n < (int)sizeof(buf)) buf[n++] = tmp[tn];
            if (n < (int)sizeof(buf)) buf[n++] = '\n';
            hp_emit_frame(HP_TYPE_LOG, 0, buf, (uint16_t)n);
        }

        // Schedule next round, leaving enough headroom that we don't oversleep
        // the deadline while the USB FIFO drains.
        t_ref += MAX(10 * MX_SLOT_LENGTH, GPI_TICK_MS_TO_HYBRID2(1000));
    }

    GPI_TRACE_RETURN(0);
}
