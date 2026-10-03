#include "net.h"
#include <stddef.h>

#define RTL8139_ID 0x8139
#define RX_RING_SIZE 8192u
#define RX_BUFFER_SIZE (RX_RING_SIZE + 16u + 1536u)
#define TX_BUFFER_SIZE 1536u
#define TX_TIMEOUT 1000000u
#define PING_TIMEOUT 1200000u

#define RTL_IDR0 0x00
#define RTL_TSD0 0x10
#define RTL_TSAD0 0x20
#define RTL_RBSTART 0x30
#define RTL_CAPR 0x38
#define RTL_CBR 0x3A
#define RTL_IMR 0x3C
#define RTL_ISR 0x3E
#define RTL_TCR 0x40
#define RTL_RCR 0x44
#define RTL_CONFIG1 0x52
#define RTL_CR 0x37

#define CR_RESET 0x10u
#define CR_RX_ENABLE 0x08u
#define CR_TX_ENABLE 0x04u
#define CR_RX_EMPTY 0x01u

#define ISR_RX_OK 0x0001u
#define ISR_TX_OK 0x0004u
#define ISR_RX_ERR 0x0002u
#define ISR_TX_ERR 0x0008u

#define ETH_ARP 0x0806u
#define ETH_IPV4 0x0800u
#define ARP_REQUEST 1u
#define ARP_REPLY 2u
#define IP_PROTO_ICMP 1u
#define IP_PROTO_UDP 17u

#ifdef UNIT_TEST
static uint32_t io_ports[65536];
static inline void io_out8(uint16_t port, uint8_t value) {
    io_ports[port] = (io_ports[port] & 0xFFFFFF00u) | value;
}
static inline uint8_t io_in8(uint16_t port) {
    return (uint8_t)io_ports[port];
}
static inline void io_out16(uint16_t port, uint16_t value) {
    io_ports[port] = (io_ports[port] & 0xFFFF0000u) | value;
}
static inline uint16_t io_in16(uint16_t port) {
    return (uint16_t)io_ports[port];
}
static inline void io_out32(uint16_t port, uint32_t value) {
    io_ports[port] = value;
}
static inline uint32_t io_in32(uint16_t port) {
    return io_ports[port];
}
#else
static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void io_out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t io_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void io_out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint32_t io_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
#endif

static int ready;
static uint16_t io_base;
static uint16_t pci_vendor;
static uint16_t pci_device;
static uint8_t local_mac[6];
static uint32_t local_ip = 0x0A00020Fu;
static uint32_t local_gateway = 0x0A000202u;
static uint32_t local_netmask = 0xFFFFFF00u;
static uint8_t rx_buffer[RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffer[TX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint32_t tx_packets;
static uint32_t rx_packets;
static uint32_t ping_success;
static uint32_t ping_fail;
static uint16_t ip_id = 1;

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u |
        ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) | (offset & 0xFCu);
    io_out32(0xCF8, address);
    return io_in32(0xCFC);
}

static int find_rtl8139(void) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_read32((uint8_t)bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);
                if (vendor == 0xFFFFu || device != RTL8139_ID) continue;
                uint32_t bar0 = pci_read32((uint8_t)bus, slot, func, 0x10);
                if (!(bar0 & 1u)) continue;
                io_base = (uint16_t)(bar0 & 0xFFFCu);
                pci_vendor = vendor;
                pci_device = device;
                uint32_t command = pci_read32((uint8_t)bus, slot, func, 0x04);
                command |= 0x00000005u;
                uint32_t address = 0x80000000u |
                    ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | 0x04u;
                io_out32(0xCF8, address);
                io_out32(0xCFC, command);
                return 1;
            }
        }
    }
    return 0;
}

