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

enum connectionStatus
{
    DISCONNECTED = 0x0,
    WAITING_ACK = 0x1,
    ESTABLISHED = 0x2
};
typedef enum connectionStatus connection_status_t;

void waitingHandshake (connection_status_t status, packet_t packet, int inSocket, int outSocket, struct sockaddr *sockaddr, int numSeq)
{

    while(status == DISCONNECTED)
    {
        if(recvPacket(packet, inSocket, 52) == -1)
        {
            destroyPacket(packet);
            closeSocket(outSocket);
            closeSocket(inSocket);
            raler("recvfrom");
        }
        if(packet->type == SYN)
            status = WAITING_ACK;
    }

    int numAcq = packet->numSequence + 1;
    if(setPacket(packet, 0, SYN+ACK, numSeq, numAcq, ECN_DISABLED, 52, "") == -1)
    {
        destroyPacket(packet);
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("snprintf");
    }

    if(sendPacket(outSocket, packet, sockaddr) == -1)
    {
        destroyPacket(packet);
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("sendto");
    }

    while(status == WAITING_ACK)
    {
        if(recvPacket(packet, inSocket, 52) == -1)
        {
            destroyPacket(packet);
            closeSocket(outSocket);
            closeSocket(inSocket);
            raler("recvfrom");
        }
        if(packet->type == ACK)
            status = ESTABLISHED;
    }

    destroyPacket(packet);
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

    connection_status_t connectionStatus = DISCONNECTED;

    int outSocket = createSocket();
    if(outSocket == -1)
        raler("socket");
    struct sockaddr_in sockAddr = prepareSendSocket(outSocket, ip, port_medium);
    struct sockaddr *sockaddr = (struct sockaddr *) &sockAddr;

    int inSocket = createSocket();
    if(inSocket == -1)
    {
        closeSocket(outSocket);
        raler("socket");
    }
    if(prepareRecvSocket(inSocket, port_local) == -1)
    {
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("bind");
    }

    packet_t packet = NULL;
    if(newPacket(packet) == -1)
    {
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("newPacket");
    }

    srand(time(NULL));
    int numSeq = rand() % (UINT16_MAX / 2);

    waitingHandshake(connectionStatus, packet, inSocket, outSocket, sockaddr, numSeq);

    closeSocket(inSocket);
    closeSocket(outSocket);
    destroyPacket(packet);

    return 0;
}
