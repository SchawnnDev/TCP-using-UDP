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

void waitingHandshake (connection_status_t status, int inSocket, int outSocket, struct sockaddr *sockaddr, int numSeq)
{
    packet_t packet = newPacket();

    while(status == DISCONNECTED)
    {
        packet = recvPacket(packet, inSocket, 52);
        if(packet->type == SYN)
            status = WAITING_ACK;
    }

    int numAcq = packet->numSequence + 1;
    packet = setPacket(packet, 0, SYN+ACK, numSeq, numAcq, ECN_DISABLED, 52, "");
    sendPacket(outSocket, packet, sockaddr);

    while(status == WAITING_ACK)
    {
        packet = recvPacket(packet, inSocket, 52);
        if(packet->type == ACK)
            status = ESTABLISHED;
    }

    destroyPacket(packet);
}

/********************************
 * Program
 * *******************************/
void destination(char *address, int port_local, int port_medium)
{

    printf("Address : %s\nLocal : %d\n", address, port_medium);
    printf("---------------\n");
    int inSocket = createSocket();

    inSocket = prepareRecvSocket(inSocket, port_local);
    packet_t packet = newPacket();
    packet = recvPacket(packet, inSocket, 52);
    showPacket(packet);
    destroyPacket(packet);

    closeSocket(inSocket);
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

    int inSocket = createSocket();
    inSocket = prepareRecvSocket(inSocket, port_local);

    int outSocket = createSocket();
    struct sockaddr_in sockAddr = prepareSendSocket(outSocket, ip, port_medium);
    struct sockaddr *sockaddr = (struct sockaddr *) &sockAddr;

    srand(time(NULL));
    int numSeq = rand() % (UINT16_MAX / 2);

    waitingHandshake(connectionStatus, inSocket, outSocket, sockaddr, numSeq);

    return 0;
}
