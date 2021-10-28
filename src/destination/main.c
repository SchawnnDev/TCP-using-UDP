#include <stdnoreturn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"
#include "../../headers/global/socket_utils.h"

enum status
{
    DISCONNECTED = 0x0,
    WAITING = 0x1,
    ESTABLISHED = 0x2
};
typedef enum status status_t;

struct tcp
{
    packet_t packet;
    int inSocket;
    int outSocket;
    struct sockaddr *sockaddr;
    status_t status;
    uint8_t flux[UINT8_MAX];
};
typedef struct tcp *tcp_t;

tcp_t createTcp(char *ip, int port_local, int port_medium)
{
    tcp_t tcp = malloc(sizeof(struct tcp));
    tcp->status = DISCONNECTED;
    tcp->packet = newPacket();

    tcp->outSocket = createSocket();
    if(tcp->outSocket == -1)
        raler("create outSocket");

    struct sockaddr_in sockAddr = prepareSendSocket(tcp->outSocket, ip, port_medium);
    tcp->sockaddr = (struct sockaddr *) &(sockAddr);

    tcp->inSocket = createSocket();
    if(tcp->inSocket == -1)
    {
        closeSocket(tcp->inSocket);
        raler("create inSocket");
    }
    if(prepareRecvSocket(tcp->inSocket, port_local) == -1)
    {
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("prepareRecvSocket");
    }
}

void destroyTcp(tcp_t tcp)
{
    closeSocket(tcp->outSocket);
    closeSocket(tcp->inSocket);
    destroyPacket(tcp->packet);
    free(tcp);
}

void handleConnection (packet_t packet, tcp_t tcp, uint8_t type, status_t start, status_t end)
{
    // Status : DISCONNECTED (open) or ESTABLISHED (close)
    while(tcp->status == start)
    {
        if(recvPacket(packet, tcp->inSocket, 52) == -1)
        {
            destroyPacket(packet);
            closeSocket(tcp->outSocket);
            closeSocket(tcp->inSocket);
            raler("recvfrom");
        }
        // Type : SYN (open) or FIN (close)
        if(packet->type == type)
            tcp->status = WAITING;
    }

    uint8_t idFlux = packet->idFlux;
    uint8_t numAcq = packet->numSequence + 1;
    srand(time(NULL));
    uint8_t numSeq = rand() % (UINT16_MAX / 2);
    if(setPacket(packet, idFlux, SYN+ACK, numSeq, numAcq, ECN_DISABLED, 52, "") == -1)
    {
        destroyPacket(packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }

    if(sendPacket(tcp->outSocket, packet, tcp->sockaddr) == -1)
    {
        destroyPacket(packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("sendto");
    }

    while(tcp->status == WAITING)
    {
        if(recvPacket(packet, tcp->inSocket, 52) == -1)
        {
            destroyPacket(packet);
            closeSocket(tcp->outSocket);
            closeSocket(tcp->inSocket);
            raler("recvfrom");
        }
        // Status : ESTABLISHED (open) or DISCONNECTED (close)
        if(packet->type == ACK)
            tcp->status = end;
    }
}

uint8_t checkPacket(tcp_t tcp, uint8_t idFlux)
{
    uint8_t last_ack = tcp->flux[idFlux];
    if(tcp->packet->numAcquittement == last_ack + 1)
        tcp->flux[idFlux]++;
    return tcp->flux[idFlux];
}

void sendACK(tcp_t tcp)
{

    uint8_t idFlux = tcp->packet->idFlux;
    uint8_t numAcq = checkPacket(tcp, idFlux);
    uint8_t ECN = tcp->packet->ECN;
    uint8_t size = tcp->packet->tailleFenetre;

    if(setPacket(tcp->packet, idFlux, ACK, numAcq, numAcq, ECN, size, "") == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }

    if(sendPacket(tcp->outSocket, tcp->packet, tcp->sockaddr) == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("sendto");
    }
}

/********************************
 * Main program
 * *******************************/
int main(int argc, char *argv[])
{

    // if : args unvalid

    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <IP_distante> <port_local> <port_ecoute_dst_pertubateur>\n", argv[0]);
        exit(1);
    }

    // else

    char *ip = argv[1];
    int port_local = string_to_int(argv[2]);
    int port_medium = string_to_int(argv[3]);

    printf("---------------\n");
    printf("Destination address : %s\n", ip);
    printf("Local port set at : %d\n", port_local);
    printf("Destination port set at : %d\n", port_medium);
    printf("---------------\n");

    tcp_t tcp = createTcp(ip, port_local, port_medium);

    // open connection
    handleConnection(tcp->packet, tcp, SYN, DISCONNECTED, ESTABLISHED);

    // handlePackets
    while(1)
    {
        if(recvPacket(tcp->packet, tcp->inSocket, 52) == -1)
        {
            destroyPacket(tcp->packet);
            closeSocket(tcp->outSocket);
            closeSocket(tcp->inSocket);
            raler("recvfrom");
        }
        if(tcp->packet->type == FIN) // end
            break;
        sendACK(tcp);
    }

    // close connection
    handleConnection(tcp->packet, tcp, FIN, ESTABLISHED, DISCONNECTED);
    destroyTcp(tcp);

    return 0;
}
