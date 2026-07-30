#include "aura_net.h"
#include "rtl8139.h"
#include "serial.h"
#include "memory.h"

/* Heap allocator declarations */
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

/* -----------------------------------------------
 * Aura-Net Implementation
 *
 * Lightweight networking stack operating directly
 * on raw Ethernet frames via the RTL8139 driver.
 * Extended with:
 *   - COM1 Serial Radio Bridge (LoRa/Ham radio)
 *   - Resource State Machine (Logistics Router)
 *   - Store & Forward (Mesh Packet Relay)
 * ----------------------------------------------- */

/* Aura-Net statistics */
aura_stats_t aura_stats;

/* ARP cache table */
static arp_entry_t arp_table[ARP_TABLE_SIZE];

/* Mesh node list for the live radar UI */
static mesh_node_t mesh_nodes[MAX_MESH_NODES];
static int mesh_node_count = 0;

/* Mock GPS base coordinates for this node (demo purposes) */
static int32_t our_gps_lat = 4071;   /* 40.71 N (New York, degrees * 100) */
static int32_t our_gps_lon = -7400;  /* -74.00 W (New York, degrees * 100) */

/* Incoming signal detection variables */
volatile uint8_t  incoming_signal_pending = 0;
uint8_t  incoming_sender_mac[6] = {0};
int32_t  incoming_gps_lat = 0;
int32_t  incoming_gps_lon = 0;
uint32_t incoming_timestamp = 0;

/* Broadcast MAC address */
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* Our (hardcoded) IP address for demo purposes */
#define OUR_IP_ADDR  0x0A000001  /* 10.0.0.1 */

/* Heartbeat counter */
static uint32_t last_heartbeat = 0;

/* ==========================================================
 * Resource State Machine
 * ========================================================== */

/* Logistics matches table for the UI */
static logistics_match_t logistics_matches[MAX_LOGISTICS_MATCHES];
static int logistics_match_count = 0;

/* ==========================================================
 * Store & Forward Queue
 * ========================================================== */

static snf_queue_entry_t snf_queue[SNF_QUEUE_SIZE];

/* ==========================================================
 * Helper: Compare two MAC addresses
 * ========================================================== */
static int mac_cmp(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* Helper: Copy MAC address */
static void mac_copy(uint8_t* dst, const uint8_t* src) {
    for (int i = 0; i < 6; i++) dst[i] = src[i];
}

/* Helper: Compare resource IDs (8-byte fixed) */
static int res_id_cmp(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < RESOURCE_ID_LEN; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* ==========================================================
 * Initialize Aura-Net
 * ========================================================== */
void init_aura_net(void) {
    write_serial("[AURA-NET] Initializing Aura-Net...\r\n");

    aura_stats.frames_sent = 0;
    aura_stats.frames_received = 0;
    aura_stats.arp_resolved = 0;
    aura_stats.heartbeats_sent = 0;
    aura_stats.heartbeats_received = 0;
    aura_stats.our_ip = OUR_IP_ADDR;
    aura_stats.serial_frames_sent = 0;
    aura_stats.serial_frames_received = 0;
    aura_stats.packets_relayed = 0;
    aura_stats.packets_dropped = 0;
    aura_stats.resources_matched = 0;
    aura_stats.resources_advertised = 0;

    /* Get our MAC address from the RTL8139 driver */
    rtl8139_get_mac(aura_stats.our_mac);

    /* Clear ARP table */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        arp_table[i].valid = 0;
    }

    /* Clear logistics matches */
    logistics_match_count = 0;

    /* Clear S&F queue */
    for (int i = 0; i < SNF_QUEUE_SIZE; i++) {
        snf_queue[i].valid = 0;
    }

    write_serial("[AURA-NET] Our MAC: ");
    for (int i = 0; i < 6; i++) {
        write_serial_hex(aura_stats.our_mac[i]);
        if (i < 5) write_serial(":");
    }
    write_serial(" IP: 10.0.0.1\r\n");

    write_serial("[AURA-NET] Serial bridge active on COM1 (IRQ4)\r\n");
    write_serial("[AURA-NET] Resource logistics & S&F relay ready\r\n");
    write_serial("[AURA-NET] Aura-Net initialized.\r\n");
}

/* ==========================================================
 * Ethernet Frame Transmission
 * ========================================================== */
int aura_send_frame(const uint8_t* dst_mac, uint16_t ether_type,
                    const uint8_t* payload, uint32_t payload_len) {
    uint32_t frame_len = sizeof(eth_header_t) + payload_len;
    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) return -1;

    eth_header_t* hdr = (eth_header_t*)frame;
    for (int i = 0; i < 6; i++) {
        hdr->dst_mac[i] = dst_mac[i];
        hdr->src_mac[i] = aura_stats.our_mac[i];
    }
    hdr->ether_type = ether_type;

    for (uint32_t i = 0; i < payload_len; i++) {
        frame[sizeof(eth_header_t) + i] = payload[i];
    }

    int result = rtl8139_send_frame(frame, frame_len);
    kfree(frame);

    if (result == 0) {
        aura_stats.frames_sent++;
    }

    return result;
}

