#ifndef AURA_NET_H
#define AURA_NET_H

#include <stdint.h>
#include "serial.h"

/* -----------------------------------------------
 * Aura-Net: Bare-Metal Networking Stack (Phase 7.3)
 *
 * A minimal network stack that sends and receives
 * raw Ethernet frames. Implements:
 *   1. ARP (Address Resolution Protocol) for IP->MAC
 *   2. Heartbeat packets for mesh discovery
 *   3. Network statistics for the telemetry dashboard
 *   4. COM1 Serial Radio Bridge (LoRa/Ham radio)
 *   5. Resource State Machine (Logistics Router)
 *   6. Store & Forward (Mesh Packet Relay)
 *
 * "Disaster Relief Protocol":
 *   In a disaster scenario with no router, each node
 *   broadcasts a heartbeat Ethernet frame every N seconds.
 *   Other nodes receive these heartbeats and build
 *   an ad-hoc network map (mesh without infrastructure).
 *
 *   With the COM1 serial bridge, the mesh extends to
 *   LoRa/Ham radio antennas for 10-mile offline broadcasts.
 * ----------------------------------------------- */

/* Ethernet type codes */
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_HEART  0x9000  /* Custom: Aura-Net heartbeat */
#define ETH_TYPE_MESH   0x9002  /* Custom: Aura-Net mesh discovery */
#define ETH_TYPE_SOS    0x9003  /* Custom: Aura-Net emergency distress */
#define ETH_TYPE_RES    0x9004  /* Custom: Aura-Net resource logistics */

/* ARP opcodes */
#define ARP_REQUEST     1
#define ARP_REPLY       2

/* Heartbeat interval: 5 seconds = 500 PIT ticks at 100 Hz */
#define HEARTBEAT_INTERVAL 500

/* Maximum ARP table entries */
#define ARP_TABLE_SIZE 16
#define MAX_MESH_NODES 16

/* ---- Store & Forward packet queue ---- */
#define SNF_QUEUE_SIZE  64   /* Maximum queued packets */

/* ==========================================================
 * DATA STRUCTURES
 * ========================================================== */

/* Aura-Net Mesh Radar packet (packed for wire transmission) */
typedef struct {
    uint8_t  header[4];     /* 'A','U','R','A' magic */
    uint8_t  sender_mac[6]; /* Source MAC address */
    int32_t  gps_lat;       /* Mock GPS latitude (degrees * 100) */
    int32_t  gps_lon;       /* Mock GPS longitude (degrees * 100) */
    uint8_t  node_status;   /* 0=Online, 1=Warning, 2=Critical */
    uint8_t  reserved[3];   /* Padding */
} __attribute__((packed)) aura_mesh_packet_t;

/* Mesh node entry for the live node list UI */
typedef struct {
    uint8_t  mac[6];        /* Node MAC address */
    int32_t  gps_lat;       /* Last known latitude */
    int32_t  gps_lon;       /* Last known longitude */
    uint8_t  status;        /* 0=Online, 1=Warning, 2=Critical */
    uint32_t last_seen;     /* Timer tick when last received */
    uint8_t  valid;         /* 1 = active entry */
} mesh_node_t;

/* Aura-Net Emergency / Distress Signal packet (packed for wire) */
typedef struct {
    uint8_t  header[4];     /* 'S','O','S','!' — visual marker */
    uint8_t  sender_mac[6]; /* Source MAC address */
    int32_t  gps_lat;       /* Latitude (degrees * 100) */
    int32_t  gps_lon;       /* Longitude (degrees * 100) */
    uint32_t timestamp;     /* Timer ticks at send time */
    uint8_t  node_status;   /* 0=OK, 1=Warning, 2=Critical */
    uint8_t  msg_type;      /* 0=heartbeat, 1=SOS distress */
    uint8_t  reserved[2];   /* Padding to 24 bytes total */
} __attribute__((packed)) aura_emergency_packet_t;

/* ==========================================================
 * Resource State Machine (Logistics Router)
 *
 * Extensible generic resource type using an 8-byte
 * human-readable resource ID (e.g. "WATER   ",
 * "MEDS    ", "FUEL    ", "SHELTER ", "FOOD    ").
 *
 * Supply types are fixed 8-byte ASCII identifiers,
 * zero-padded on the right.
 * ========================================================== */

/* Max length of a resource type string (including padding) */
#define RESOURCE_ID_LEN     8

/* Supply request/offer directions */
#define SUPPLY_REQUEST      0   /* Node NEEDS this resource */
#define SUPPLY_OFFER        1   /* Node HAS this resource */

/* Resource logistics packet (packed for wire) */
typedef struct {
    uint8_t  header[4];          /* 'R','E','S','$' magic */
    uint8_t  sender_mac[6];      /* Source MAC */
    uint8_t  resource_id[RESOURCE_ID_LEN];  /* e.g. "WATER   " */
    uint8_t  direction;          /* 0=NEED (request), 1=HAS (offer) */
    uint8_t  quantity;           /* Amount available/requested (0-255) */
    int32_t  gps_lat;            /* GPS of the node */
    int32_t  gps_lon;
    uint32_t timestamp;          /* Ticks at send time */
    uint8_t  ttl;                /* Time-to-live for forwarding */
    uint8_t  reserved[2];        /* Padding */
} __attribute__((packed)) aura_resource_packet_t;