static uint16_t swap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t checksum16(const uint8_t* data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2)
        sum += ((uint32_t)data[i] << 8) | data[i + 1];
    if (length & 1u) sum += (uint32_t)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static void put16be(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint16_t get16be(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put32be(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t get32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int ip_same_subnet(uint32_t a, uint32_t b) {
    return (a & local_netmask) == (b & local_netmask);
}

static int rtl_reset(void) {
    io_out8((uint16_t)(io_base + RTL_CR), CR_RESET);
    for (uint32_t i = 0; i < TX_TIMEOUT; ++i)
        if (!(io_in8((uint16_t)(io_base + RTL_CR)) & CR_RESET)) return 1;
    return 0;
}

static int rtl_init(void) {
    if (!rtl_reset()) return 0;
    io_out8((uint16_t)(io_base + RTL_CONFIG1), 0);
    io_out32((uint16_t)(io_base + RTL_RBSTART), (uint32_t)(uintptr_t)rx_buffer);
    io_out8((uint16_t)(io_base + RTL_IMR), 0);
    io_out16((uint16_t)(io_base + RTL_CAPR), 0xFFF0u);
    io_out32((uint16_t)(io_base + RTL_RCR), 0x0000000Fu | 0x00000080u);
    io_out32((uint16_t)(io_base + RTL_TCR), 0x00000000u);
    for (int i = 0; i < 6; ++i) local_mac[i] = io_in8((uint16_t)(io_base + RTL_IDR0 + i));
    io_out8((uint16_t)(io_base + RTL_CR), CR_RX_ENABLE | CR_TX_ENABLE);
    io_out16((uint16_t)(io_base + RTL_ISR), 0xFFFFu);
    return 1;
}

static int rtl_send(const uint8_t* frame, uint16_t length) {
    if (!ready || !frame || length == 0 || length > TX_BUFFER_SIZE) return 0;
    for (uint16_t i = 0; i < length; ++i) tx_buffer[i] = frame[i];
    io_out32((uint16_t)(io_base + RTL_TSAD0), (uint32_t)(uintptr_t)tx_buffer);
    io_out32((uint16_t)(io_base + RTL_TSD0), length);
    for (uint32_t i = 0; i < TX_TIMEOUT; ++i) {
        uint32_t status = io_in32((uint16_t)(io_base + RTL_TSD0));
        if (status & 0x2000u) {
            if (status & 0x8000u) {
                ++tx_packets;
                return 1;
            }
            if (status & 0x40000000u) return 0;
            if (status & 0x80000000u) return 0;
        }
    }
    return 0;
}

static uint8_t rx_byte(uint32_t offset) {
    return rx_buffer[offset % RX_RING_SIZE];
}

static uint16_t rx_word(uint32_t offset) {
    return (uint16_t)rx_byte(offset) | ((uint16_t)rx_byte(offset + 1u) << 8);
}

static int rtl_receive(uint8_t* out, uint16_t cap) {
    if (!ready || !out || cap == 0) return 0;
    uint16_t isr = io_in16((uint16_t)(io_base + RTL_ISR));
    if (isr) io_out16((uint16_t)(io_base + RTL_ISR), isr);
    if (io_in8((uint16_t)(io_base + RTL_CR)) & CR_RX_EMPTY) return 0;

    uint16_t capr = io_in16((uint16_t)(io_base + RTL_CAPR));
    uint16_t offset = (uint16_t)(capr + 16u);
    uint16_t status = rx_word(offset);
    uint16_t length = rx_word(offset + 2u);
    if (!(status & 0x0001u) || length < 4u || length > 1536u) return 0;
    uint16_t payload = (uint16_t)(length - 4u);
    if (payload > cap) payload = cap;
    for (uint16_t i = 0; i < payload; ++i) out[i] = rx_byte((uint32_t)offset + 4u + i);
    uint16_t next = (uint16_t)(((uint32_t)offset + 4u + length + 3u) & ~3u);
    io_out16((uint16_t)(io_base + RTL_CAPR), (uint16_t)(next - 16u));
    ++rx_packets;
    return payload;
}

static void build_ethernet(uint8_t* p, const uint8_t* dest, uint16_t type) {
    for (int i = 0; i < 6; ++i) p[i] = dest[i];
    for (int i = 0; i < 6; ++i) p[6 + i] = local_mac[i];
    put16be(p + 12, type);
}

static int send_arp_request(uint32_t target) {
    static const uint8_t broadcast[6] = {255,255,255,255,255,255};
    uint8_t frame[42];
    build_ethernet(frame, broadcast, ETH_ARP);
    put16be(frame + 14, 1);
    put16be(frame + 16, ETH_IPV4);
    frame[18] = 6; frame[19] = 4;
    put16be(frame + 20, ARP_REQUEST);
    for (int i = 0; i < 6; ++i) frame[22 + i] = local_mac[i];
    put32be(frame + 28, local_ip);
    for (int i = 0; i < 6; ++i) frame[32 + i] = 0;
    put32be(frame + 38, target);
    return rtl_send(frame, sizeof(frame));
}

static int resolve_arp(uint32_t ip, uint8_t mac[6]) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        send_arp_request(ip);
        for (uint32_t loops = 0; loops < PING_TIMEOUT / 6u; ++loops) {
            uint8_t frame[1600];
            int length = rtl_receive(frame, sizeof(frame));
            if (length < 42) continue;
            if (get16be(frame + 12) != ETH_ARP) continue;
            if (get16be(frame + 20) != ARP_REPLY) continue;
            if (get32be(frame + 28) != ip) continue;
            for (int i = 0; i < 6; ++i) mac[i] = frame[22 + i];
            return 1;
        }
    }
    return 0;
}

static int build_ipv4_icmp(uint8_t* frame, const uint8_t dest_mac[6],
                           uint32_t dest_ip, uint16_t sequence) {
    static const uint8_t icmp_payload[8] = { 'M','Y','O','S','P','I','N','G' };
    uint8_t* ip = frame + 14;
    uint8_t* icmp = frame + 34;
    build_ethernet(frame, dest_mac, ETH_IPV4);
    ip[0] = 0x45;
    ip[1] = 0;
    put16be(ip + 2, 28 + sizeof(icmp_payload));
    put16be(ip + 4, ip_id++);
    put16be(ip + 6, 0x4000u);
    ip[8] = 64;
    ip[9] = IP_PROTO_ICMP;
    put16be(ip + 10, 0);
    put32be(ip + 12, local_ip);
    put32be(ip + 16, dest_ip);
    put16be(ip + 10, checksum16(ip, 20));
    icmp[0] = 8; icmp[1] = 0;
    put16be(icmp + 2, 0);
    put16be(icmp + 4, 0x4D59u);
    put16be(icmp + 6, sequence);
    for (int i = 0; i < (int)sizeof(icmp_payload); ++i) icmp[8 + i] = icmp_payload[i];
    put16be(icmp + 2, checksum16(icmp, 16));
    return 14 + 20 + 16;
}

int net_parse_ipv4(const char* text, uint32_t* out) {
    if (!text || !out) return 0;
    uint32_t parts[4] = {0,0,0,0};
    int part = 0;
    int have = 0;
    for (const char* p = text; ; ++p) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            if (parts[part] > 25u) return 0;
            parts[part] = parts[part] * 10u + (uint32_t)(c - '0');
            if (parts[part] > 255u) return 0;
            have = 1;
        } else if (c == '.' && have) {
            if (part >= 3) return 0;
            ++part;
            have = 0;
        } else if (c == 0 && have) {
            break;
        } else {
            return 0;
        }
    }
    if (part != 3) return 0;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 1;
}

