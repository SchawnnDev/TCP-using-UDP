//
// Created by mfrei on 26/10/2021.
//

#ifndef PROJETALGORESEAUX_SOCKET_UTILS_H
#define PROJETALGORESEAUX_SOCKET_UTILS_H

/**
 * @return socket
 */
int createSocket();

/**
 * @param socket
 */
void closeSocket(int socket);

/**
 * @param socket
 * @param address
 * @param port
 * @return socketAddr
 */
struct sockaddr_in prepareSendSocket(int sock, char *address, int port);

/**
 * @param socket
 * @param port
 * @return socket
 */
int prepareRecvSocket(int sock, int port);

/**
 * @param socket
 * @param packet
 * @param sockaddr
 */
void sendPacket(int sock, packet_t packet, struct sockaddr *sockaddr);

/**
 * @param socket
 * @param size
 * @return packet
 */
packet_t recvPacket(int sock, int size);

/*///////////*/
/* FUNCTIONS */
/*///////////*/

int createSocket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1)
        raler("Create socket");
    return sock;
}

void closeSocket(int sock) {
    if(close(sock) < 0)
        raler("socket");
}

struct sockaddr_in prepareSendSocket(int sock, char *address, int port) {

    struct in_addr addIP;
    if (inet_aton(address, &addIP) == 0) {
        close(sock);
        raler("aton");
    }

    struct sockaddr_in socketAddr;
    memset(&socketAddr, 0, sizeof(socketAddr));
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    socketAddr.sin_addr.s_addr = addIP.s_addr;;

    return socketAddr;
}

int prepareRecvSocket(int sock, int port) {

    struct sockaddr_in socketAddr;
    memset(&socketAddr, 0, sizeof(socketAddr));
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = port;
    socketAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    struct sockaddr *sockaddr = (struct sockaddr*) &socketAddr;

    if (bind(sock, sockaddr, sizeof(socketAddr)) == -1)
        raler("bind");

    return sock; // else, use *
}

void sendPacket(int sock, packet_t packet, struct sockaddr *sockaddr) {
    char* bytes_arr = malloc(packet->tailleFenetre);
    memcpy(bytes_arr, packet, packet->tailleFenetre);

    if (sendto(sock, bytes_arr, packet->tailleFenetre, 0, sockaddr, sizeof(*sockaddr)) == -1) {
        close(sock);
        raler("sendto");
    }

    free(bytes_arr);
}

packet_t recvPacket(int sock, int size) {

    struct sockaddr from;
    socklen_t addrlen = sizeof(from);
    char *buffer = malloc(sizeof(char)*size);

    if (recvfrom(sock, buffer, size, 0, &from, &addrlen) == -1)
        raler("recvfrom");

    packet_t packet = newPacket();
    parsePacket(packet, buffer);
    free(buffer);
    return packet;
}

#endif //PROJETALGORESEAUX_SOCKET_UTILS_H
