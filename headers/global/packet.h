#ifndef _PACKET_H
#define _PACKET_H

uint8_t ACK = 0x10;
uint8_t RST = 0x04;
uint8_t FIN = 0x02;
uint8_t SYN = 0x01;
//uint8_t test = ack | rst | fin;

uint8_t ECN_ACTIVE = 0x01;
uint8_t ECN_DISABLED = 0x00;

struct packet {
    uint8_t idFlux;
    uint8_t type;
    uint16_t numSequence;
    uint16_t numAcquittement;
    uint8_t ECN;
    uint8_t tailleFenetre;
    char *data;
};

typedef struct packet *packet_t;

/**
 *
 * @param id
 * @param type
 * @param seq
 * @param acq
 * @param ECN
 * @param size
 * @param data
 * @return
 */
packet_t createPacket(uint8_t id, uint8_t type, uint8_t seq, uint8_t acq, uint8_t ECN, uint8_t size, char *data);

/**
 *
 * @param socket
 * @param packet
 * @param sockaddr
 */
void sendPacket(int socket, packet_t packet, struct sockaddr *sockaddr);

/**
 *
 * @param packet
 */
void showPacket(packet_t packet);

/**
 *
 * @param packet
 * @param data
 * @return
 */
int parsePacket(packet_t packet, const char *data);

packet_t createPacket(uint8_t id, uint8_t type,
                      uint8_t seq, uint8_t acq, uint8_t ECN, uint8_t size, char *data) {
    packet_t packet = malloc(sizeof(struct packet));
    packet->idFlux = id;
    packet->type = type;
    packet->numSequence = 22;
    packet->numAcquittement = 20;
    packet->ECN = ECN;
    packet->tailleFenetre = size;
    packet->data = malloc(size - 8);
    packet->data = data;
    return packet;
}

void showPacket(packet_t packet) {
    printf("Packet idFlux : %d\n", packet->idFlux);
    printf("Packet type : %d\n", packet->type);
    printf("Packet numSequence : %d\n", packet->numSequence);
    printf("Packet numAcquittement : %d\n", packet->numAcquittement);
    printf("Packet ECN : %d\n", packet->ECN);
    printf("Packet tailleFenetre : %d\n", packet->tailleFenetre);
    printf("Packet data : %s\n", packet->data);
}

int parsePacket(packet_t packet, const char *data) {
    packet->idFlux = data[0];
    packet->type = data[1];
    packet->numSequence = data[2] | (uint16_t) data[3] << 8;
    packet->numAcquittement = data[4] | (uint16_t) data[5] << 8;
    packet->ECN = data[6];
    packet->tailleFenetre = data[7];
    packet->data = malloc(packet->tailleFenetre - 8);

    // Les données peuvent varier en fonction de la taille de la fenêtre

    for (int i = 8; i < packet->tailleFenetre; ++i)
        packet->data[i - 8] = data[i];
}

#endif