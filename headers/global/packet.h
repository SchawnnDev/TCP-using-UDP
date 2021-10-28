#ifndef _PACKET_H
#define _PACKET_H

#define PACKET_DATA_SIZE 44

uint8_t ACK = 0x10;
uint8_t RST = 0x04;
uint8_t FIN = 0x02;
uint8_t SYN = 0x01;
//uint8_t test = ack | rst | fin;

uint8_t ECN_ACTIVE = 0x01;
uint8_t ECN_DISABLED = 0x00;

/** @struct packet
 *  @brief This structure is a TCP packet
 */
/** @var packet::idFlux
 *  Member 'idFlux' contains the packet's ID
 */
/** @var packet::type
 *  Member 'type' contains the packet's type (ACK, RST, FIN, SYN)
 */
/** @var packet::numSequence
*  Member 'numSequence' contains the packet's sequence number
*/
/** @var packet::numAcquittement
 *  Member 'numAcquittement' contains the packet's acquittal number
 */
/** @var packet::ECN
*  Member 'ECN' contains the packet's ECN bit (true, false)
*/
/** @var packet::tailleFenetre
 *  Member 'tailleFenetre' contains the packet's size
 */
/** @var packet::data
*  Member 'data' contains the packet's data
*/
struct packet
{
    uint8_t idFlux;
    uint8_t type;
    uint16_t numSequence;
    uint16_t numAcquittement;
    uint8_t ECN;
    uint8_t tailleFenetre;
    char data[PACKET_DATA_SIZE];
};

typedef struct packet *packet_t;

/**
 * @fn      packet_t newPacket()
 * @brief   Allocates a packet structure
 * @return  Packet created
 */
packet_t newPacket();

/**
 * @fn      void destroyPacket(packet_t packet)
 * @brief   Destroys packet, frees structure
 * @param   packet  Packet to destroy
 */
void destroyPacket(packet_t packet);

/**
 * @fn      packet_t setPacket(packet_t packet, uint8_t id, uint8_t type,
 *          uint8_t seq, uint8_t acq, uint8_t ECN, uint8_t size, char *data);
 * @brief   Inserts given values into a packet
 * @param   id      packet's ID
 * @param   type    packet's type
 * @param   seq     packet's sequence number
 * @param   acq     packet's acquittal number
 * @param   ECN     packet's ECN bit
 * @param   size    packet's size
 * @param   data    packet's data
 * @return  -1 if an error has occurred, else 0
 */
int setPacket(packet_t packet, uint8_t id, uint8_t type, uint8_t seq,
                   uint8_t acq, uint8_t ECN, uint8_t size, char *data);

/**
 * @fn      void showPacket(packet_t packet)
 * @brief   Displays the values inside a packet
 * @param   packet  Packet to display
 */
void showPacket(packet_t packet);

/**
 * @fn      void parsePacket(packet_t packet, const char *data)
 * @brief   Store the received message into a packet
 * @param   packet  Empty packet
 * @param   data    Message received
 */
void parsePacket(packet_t packet, const char *data);

/*///////////*/
/* FUNCTIONS */
/*///////////*/

packet_t newPacket()
{
    packet_t packet = malloc(sizeof(struct packet));
    if(packet == NULL)
        raler("newPacket");
    return packet;
}

void destroyPacket(packet_t packet)
{
    free(packet);
}

int setPacket(packet_t packet, uint8_t id, uint8_t type,
                      uint8_t seq, uint8_t acq, uint8_t ECN, uint8_t size, char *data) {
    packet->idFlux = id;
    packet->type = type;
    packet->numSequence = seq;
    packet->numAcquittement = acq;
    packet->ECN = ECN;
    packet->tailleFenetre = size;

    int r;
    if((r = snprintf(packet->data, 44, "%s", data)) >= 44 || r < 0)
        return -1;

    return 0;
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

void parsePacket(packet_t packet, const char *data)
{
    packet->idFlux = data[0];
    packet->type = data[1];
    packet->numSequence = data[2] | (uint16_t) data[3] << 8;
    packet->numAcquittement = data[4] | (uint16_t) data[5] << 8;
    packet->ECN = data[6];
    packet->tailleFenetre = data[7];

    for (int i = 8; i < 44; ++i)
        packet->data[i - 8] = data[i];
}

#endif