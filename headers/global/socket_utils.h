#ifndef _SOCKET_UTILS_H
#define _SOCKET_UTILS_H

#include <unistd.h>
#include <arpa/inet.h>

/**
 * @fn      int createSocket()
 * @brief   Creates a socket
 * @return  Socket created, -1 if an error has occurred
 */
int createSocket();

/**
 * @fn      void closeSocket(int socket)
 * @brief   Closes a given socket
 * @param   socket     Socket to close
 */
void closeSocket(int socket);

/**
 * @fn      struct sockaddr_in prepareSendSocket(int socket, char *address, int port)
 * @brief   Sets up a socket that will be used to send packets
 * @param   socket     Socket to prepare
 * @param   address    Adress the socket will be linked to
 * @param   port       Port the socket will be linked to
 * @return  Structure handling the adress
 */
struct sockaddr_in *prepareSendSocket(int socket, char *address, int port);

/**
 * @fn      int prepareRecvSocket(int sock, int port)
 * @brief   Sets up a socket that will be used to receive packets
 * @param   socket     Socket to prepare
 * @param   port       Port the socket will be linked to
 * @return  -1 if an error has occurred, else 0
 */
int prepareRecvSocket(int socket, int port);

/**
 * @fn      void sendPacket(int socket, packet_t packet, struct sockaddr *sockaddr)
 * @brief   Sends a packet using a given socket
 * @param   socket      Socket used to send a packet
 * @param   packet      Packet to be sent
 * @param   sockaddr    Destination address
 * @return  -1 if an error has occurred, else 0
 */
int sendPacket(int socket, packet_t packet, struct sockaddr_in *sockaddr);

/**
 * @fn      packet_t recvPacket(int socket, int size)
 * @brief   Receives a packet using a given socket
 * @param   socket      Socket used to receive a packet
 * @param   size        Max size of the packet
 * @return  -1 if an error has occurred, else 0
 */
int recvPacket(packet_t packet, int socket, int size);

tcp_t createTcp(char *ip, int port_local, int port_medium);

void destroyTcp(tcp_t tcp);

/*///////////*/
/* FUNCTIONS */
/*///////////*/

int createSocket() {
    return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

void closeSocket(int socket) {
    if (close(socket) < 0)
        raler("socket");
}

struct sockaddr_in *prepareSendSocket(int socket, char *address, int port) {
    struct sockaddr_in *sockAddr = malloc(sizeof(struct sockaddr_in));

    if (inet_pton(AF_INET, address, &(sockAddr->sin_addr)) <= 0) {
        closeSocket(socket);
        raler("inet_pton");
    }

    sockAddr->sin_port = htons(port);
    sockAddr->sin_family = AF_INET;

    return sockAddr;
}

int prepareRecvSocket(int socket, int port) {
    struct sockaddr_in socketAddr;
    memset(&socketAddr, 0, sizeof(socketAddr));
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    printf("prepareRcv port: %d, new: %d\n", port, htons(port));
    socketAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    struct sockaddr *sockaddr = (struct sockaddr *) &socketAddr;

    if (bind(socket, sockaddr, sizeof(socketAddr)) == -1)
        return -1;

    return 0;
}

int sendPacket(int socket, packet_t packet, struct sockaddr_in *sockaddr) {
    struct sockaddr *sp = (struct sockaddr *) &(*sockaddr);

    printf("SendTo: Flux thread=%d, go packet, ack=%d, seqNum:%d, type=%s \n", packet->idFlux, packet->numAcquittement, packet->numSequence, packet->type | ACK ? "ACK" : "Other");

    return sendto(socket, packet, 52, 0, sp, sizeof(*sp)) == -1 ? -1 : 0;
}

int recvPacket(packet_t packet, int socket, int size) {
    struct sockaddr from;
    socklen_t addrlen = sizeof(from);

    if (recvfrom(socket, packet, size, 0, &from, &addrlen) == -1)
        return -1;

    printf("RevcPacket: Flux thread=%d, go packet, ack=%d, seqNum:%d, type=%s \n", packet->idFlux, packet->numAcquittement, packet->numSequence, packet->type & ACK ? "ACK" : "Other");

    return 0;
}

tcp_t createTcp(char *ip, int port_local, int port_medium)
{
    // alloc TCP general structure
    tcp_t tcp = malloc(sizeof(struct tcp));
    if(tcp == NULL) raler("malloc tcp");

    // TCP writing socket
    tcp->outSocket = createSocket();
    if(tcp->outSocket == -1)
        raler("create outSocket");

    // TCP adresses
    struct sockaddr_in *sockAddr = prepareSendSocket(tcp->outSocket, ip, port_medium);
    tcp->sockaddr = sockAddr;

    // TCP reading socket
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

    return tcp;
}

void destroyTcp(tcp_t tcp)
{
    closeSocket(tcp->outSocket);
    closeSocket(tcp->inSocket);
    free(tcp);
}

#endif //_SOCKET_UTILS_H