/* ==========================================================
 * Serial Radio Bridge — Dual Transmission
 *
 * Sends a packet over BOTH Ethernet AND COM1 serial.
 * This allows the Aura-Net protocol to bridge between
 * wired Ethernet and radio (LoRa/Ham) networks.
 * ========================================================== */
void aura_broadcast_dual(const uint8_t* dst_mac, uint16_t ether_type,
                         const uint8_t* payload, uint32_t payload_len) {
    /* Send over Ethernet */
    aura_send_frame(dst_mac, ether_type, payload, payload_len);

    /* Send over COM1 serial with framing */
    uint32_t frame_len = sizeof(eth_header_t) + payload_len;
    uint8_t frame_buf[SERIAL_MAX_FRAME];
    if (frame_len <= SERIAL_MAX_FRAME) {
        eth_header_t* hdr = (eth_header_t*)frame_buf;
        for (int i = 0; i < 6; i++) {
            hdr->dst_mac[i] = dst_mac[i];
            hdr->src_mac[i] = aura_stats.our_mac[i];
        }
        hdr->ether_type = ether_type;

        for (uint32_t i = 0; i < payload_len; i++) {
            frame_buf[sizeof(eth_header_t) + i] = payload[i];
        }

        serial_send_frame(frame_buf, frame_len);
        aura_stats.serial_frames_sent++;
    }
}

/* ==========================================================
 * Poll for incoming serial frames and inject them into
 * the Aura-Net frame handler.
 * ========================================================== */
void aura_poll_serial(void) {
    uint8_t buf[SERIAL_MAX_FRAME];
    int len = serial_read_frame(buf, SERIAL_MAX_FRAME);

    if (len > 0) {
        /* We received a complete serial frame */
        aura_stats.serial_frames_received++;

        /* Process it as if it came from Ethernet */
        if (len >= (int)sizeof(eth_header_t)) {
            eth_header_t* hdr = (eth_header_t*)buf;

            /* Skip frames we sent ourselves (check src MAC) */
            if (mac_cmp(hdr->src_mac, aura_stats.our_mac)) return;

            aura_handle_frame(hdr, (uint32_t)len);
        }
    }
}

/* ==========================================================
 * ARP
 * ========================================================== */
void aura_arp_request(uint32_t ip) {
    arp_packet_t arp;
    arp.hw_type = 0x0100;
    arp.proto_type = 0x0008;
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = 0x0100;

    for (int i = 0; i < 6; i++) {
        arp.sender_mac[i] = aura_stats.our_mac[i];
        arp.target_mac[i] = 0;
    }
    arp.sender_ip = aura_stats.our_ip;
    arp.target_ip = ip;

    aura_send_frame(broadcast_mac, ETH_TYPE_ARP, (uint8_t*)&arp, sizeof(arp_packet_t));

    write_serial("[AURA-NET] ARP request sent\r\n");
}

static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    int slot = -1;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip_addr == ip) {
            slot = i;
            break;
        }
        if (!arp_table[i].valid && slot < 0) slot = i;
    }

    if (slot < 0) slot = 0;

    arp_table[slot].valid = 1;
    arp_table[slot].ip_addr = ip;
    for (int i = 0; i < 6; i++) {
        arp_table[slot].mac_addr[i] = mac[i];
    }
}

