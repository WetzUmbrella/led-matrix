// Pico W LED wall coordinator
//  - USE_ROUTER 1: joins an external router (needed for 5+ panels, the Pico AP caps at 4 clients)
//    USE_ROUTER 0: runs its own "LEDWALL" WiFi access point + DHCP (fallback, max 4 panels)
//  - keeps a live list of panels (they send HELLO every 0.3s, dropped after 5s silence)
//  - each panel has a permanent board number (ESP-1, ESP-2, ...), set once per board with
//    panel_sync/set_board_number.sh
//  - the wall is the panels connected right now, lined up by board number (lowest = top-left),
//    so the GIF splits by how many are on: 1=1x1, 2=2x1, 3=3x1, 4=2x2, 5-6=3x2, 7-8=4x2
//  - broadcasts a beacon 10x/s with the frame clock AND the layout (who is in which slot)

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "dhcpserver.h"

#define USE_ROUTER       1             // 0 = Pico runs its own access point (max 4 panels)

// Router credentials live in wifi_secrets.h (git-ignored, copy wifi_secrets.example.h)
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#elif USE_ROUTER
#warning "wifi_secrets.h missing - using placeholder router credentials"
#endif
#ifndef ROUTER_SSID
#define ROUTER_SSID      "CHANGE_ME"
#define ROUTER_PASS      "CHANGE_ME"
#endif

#define WIFI_SSID        "LEDWALL"     // own access point, only used when USE_ROUTER is 0
#define WIFI_PASS        "ledwall123"
#define SYNC_PORT        4210          // Pico -> panels (broadcast)
#define HELLO_PORT       4211          // panels -> Pico (unicast)
#define SYNC_MAGIC       0x4C454457    // "LEDW"
#define HELLO_MAGIC      0x48454C4F    // "HELO"
#define PROTO_VERSION    3             // 3: HELLO carries the board number
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
    uint8_t  board;               // board number from the panel's flash, 0 = never set
} hello_t;

// Who's connected, indexed by board number: board_id[n-1] is ESP-n's panel id, 0 = not connected.
// Only touched with the lwIP lock held.
static uint32_t board_id[MAX_NODES];
static uint32_t board_seen[MAX_NODES];
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

// Line up the connected panels by board number, no gaps. ids[k] is the panel in slot k
// (slot 0 = top-left), boards[k] its board number. Returns how many are connected.
static int pack_wall(uint32_t ids[MAX_NODES], uint8_t boards[MAX_NODES])
{
    int n = 0;
    memset(ids, 0, MAX_NODES * sizeof(ids[0]));
    for (int i = 0; i < MAX_NODES; i++) {
        if (!board_id[i]) continue;
        ids[n] = board_id[i];
        boards[n] = (uint8_t)(i + 1);
        n++;
    }
    return n;
}

static void print_layout(void)
{
    uint32_t ids[MAX_NODES];
    uint8_t boards[MAX_NODES], c, r;
    int n = pack_wall(ids, boards);
    layout_for(n, &c, &r);
    printf("LAYOUT v%u: %d panel(s), %ux%u\n", layout_ver, n, c, r);
    for (int k = 0; k < n; k++)
        printf("  slot %d -> ESP-%u (%08lx)\n", k + 1, boards[k], (unsigned long)ids[k]);
}

// HELLOs arrive every 0.3s, so rate-limit complaints about bad panels to one per 5s
static bool warn_ok(uint32_t now)
{
    static uint32_t last = 0;
    if (last && now - last < 5000) return false;
    last = now;
    return true;
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

        if (h.board < 1 || h.board > MAX_NODES) {
            if (warn_ok(now))
                printf("IGNORED %08lx: board number %u not in 1-%d, run set_board_number.sh\n",
                       (unsigned long)h.id, h.board, MAX_NODES);
            pbuf_free(p);
            return;
        }

        int s = h.board - 1;
        if (board_id[s] == h.id) {                 // already in the wall, just refresh
            board_seen[s] = now;
        } else if (board_id[s]) {                  // two boards set to the same number
            if (warn_ok(now))
                printf("IGNORED %08lx: ESP-%u is already %08lx, give it another number\n",
                       (unsigned long)h.id, h.board, (unsigned long)board_id[s]);
        } else {
            for (int i = 0; i < MAX_NODES; i++)    // board was renumbered: forget its old number
                if (board_id[i] == h.id) board_id[i] = 0;
            board_id[s] = h.id;
            board_seen[s] = now;
            layout_ver++;
            printf("JOIN  ESP-%u (%08lx)\n", h.board, (unsigned long)h.id);
            print_layout();
        }
    }
    pbuf_free(p);
}

// Drop any panel we haven't heard from. The rest close up and the GIF re-splits.
static void expire_nodes(uint32_t now)
{
    bool changed = false;
    for (int i = 0; i < MAX_NODES; i++) {
        if (board_id[i] && now - board_seen[i] > NODE_TIMEOUT_MS) {
            printf("LEAVE ESP-%d (%08lx)\n", i + 1, (unsigned long)board_id[i]);
            board_id[i] = 0;
            changed = true;
        }
    }
    if (changed) {
        layout_ver++;
        print_layout();
    }
}

#if USE_ROUTER
// Join the router, retrying forever. Blocks until we have an IP from its DHCP.
static void router_connect(void)
{
    int tries = 0;
    uint32_t auth = ROUTER_PASS[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;   // "" = open network
    printf("Joining %s...\n", ROUTER_SSID);
    while (cyw43_arch_wifi_connect_timeout_ms(ROUTER_SSID, ROUTER_PASS[0] ? ROUTER_PASS : NULL,
                                              auth, 15000) != 0) {
        printf("  join failed (try %d), retrying\n", ++tries);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, tries & 1);
    }
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);      // radio never naps, fewer dropped packets
}

// Subnet broadcast (e.g. 192.168.1.255) from whatever address the router gave us
static ip_addr_t router_bcast(void)
{
    struct netif *nif = &cyw43_state.netif[CYW43_ITF_STA];
    cyw43_arch_lwip_begin();
    uint32_t ip = ip4_addr_get_u32(netif_ip4_addr(nif));
    uint32_t nm = ip4_addr_get_u32(netif_ip4_netmask(nif));
    printf("Joined %s, ip=%s", ROUTER_SSID, ip4addr_ntoa(netif_ip4_addr(nif)));
    cyw43_arch_lwip_end();

    ip_addr_t bcast = IPADDR4_INIT(ip | ~nm);
    printf(" bcast=%s\n", ipaddr_ntoa(&bcast));
    return bcast;
}
#endif

int main(void)
{
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("WiFi chip init failed\n");
        return 1;
    }

#if USE_ROUTER
    cyw43_arch_enable_sta_mode();
    router_connect();
    ip_addr_t bcast = router_bcast();
#else
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
#endif

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
#if USE_ROUTER
        // Lost the router? Rejoin. Beacons pause, but panels keep animating on their own clock.
        if (seq % 10 == 0 &&
            cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
            printf("Lost %s, rejoining\n", ROUTER_SSID);
            router_connect();
            bcast = router_bcast();
        }
#endif
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
        uint32_t ids[MAX_NODES];
        uint8_t boards[MAX_NODES];
        b.count = (uint8_t)pack_wall(ids, boards);
        layout_for(b.count, &b.cols, &b.rows);
        memcpy(b.ids, ids, sizeof(ids));                // b.ids is packed (unaligned), so copy

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
