// Pico W LED wall coordinator
//  - runs the "LEDWALL" WiFi access point + DHCP
//  - keeps a live list of panels (they send HELLO every 0.5s, dropped after 2s silence)
//  - panels keep their number while alive; new panels take the lowest free number
//  - picks the wall layout from the highest number in use (1=1x1, 2=2x1, 3=3x1, 4=2x2, ...)
//  - broadcasts a beacon 10x/s with the frame clock AND the layout (who is in which slot)

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "dhcpserver.h"

#define WIFI_SSID        "LEDWALL"
#define WIFI_PASS        "ledwall123"
#define SYNC_PORT        4210          // Pico -> panels (broadcast)
#define HELLO_PORT       4211          // panels -> Pico (unicast)
#define SYNC_MAGIC       0x4C454457    // "LEDW"
#define HELLO_MAGIC      0x48454C4F    // "HELO"
#define PROTO_VERSION    2
#define FPS              30
#define BEACON_MS        100
#define NODE_TIMEOUT_MS  5000          // generous: a few lost HELLOs must not kick a panel out
#define MAX_NODES        8

enum { CMD_PLAY = 1 };

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint32_t seq;
    uint8_t  cmd;
    uint32_t frame_no;
    uint8_t  layout_ver;          // bumps every time a panel joins or leaves
    uint8_t  count;               // panels in the wall
    uint8_t  cols;
    uint8_t  rows;
    uint32_t ids[MAX_NODES];      // panel IDs in slot order (slot 0 = top-left)
} beacon_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t id;
} hello_t;

// Slot table. A panel keeps its slot (its number) for as long as it stays alive.
// slot_id[i] == 0 means slot i is empty. slot_last[i] remembers who last owned it,
// so a panel that reboots or drops out briefly gets its OLD number back.
// Only touched with the lwIP lock held.
static uint32_t slot_id[MAX_NODES];
static uint32_t slot_seen[MAX_NODES];
static uint32_t slot_last[MAX_NODES];
static uint8_t  layout_ver = 0;

static void layout_for(int n, uint8_t *cols, uint8_t *rows)
{
    switch (n) {
        case 0: case 1: *cols = 1; *rows = 1; break;
        case 2:         *cols = 2; *rows = 1; break;
        case 3:         *cols = 3; *rows = 1; break;
        case 4:         *cols = 2; *rows = 2; break;
        case 5: case 6: *cols = 3; *rows = 2; break;
        default:        *cols = 4; *rows = 2; break;
    }
}

// Grid size = highest occupied slot. Empty slots in the middle stay as dark holes,
// so nobody else moves when a panel leaves.
static int grid_slots(void)
{
    int n = 0;
    for (int i = 0; i < MAX_NODES; i++)
        if (slot_id[i]) n = i + 1;
    return n;
}

static void print_layout(void)
{
    uint8_t c, r;
    int n = grid_slots();
    layout_for(n, &c, &r);
    printf("LAYOUT v%u: %d slot(s), %ux%u\n", layout_ver, n, c, r);
    for (int i = 0; i < n; i++) {
        if (slot_id[i]) printf("  panel %d -> %08lx\n", i + 1, (unsigned long)slot_id[i]);
        else            printf("  panel %d -> (empty)\n", i + 1);
    }
}

// Called by lwIP (lock already held) when a HELLO arrives
static void hello_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port)
{
    hello_t h;
    if (p->tot_len == sizeof(h) &&
        pbuf_copy_partial(p, &h, sizeof(h), 0) == sizeof(h) &&
        h.magic == HELLO_MAGIC && h.id != 0) {

        uint32_t now = to_ms_since_boot(get_absolute_time());

        for (int i = 0; i < MAX_NODES; i++) {
            if (slot_id[i] == h.id) {             // already in the wall, just refresh
                slot_seen[i] = now;
                pbuf_free(p);
                return;
            }
        }

        // New (or returning) panel: take back its old slot if free, else the lowest free slot
        int pick = -1;
        for (int i = 0; i < MAX_NODES; i++)
            if (!slot_id[i] && slot_last[i] == h.id) { pick = i; break; }
        if (pick < 0)
            for (int i = 0; i < MAX_NODES; i++)
                if (!slot_id[i]) { pick = i; break; }

        if (pick >= 0) {
            slot_id[pick] = h.id;
            slot_seen[pick] = now;
            slot_last[pick] = h.id;
            layout_ver++;
            printf("JOIN  %08lx as panel %d\n", (unsigned long)h.id, pick + 1);
            print_layout();
        }
    }
    pbuf_free(p);
}

// Free the slot of any panel we haven't heard from. Everyone else keeps their number.
static void expire_nodes(uint32_t now)
{
    bool changed = false;
    for (int i = 0; i < MAX_NODES; i++) {
        if (slot_id[i] && now - slot_seen[i] > NODE_TIMEOUT_MS) {
            printf("LEAVE %08lx (panel %d)\n", (unsigned long)slot_id[i], i + 1);
            slot_id[i] = 0;
            changed = true;
        }
    }
    if (changed) {
        layout_ver++;
        print_layout();
    }
}

int main(void)
{
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("WiFi chip init failed\n");
        return 1;
    }

    cyw43_arch_enable_ap_mode(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);      // radio never naps, fewer dropped packets

    ip_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

    struct netif *ap_nif = &cyw43_state.netif[CYW43_ITF_AP];
    cyw43_arch_lwip_begin();
    netif_set_addr(ap_nif, ip_2_ip4(&gw), ip_2_ip4(&mask), ip_2_ip4(&gw));
    cyw43_arch_lwip_end();

    dhcp_server_t dhcp;
    dhcp_server_init(&dhcp, ap_nif, &gw, &mask);

    ip_addr_t bcast;
    IP4_ADDR(ip_2_ip4(&bcast), 192, 168, 4, 255);

    cyw43_arch_lwip_begin();
    struct udp_pcb *tx = udp_new();
    struct udp_pcb *rx = udp_new();
    udp_bind(rx, IP_ANY_TYPE, HELLO_PORT);
    udp_recv(rx, hello_recv, NULL);
    cyw43_arch_lwip_end();

    printf("LEDWALL up. Beacons on %d, HELLOs on %d\n", SYNC_PORT, HELLO_PORT);

    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    uint32_t seq = 0;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        beacon_t b;
        memset(&b, 0, sizeof(b));
        b.magic    = SYNC_MAGIC;
        b.version  = PROTO_VERSION;
        b.seq      = seq;
        b.cmd      = CMD_PLAY;
        b.frame_no = (uint32_t)((uint64_t)(now - start_ms) * FPS / 1000);

        cyw43_arch_lwip_begin();
        expire_nodes(now);
        b.layout_ver = layout_ver;
        b.count = (uint8_t)grid_slots();
        layout_for(b.count, &b.cols, &b.rows);
        memcpy(b.ids, slot_id, sizeof(slot_id));        // 0 = empty slot

        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(b), PBUF_RAM);
        if (p) {
            memcpy(p->payload, &b, sizeof(b));
            udp_sendto(tx, p, &bcast, SYNC_PORT);
            pbuf_free(p);
        }
        cyw43_arch_lwip_end();

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (seq / 5) & 1);
        seq++;
        sleep_ms(BEACON_MS);
    }
}