/* ==========================================================
 * Main Frame Handler
 * ========================================================== */
void aura_handle_frame(eth_header_t* frame, uint32_t length) {
    (void)length;
    aura_stats.frames_received++;

    uint16_t ether_type = frame->ether_type;

    /* ---- ARP ---- */
    if (ether_type == ETH_TYPE_ARP && length >= sizeof(eth_header_t) + sizeof(arp_packet_t)) {
        arp_packet_t* arp = (arp_packet_t*)((uint8_t*)frame + sizeof(eth_header_t));
        arp_cache_add(arp->sender_ip, arp->sender_mac);

        if (arp->opcode == 0x0100 && arp->target_ip == aura_stats.our_ip) {
            arp_packet_t reply;
            reply.hw_type = 0x0100;
            reply.proto_type = 0x0008;
            reply.hw_len = 6;
            reply.proto_len = 4;
            reply.opcode = 0x0200;

            for (int i = 0; i < 6; i++) {
                reply.sender_mac[i] = aura_stats.our_mac[i];
                reply.target_mac[i] = arp->sender_mac[i];
            }
            reply.sender_ip = aura_stats.our_ip;
            reply.target_ip = arp->sender_ip;

            aura_send_frame(arp->sender_mac, ETH_TYPE_ARP, (uint8_t*)&reply, sizeof(arp_packet_t));
            aura_stats.arp_resolved++;
        }
    }

    /* ---- Legacy Heartbeat ---- */
    if (ether_type == ETH_TYPE_HEART) {
        aura_stats.heartbeats_received++;
        write_serial("[AURA-NET] Legacy heartbeat received\r\n");
    }

    /* ---- Emergency / Distress Signal ---- */
    if (ether_type == ETH_TYPE_SOS && length >= sizeof(eth_header_t) + sizeof(aura_emergency_packet_t)) {
        aura_emergency_packet_t* emg = (aura_emergency_packet_t*)((uint8_t*)frame + sizeof(eth_header_t));

        if (emg->header[0] == 'S' && emg->header[1] == 'O' &&
            emg->header[2] == 'S' && emg->header[3] == '!') {

            /* Skip self-originated */
            if (mac_cmp(emg->sender_mac, aura_stats.our_mac)) return;

            /* Store for UI modal */
            mac_copy(incoming_sender_mac, emg->sender_mac);
            incoming_gps_lat = emg->gps_lat;
            incoming_gps_lon = emg->gps_lon;
            incoming_timestamp = emg->timestamp;
            incoming_signal_pending = 1;

            write_serial("[AURA-SOS] *** INCOMING DISTRESS ***\r\n");
        }
    }

    /* ---- Mesh Discovery Packet ---- */
    if (ether_type == ETH_TYPE_MESH && length >= sizeof(eth_header_t) + sizeof(aura_mesh_packet_t)) {
        aura_mesh_packet_t* mesh = (aura_mesh_packet_t*)((uint8_t*)frame + sizeof(eth_header_t));

        if (mesh->header[0] == 'A' && mesh->header[1] == 'U' &&
            mesh->header[2] == 'R' && mesh->header[3] == 'A') {

            aura_stats.heartbeats_received++;

            if (mac_cmp(mesh->sender_mac, aura_stats.our_mac)) return;

            extern uint32_t timer_ticks;

            int found = -1;
            for (int i = 0; i < mesh_node_count; i++) {
                if (mac_cmp(mesh_nodes[i].mac, mesh->sender_mac)) {
                    found = i;
                    break;
                }
            }

            if (found >= 0) {
                mesh_nodes[found].gps_lat = mesh->gps_lat;
                mesh_nodes[found].gps_lon = mesh->gps_lon;
                mesh_nodes[found].status = mesh->node_status;
                mesh_nodes[found].last_seen = timer_ticks;
            } else if (mesh_node_count < MAX_MESH_NODES) {
                int slot = mesh_node_count++;
                mac_copy(mesh_nodes[slot].mac, mesh->sender_mac);
                mesh_nodes[slot].gps_lat = mesh->gps_lat;
                mesh_nodes[slot].gps_lon = mesh->gps_lon;
                mesh_nodes[slot].status = mesh->node_status;
                mesh_nodes[slot].last_seen = timer_ticks;
                mesh_nodes[slot].valid = 1;

                write_serial("[AURA-MESH] New node discovered! Slot: ");
                write_serial_hex(slot);
                write_serial("\r\n");
            }
        }
    }

    /* ---- Resource Logistics Packet ---- */
    if (ether_type == ETH_TYPE_RES) {
        aura_handle_resource_packet(frame, length);
    }
}