int net_ping_ipv4(uint32_t destination) {
    if (!ready) { ++ping_fail; return 0; }
    uint32_t next_hop = ip_same_subnet(destination, local_ip) ? destination : local_gateway;
    uint8_t dest_mac[6];
    if (!resolve_arp(next_hop, dest_mac)) { ++ping_fail; return 0; }

    uint8_t frame[128];
    uint16_t sequence = (uint16_t)(ping_success + ping_fail + 1u);
    int length = build_ipv4_icmp(frame, dest_mac, destination, sequence);
    if (!rtl_send(frame, (uint16_t)length)) { ++ping_fail; return 0; }

    for (uint32_t loops = 0; loops < PING_TIMEOUT; ++loops) {
        uint8_t rx[1600];
        int n = rtl_receive(rx, sizeof(rx));
        if (n < 42 || get16be(rx + 12) != ETH_IPV4) continue;
        if ((rx[14] >> 4) != 4 || (rx[14] & 0x0Fu) < 5) continue;
        uint8_t ihl = (uint8_t)((rx[14] & 0x0Fu) * 4u);
        if (n < (int)(14 + ihl + 8)) continue;
        if (rx[14 + 9] != IP_PROTO_ICMP) continue;
        if (get32be(rx + 14 + 12) != destination) continue;
        uint8_t* icmp = rx + 14 + ihl;
        if (icmp[0] != 0 || get16be(icmp + 6) != sequence) continue;
        ++ping_success;
        return 1;
    }
    ++ping_fail;
    return 0;
}

int net_udp_send(uint32_t destination, uint16_t source_port, uint16_t destination_port,
                 const uint8_t* payload, uint16_t length) {
    if (!ready || !payload || length > 1024u) return 0;
    uint32_t next_hop = ip_same_subnet(destination, local_ip) ? destination : local_gateway;
    uint8_t dest_mac[6];
    if (!resolve_arp(next_hop, dest_mac)) return 0;

    uint8_t frame[14 + 20 + 8 + 1024];
    build_ethernet(frame, dest_mac, ETH_IPV4);
    uint8_t* ip = frame + 14;
    uint8_t* udp = frame + 34;
    put16be(udp + 0, source_port);
    put16be(udp + 2, destination_port);
    put16be(udp + 4, (uint16_t)(8u + length));
    put16be(udp + 6, 0);
    for (uint16_t i = 0; i < length; ++i) udp[8 + i] = payload[i];
    uint16_t ip_len = (uint16_t)(20u + 8u + length);
    ip[0] = 0x45; ip[1] = 0;
    put16be(ip + 2, ip_len);
    put16be(ip + 4, ip_id++);
    put16be(ip + 6, 0x4000u);
    ip[8] = 64; ip[9] = IP_PROTO_UDP;
    put16be(ip + 10, 0);
    put32be(ip + 12, local_ip); put32be(ip + 16, destination);
    put16be(ip + 10, checksum16(ip, 20));
    return rtl_send(frame, (uint16_t)(14u + ip_len));
}

void net_get_status(net_status_t* out) {
    if (!out) return;
    out->available = ready;
    out->link_up = ready;
    for (int i = 0; i < 6; ++i) out->mac[i] = local_mac[i];
    out->ip = local_ip;
    out->gateway = local_gateway;
    out->netmask = local_netmask;
    out->vendor = pci_vendor;
    out->device = pci_device;
    out->tx_packets = tx_packets;
    out->rx_packets = rx_packets;
    out->ping_success = ping_success;
    out->ping_fail = ping_fail;
}

void net_init(void) {
    ready = 0;
    io_base = 0;
    pci_vendor = 0;
    pci_device = 0;
    tx_packets = rx_packets = 0;
    ping_success = ping_fail = 0;
    if (!find_rtl8139()) return;
    ready = rtl_init();
    if (!ready) return;
}
