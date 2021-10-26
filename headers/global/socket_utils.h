#ifndef _SOCKET_UTILS_H
#define _SOCKET_UTILS_H

/**
 * @fn      int createSocket()
 * @brief   Creates a socket
 * @return  Socket created
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
struct sockaddr_in prepareSendSocket(int socket, char *address, int port);

/**
 * @fn      int prepareRecvSocket(int sock, int port)
 * @brief   Sets up a socket that will be used to receive packets
 * @param   socket     Socket to prepare
 * @param   port       Port the socket will be linked to
 * @return  Socket prepared
 */
int prepareRecvSocket(int socket, int port);

/**
 * @fn      void sendPacket(int socket, packet_t packet, struct sockaddr *sockaddr)
 * @brief   Sends a packet using a given socket
 * @param   socket      Socket used to send a packet
 * @param   packet      Packet to be sent
 * @param   sockaddr    Destination address
 */
void sendPacket(int socket, packet_t packet, struct sockaddr *sockaddr);

/**
 * @fn      packet_t recvPacket(int socket, int size)
 * @brief   Receives a packet using a given socket
 * @param   socket      Socket used to receive a packet
 * @param   size        Max size of the packet
 * @return  Packet received
 */
packet_t recvPacket(int socket, int size);

/*///////////*/
/* FUNCTIONS */
/*///////////*/

int createSocket() {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == -1)
        raler("Create socket");
    return s;
}

void closeSocket(int socket) {
    if(close(socket) < 0)
        raler("socket");
}

struct sockaddr_in prepareSendSocket(int socket, char *address, int port) {

    struct in_addr addIP;
    if (inet_aton(address, &addIP) == 0) {
        close(socket);
        raler("aton");
    }

    struct sockaddr_in socketAddr;
    memset(&socketAddr, 0, sizeof(socketAddr));
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    socketAddr.sin_addr.s_addr = addIP.s_addr;

    return socketAddr;
}

int prepareRecvSocket(int socket, int port) {

    struct sockaddr_in socketAddr;
    memset(&socketAddr, 0, sizeof(socketAddr));
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = port;
    socketAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    struct sockaddr *sockaddr = (struct sockaddr*) &socketAddr;

    if (bind(socket, sockaddr, sizeof(socketAddr)) == -1) {
        close(socket);
        raler("bind");
    }

    return socket; // else, use *
}

void sendPacket(int socket, packet_t packet, struct sockaddr *sockaddr) {
    char* bytes_arr = malloc(packet->tailleFenetre);
    memcpy(bytes_arr, packet, packet->tailleFenetre);

    if (sendto(socket, bytes_arr, packet->tailleFenetre, 0, sockaddr, sizeof(*sockaddr)) == -1) {
        close(socket);
        raler("sendto");
    }

    free(bytes_arr);
}

packet_t recvPacket(int socket, int size) {

    struct sockaddr from;
    socklen_t addrlen = sizeof(from);
    char *buffer = malloc(sizeof(char)*size);

    if (recvfrom(socket, buffer, size, 0, &from, &addrlen) == -1) {
        closeSocket(socket);
        raler("recvfrom");
    }

    packet_t packet = newPacket();
    parsePacket(packet, buffer);
    free(buffer);

    return packet;
}

#endif //_SOCKET_UTILS_H