/* ==========================================================
 * Heartbeat — now broadcasts over both Ethernet and Serial
 * ========================================================== */
void aura_update_heartbeat(void) {
    extern uint32_t timer_ticks;

    if (timer_ticks - last_heartbeat < HEARTBEAT_INTERVAL) return;
    last_heartbeat = timer_ticks;

    /* Build mesh packet */
    aura_mesh_packet_t mesh_pkt;
    mesh_pkt.header[0] = 'A';
    mesh_pkt.header[1] = 'U';
    mesh_pkt.header[2] = 'R';
    mesh_pkt.header[3] = 'A';
    mac_copy(mesh_pkt.sender_mac, aura_stats.our_mac);
    mesh_pkt.gps_lat = our_gps_lat;
    mesh_pkt.gps_lon = our_gps_lon;
    mesh_pkt.node_status = 0;
    mesh_pkt.reserved[0] = 0;
    mesh_pkt.reserved[1] = 0;
    mesh_pkt.reserved[2] = 0;

    /* Broadcast over BOTH Ethernet and Serial */
    aura_broadcast_dual(broadcast_mac, ETH_TYPE_MESH,
                        (uint8_t*)&mesh_pkt, sizeof(aura_mesh_packet_t));

    aura_stats.heartbeats_sent++;

    /* Also process the S&F queue */
    snf_process_queue();
}

/* ==========================================================
 * Stats, Mesh Node List
 * ========================================================== */
aura_stats_t* get_aura_stats(void) { return &aura_stats; }
mesh_node_t* get_mesh_nodes(void) { return mesh_nodes; }
int get_mesh_node_count(void) { return mesh_node_count; }

/* ==========================================================
 * SOS Emergency Broadcast
 * ========================================================== */
void aura_send_sos(void) {
    extern uint32_t timer_ticks;

    aura_emergency_packet_t pkt;
    pkt.header[0] = 'S';
    pkt.header[1] = 'O';
    pkt.header[2] = 'S';
    pkt.header[3] = '!';

    mac_copy(pkt.sender_mac, aura_stats.our_mac);
    pkt.gps_lat    = our_gps_lat;
    pkt.gps_lon    = our_gps_lon;
    pkt.timestamp  = timer_ticks;
    pkt.node_status = 2;
    pkt.msg_type   = 1;
    pkt.reserved[0] = 0;
    pkt.reserved[1] = 0;

    /* Broadcast over BOTH Ethernet and Serial (radio bridge) */
    aura_broadcast_dual(broadcast_mac, ETH_TYPE_SOS,
                        (const uint8_t*)&pkt, sizeof(aura_emergency_packet_t));

    aura_stats.frames_sent++;
    write_serial("[AURA-SOS] *** EMERGENCY BROADCAST (Eth+Serial) ***\r\n");
}

/* ==========================================================
 * RESOURCE STATE MACHINE
 * ========================================================== */

/* Helper: zero-pad a resource ID */
static void set_resource_id(uint8_t* dst, const char* src) {
    int i;
    for (i = 0; i < RESOURCE_ID_LEN - 1 && src[i] != '\0'; i++) {
        dst[i] = (uint8_t)src[i];
    }
    while (i < RESOURCE_ID_LEN) {
        dst[i++] = ' ';
    }
}