/* A tracked logistics resource match */
typedef struct {
    uint8_t  resource_id[RESOURCE_ID_LEN];  /* Resource type */
    uint8_t  from_mac[6];                   /* Node that HAS the resource */
    uint8_t  to_mac[6];                     /* Node that NEEDS the resource */
    int32_t  from_gps_lat;
    int32_t  from_gps_lon;
    int32_t  to_gps_lat;
    int32_t  to_gps_lon;
    uint32_t timestamp;                     /* When match was made */
    uint8_t  quantity;
    uint8_t  active;                        /* 1 = active match */
} logistics_match_t;

/* Maximum tracked logistics matches */
#define MAX_LOGISTICS_MATCHES 32

/* ==========================================================
 * Store & Forward (Mesh Relay)
 *
 * When a packet arrives destined for a node that is not
 * directly reachable, we queue it and rebroadcast during
 * the next heartbeat interval. This turns every node into
 * a physical relay tower.
 * ========================================================== */

/* S&F queue entry */
typedef struct {
    uint8_t  data[SERIAL_MAX_FRAME];   /* Full raw frame data */
    int      len;                      /* Length of stored data */
    uint8_t  target_mac[6];            /* Destination MAC */
    uint8_t  retries;                  /* Retry count */
    uint8_t  max_retries;              /* Max retries before drop */
    uint32_t next_retry_tick;          /* Tick when to retry */
    uint8_t  valid;                    /* 1 = active entry */
} snf_queue_entry_t;

/* ==========================================================
 * ARP & Ethernet
 * ========================================================== */

/* Incoming signal detection — set by aura_handle_frame, read by desktop loop */
extern volatile uint8_t  incoming_signal_pending;
extern uint8_t  incoming_sender_mac[6];
extern int32_t  incoming_gps_lat;
extern int32_t  incoming_gps_lon;
extern uint32_t incoming_timestamp;

/* ARP table entry */
typedef struct {
    uint32_t ip_addr;
    uint8_t  mac_addr[6];
    uint8_t  valid;
} arp_entry_t;

/* Ethernet header */
typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ether_type;
} __attribute__((packed)) eth_header_t;

/* ARP packet (inside Ethernet payload) */
typedef struct {
    uint16_t hw_type;       /* 1 = Ethernet */
    uint16_t proto_type;    /* 0x0800 = IPv4 */
    uint8_t  hw_len;        /* 6 */
    uint8_t  proto_len;     /* 4 */
    uint16_t opcode;        /* 1 = request, 2 = reply */
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed)) arp_packet_t;

/* Aura-Net statistics for telemetry */
typedef struct {
    volatile uint32_t frames_sent;
    volatile uint32_t frames_received;
    volatile uint32_t arp_resolved;
    volatile uint32_t heartbeats_sent;
    volatile uint32_t heartbeats_received;
    uint8_t  our_mac[6];
    uint32_t our_ip;
    /* Serial bridge stats */
    volatile uint32_t serial_frames_sent;
    volatile uint32_t serial_frames_received;
    /* Store & forward stats */
    volatile uint32_t packets_relayed;
    volatile uint32_t packets_dropped;
    /* Resource logistics stats */
    volatile uint32_t resources_matched;
    volatile uint32_t resources_advertised;
} aura_stats_t;

extern aura_stats_t aura_stats;

/* ==========================================================
 * FUNCTION DECLARATIONS
 * ========================================================== */

/* Initialize Aura-Net */
void init_aura_net(void);

/* Send a raw Ethernet frame */
int aura_send_frame(const uint8_t* dst_mac, uint16_t ether_type,
                    const uint8_t* payload, uint32_t payload_len);

/* Send an ARP request for a given IP */
void aura_arp_request(uint32_t ip);

/* Handle an incoming frame (called from poll loop) */
void aura_handle_frame(eth_header_t* frame, uint32_t length);

/* Update heartbeat (called periodically from desktop loop) */
void aura_update_heartbeat(void);

/* Get Aura-Net stats */
aura_stats_t* get_aura_stats(void);

/* Broadcast an SOS/disaster-relief Ethernet packet */
void aura_send_sos(void);

/* Get mesh node list and count */
mesh_node_t* get_mesh_nodes(void);
int get_mesh_node_count(void);

/* ==========================================================
 * Serial Radio Bridge Functions
 * ========================================================== */

/* Send a packet over both Ethernet AND serial (radio bridge) */
void aura_broadcast_dual(const uint8_t* dst_mac, uint16_t ether_type,
                         const uint8_t* payload, uint32_t payload_len);

/* Poll for incoming serial frames and process them */
void aura_poll_serial(void);

/* ==========================================================
 * Resource State Machine Functions
 * ========================================================== */

/* Advertise that we HAVE a resource (broadcast to mesh) */
void aura_advertise_resource(const char* resource_id, uint8_t quantity);

/* Advertise that we NEED a resource (broadcast to mesh) */
void aura_request_resource(const char* resource_id, uint8_t quantity);

/* Process an incoming resource packet — match NEED with HAS */
void aura_handle_resource_packet(eth_header_t* frame, uint32_t length);

/* Get the list of logistics matches for the UI */
logistics_match_t* get_logistics_matches(void);
int get_logistics_match_count(void);

/* ==========================================================
 * Store & Forward Functions
 * ========================================================== */

/* Queue a packet for retransmission (store & forward) */
int snf_queue_packet(const uint8_t* data, int len,
                     const uint8_t* target_mac);

/* Process the S&F queue: retransmit any pending packets */
void snf_process_queue(void);

#endif /* AURA_NET_H */
