#ifndef MYOS_NET_H
#define MYOS_NET_H

#include <stdint.h>

typedef struct {
    int available;
    int link_up;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t gateway;
    uint32_t netmask;
    uint16_t vendor;
    uint16_t device;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t ping_success;
    uint32_t ping_fail;
} net_status_t;

void net_init(void);
int net_available(void);
int net_parse_ipv4(const char* text, uint32_t* out);
int net_ping_ipv4(uint32_t destination);
void net_get_status(net_status_t* out);
int net_udp_send(uint32_t destination, uint16_t source_port, uint16_t destination_port,
                 const uint8_t* payload, uint16_t length);

#endif