/* Advertise that we HAVE a resource */
void aura_advertise_resource(const char* resource_id, uint8_t quantity) {
    aura_resource_packet_t pkt;
    pkt.header[0] = 'R';
    pkt.header[1] = 'E';
    pkt.header[2] = 'S';
    pkt.header[3] = '$';
    mac_copy(pkt.sender_mac, aura_stats.our_mac);
    set_resource_id(pkt.resource_id, resource_id);
    pkt.direction = SUPPLY_OFFER;   /* HAS */
    pkt.quantity = quantity;
    pkt.gps_lat = our_gps_lat;
    pkt.gps_lon = our_gps_lon;
    pkt.timestamp = 0;   /* Will be set by handler */
    pkt.ttl = 5;
    pkt.reserved[0] = 0;
    pkt.reserved[1] = 0;

    aura_broadcast_dual(broadcast_mac, ETH_TYPE_RES,
                        (uint8_t*)&pkt, sizeof(aura_resource_packet_t));
    aura_stats.resources_advertised++;

    write_serial("[AURA-RES] Advertising resource: ");
    write_serial(resource_id);
    write_serial("\r\n");
}

/* Advertise that we NEED a resource */
void aura_request_resource(const char* resource_id, uint8_t quantity) {
    aura_resource_packet_t pkt;
    pkt.header[0] = 'R';
    pkt.header[1] = 'E';
    pkt.header[2] = 'S';
    pkt.header[3] = '$';
    mac_copy(pkt.sender_mac, aura_stats.our_mac);
    set_resource_id(pkt.resource_id, resource_id);
    pkt.direction = SUPPLY_REQUEST; /* NEED */
    pkt.quantity = quantity;
    pkt.gps_lat = our_gps_lat;
    pkt.gps_lon = our_gps_lon;
    pkt.timestamp = 0;
    pkt.ttl = 5;
    pkt.reserved[0] = 0;
    pkt.reserved[1] = 0;

    aura_broadcast_dual(broadcast_mac, ETH_TYPE_RES,
                        (uint8_t*)&pkt, sizeof(aura_resource_packet_t));
    aura_stats.resources_advertised++;

    write_serial("[AURA-RES] Requesting resource: ");
    write_serial(resource_id);
    write_serial("\r\n");
}

/* Process an incoming resource packet */
void aura_handle_resource_packet(eth_header_t* frame, uint32_t length) {
    if (length < sizeof(eth_header_t) + sizeof(aura_resource_packet_t)) return;

    aura_resource_packet_t* res = (aura_resource_packet_t*)((uint8_t*)frame + sizeof(eth_header_t));

    /* Validate magic */
    if (res->header[0] != 'R' || res->header[1] != 'E' ||
        res->header[2] != 'S' || res->header[3] != '$') return;

    /* Skip self-originated */
    if (mac_cmp(res->sender_mac, aura_stats.our_mac)) return;

    extern uint32_t timer_ticks;

    /* Try to match this with known resources */
    if (res->direction == SUPPLY_REQUEST) {
        /* Someone NEEDS a resource — check if we've seen an offer */
        for (int i = 0; i < logistics_match_count; i++) {
            if (logistics_matches[i].active &&
                res_id_cmp(logistics_matches[i].resource_id, res->resource_id)) {
                /* We have a match! Create vector */
                if (logistics_match_count < MAX_LOGISTICS_MATCHES) {
                    int slot = logistics_match_count++;
                    logistics_matches[slot].active = 1;
                    logistics_matches[slot].quantity = res->quantity;
                    logistics_matches[slot].timestamp = timer_ticks;
                    mac_copy(logistics_matches[slot].to_mac, res->sender_mac);
                    logistics_matches[slot].to_gps_lat = res->gps_lat;
                    logistics_matches[slot].to_gps_lon = res->gps_lon;
                    set_resource_id(logistics_matches[slot].resource_id, (const char*)res->resource_id);

                    /* from = the node that HAS it (from the previous offer) */
                    mac_copy(logistics_matches[slot].from_mac, logistics_matches[i].from_mac);
                    logistics_matches[slot].from_gps_lat = logistics_matches[i].from_gps_lat;
                    logistics_matches[slot].from_gps_lon = logistics_matches[i].from_gps_lon;

                    aura_stats.resources_matched++;
                    write_serial("[AURA-RES] LOGISTICS MATCH! Resource: ");
                    write_serial((const char*)res->resource_id);
                    write_serial("\r\n");
                }
                break;
            }
        }

        /* Store the request for future matching */
        if (logistics_match_count < MAX_LOGISTICS_MATCHES) {
            int slot = logistics_match_count++;
            logistics_matches[slot].active = 1;
            logistics_matches[slot].quantity = res->quantity;
            logistics_matches[slot].timestamp = timer_ticks;
            mac_copy(logistics_matches[slot].to_mac, res->sender_mac);
            logistics_matches[slot].to_gps_lat = res->gps_lat;
            logistics_matches[slot].to_gps_lon = res->gps_lon;
            set_resource_id(logistics_matches[slot].resource_id, (const char*)res->resource_id);
            /* from will be filled when a matching offer arrives */
        }
    }

    if (res->direction == SUPPLY_OFFER) {
        /* Someone HAS a resource — check if we have a pending request */
        for (int i = 0; i < logistics_match_count; i++) {
            if (logistics_matches[i].active &&
                res_id_cmp(logistics_matches[i].resource_id, res->resource_id)) {
                /* Fill in the FROM field */
                mac_copy(logistics_matches[i].from_mac, res->sender_mac);
                logistics_matches[i].from_gps_lat = res->gps_lat;
                logistics_matches[i].from_gps_lon = res->gps_lon;
                logistics_matches[i].timestamp = timer_ticks;
                break;
            }
        }
    }
}

/* Expose logistics matches for the UI */
logistics_match_t* get_logistics_matches(void) { return logistics_matches; }
int get_logistics_match_count(void) { return logistics_match_count; }

/* ==========================================================
 * STORE & FORWARD (Mesh Relay)
 * ========================================================== */

/* Queue a packet for retransmission */
int snf_queue_packet(const uint8_t* data, int len,
                     const uint8_t* target_mac) {
    extern uint32_t timer_ticks;

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < SNF_QUEUE_SIZE; i++) {
        if (!snf_queue[i].valid) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        /* Queue full — drop oldest entry */
        uint32_t oldest_tick = 0xFFFFFFFF;
        int oldest = 0;
        for (int i = 0; i < SNF_QUEUE_SIZE; i++) {
            if (snf_queue[i].next_retry_tick < oldest_tick) {
                oldest_tick = snf_queue[i].next_retry_tick;
                oldest = i;
            }
        }
        slot = oldest;
        aura_stats.packets_dropped++;
    }

    /* Copy data */
    int copy_len = len;
    if (copy_len > SERIAL_MAX_FRAME) copy_len = SERIAL_MAX_FRAME;
    for (int i = 0; i < copy_len; i++) {
        snf_queue[slot].data[i] = data[i];
    }
    snf_queue[slot].len = copy_len;
    mac_copy(snf_queue[slot].target_mac, target_mac);
    snf_queue[slot].retries = 0;
    snf_queue[slot].max_retries = 3;
    snf_queue[slot].next_retry_tick = timer_ticks + 100; /* Retry in ~1 second */
    snf_queue[slot].valid = 1;

    return 0;
}

/* Process the S&F queue: retransmit packets that need retrying */
void snf_process_queue(void) {
    extern uint32_t timer_ticks;

    for (int i = 0; i < SNF_QUEUE_SIZE; i++) {
        if (!snf_queue[i].valid) continue;
        if (timer_ticks < snf_queue[i].next_retry_tick) continue;

        if (snf_queue[i].retries >= snf_queue[i].max_retries) {
            /* Max retries exceeded — drop */
            snf_queue[i].valid = 0;
            aura_stats.packets_dropped++;
            continue;
        }

        /* Retransmit */
        rtl8139_send_frame(snf_queue[i].data, snf_queue[i].len);
        snf_queue[i].retries++;
        snf_queue[i].next_retry_tick = timer_ticks + 100;
        aura_stats.packets_relayed++;

        write_serial("[AURA-SNF] Relay retry ");
        write_serial_hex(snf_queue[i].retries);
        write_serial("\r\n");
    }
}
