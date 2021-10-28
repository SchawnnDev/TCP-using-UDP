#include <stdnoreturn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/poll.h>
#include <stdint.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"
#include "../../headers/global/socket_utils.h"

enum connectionStatus
{
    DISCONNECTED = 0x0,
    WAITING_SYN_ACK = 0x1,
    ESTABLISHED = 0x2
};

typedef enum connectionStatus connection_status_t;

enum packetStatus
{
    CAN_SEND_PACKET = 0,
    WAITING_ACK = 1
};

typedef enum packetStatus packet_status_t;

enum sendMode
{
    UNKNOWN = -1, STOP_AND_WAIT = 0, GO_BACK_N = 1
};

typedef enum sendMode send_mode_t;

send_mode_t parseMode(char *mode)
{
    if (strcmp(mode, "stop and wait") != 0)
        return STOP_AND_WAIT;
    if (strcmp(mode, "go-back-n") != 0)
        return GO_BACK_N;
    return UNKNOWN; // Default
}

struct tcpSocket
{
    int inSocket;
    int outSocket;
    int numSeq;
    struct sockaddr *sockaddr;
    connection_status_t status;
};

typedef struct tcpSocket *tcp_socket_t;

/**
 *
 * @param ip
 * @param localPort
 * @param destinationPort
 * @return NULL if error
 */
tcp_socket_t connectTcpSocket(char *ip, int localPort, int destinationPort)
{
    tcp_socket_t sock = malloc(sizeof(struct tcpSocket));
    sock->inSocket = createSocket();
    sock->outSocket = createSocket();
    sock->status = DISCONNECTED;

    // vers destination
    struct sockaddr_in sockaddr = prepareSendSocket(sock->outSocket, ip, destinationPort);
    sock->sockaddr = (struct sockaddr *) &sockaddr; // marche peut-être pas ??

    // On set un timeout de 100 ms
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    // depuis destination
    if (prepareRecvSocket(sock->inSocket, localPort) < 0 ||
        setsockopt(sock->inSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        close(sock->inSocket);
        close(sock->outSocket);
        free(sock);
        return NULL;
    }

    // proceed handshake here

    srand(time(NULL));
    sock->numSeq = rand() % (UINT16_MAX / 2);
    ssize_t ret;

    char data[52];
    packet_t packet = malloc(sizeof(struct packet));

    do
    {

        if (sock->status == DISCONNECTED)
        {
            // envoi du paquet
            setPacket(packet, 0, SYN, sock->numSeq, 0, ECN_DISABLED, 52, "");
            sendPacket(sock->outSocket, packet, sock->sockaddr);
            sock->status = WAITING_SYN_ACK;
            continue;
        }

        ret = recvfrom(sock->inSocket, &data, 52, 0, NULL, NULL);

        if (ret < 0) // timeout
        {
            sock->status = DISCONNECTED;
            continue;
        }

        // Inutile car la fonction est juste executée si disc ou wait : if(status == WAITING_SYN_ACK)
        parsePacket(packet, data);

        // verification du type packet doit etre ACK & SYN
        if (!(packet->type & ACK) || !(packet->type & SYN))
        {
            sock->status = DISCONNECTED;
            continue;
        }

        int b = packet->numSequence;
        packet->type = ACK;
        packet->numSequence = sock->numSeq + 1;
        packet->numAcquittement = b + 1;

        sendPacket(sock->outSocket, packet, sock->sockaddr);

        break;

    } while (1);

    destroyPacket(packet);
    sock->status = ESTABLISHED;

    return sock;
}

struct flux
{
    int fluxId;
    int congestionWindowSize;
    int currentIndex;
    packet_t packets[UINT8_MAX];
    char* buf;
    int bufLen;
};

typedef struct flux *flux_t;

/*
 *
 *     int packetNb = len / 42;

    int fluxCount = 1;
    fluxes[0] = malloc(sizeof(struct flux));
    fluxes[0]->fluxId = 0;
    fluxes[0]->congestionWindowSize = 52;
    fluxes[0]->currentIndex = 0;
 *
 */
noreturn int sendTcpSocket(tcp_socket_t socket, mode_t mode, flux_t* fluxes, int fluxCount)
{
    // not connected
    if (socket->status != ESTABLISHED)
        return -1;

    // Nombre de packets qu'il va falloir envoyer
    int first = 1;
    int ret;
    char buf[42];

    do
    {

        if(!first)
        {

            ret = recvfrom(socket->inSocket, &buf, 52, 0, NULL, NULL);

            if (ret < 0) // timeout
            {
                // Y'a probleme!!!
            }

            // Inutile car la fonction est juste executée si disc ou wait : if(status == WAITING_SYN_ACK)
           // parsePacket(packet, data);


        } else {
            first = 0;
        }

        for (int i = 0; i < fluxCount; ++i)
        {
            flux_t flux = fluxes[i];
            int windowSize = flux->congestionWindowSize / 52;
            int index = flux->currentIndex;
            for (int j = 0; j < windowSize; ++j)
            {
                packet_t packet = malloc(sizeof(struct packet));
                setPacket(packet, flux->fluxId, 0, index++, 0, 0, 0, "");
                sendPacket(socket->outSocket, packet, socket->sockaddr);

                if()

            }

        }


    } while (1);


    //
    return 0;
}

/**
 *
 * @param socket
 */
void closeTcpSocket(tcp_socket_t socket)
{
    // 4 way handshake here
    close(socket->inSocket);
    close(socket->outSocket);
    free(socket);
}

/**
 *  Si le status est disconnected, c'est qu'aucun packet n'a encore été échangé avec le serveur.
 *  Si le status est wait, alors
 *  CLI: A: numSeq = random ; puis envoi syn au serveur
 *  SERVER: B: numSeq = random ; envoi syn au cli + ack => (A + 1)
 *  CLI: syn: numSeq = B ;
 *
 */
connection_status_t
proceedHandshake(connection_status_t status, struct sockaddr *sockaddr, int inSocket, int outSocket, int a)
{
    packet_t packet = NULL;
    if (newPacket(packet) == -1)
    {
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("newPacket");
    }

    if (status == DISCONNECTED)
    {
        if (setPacket(packet, 0, SYN, a, 0, ECN_DISABLED, 52, "") == -1)
        {
            destroyPacket(packet);
            closeSocket(outSocket);
            closeSocket(inSocket);
            raler("snprintf");
        }
        if (sendPacket(outSocket, packet, sockaddr) == -1)
        {
            destroyPacket(packet);
            closeSocket(outSocket);
            closeSocket(inSocket);
            raler("sendto");
        }
        destroyPacket(packet);
        return WAITING_SYN_ACK;
    }
    // Inutile car la fonction est juste executée si disc ou wait : if(status == WAITING_SYN_ACK)
    if (recvPacket(packet, inSocket, 52) == -1)
    {
        destroyPacket(packet);
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("recvfrom");
    }

    int b = packet->numSequence;
    packet->type = ACK;
    packet->numSequence = a + 1;
    packet->numAcquittement = b + 1;

    if (sendPacket(outSocket, packet, sockaddr) == -1)
    {
        destroyPacket(packet);
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("sendto");
    }
    destroyPacket(packet);

    return ESTABLISHED;
}


/********************************
 * Main program
 * *******************************/
int main(int argc, char *argv[])
{
    // if : args unvalid

    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s <mode> <IP_distante> <port_local> <port_ecoute_src_pertubateur>\n", argv[0]);
        exit(1);
    }

    send_mode_t mode = parseMode(argv[1]);

    if (mode == UNKNOWN)
    {
        fprintf(stderr, "Usage: <mode> must be either 'stop and wait' or 'go-back-n'\n");
        exit(1);
    }

    // else

    char *ip = argv[2];
    int port_local = string_to_int(argv[3]);
    int port_medium = string_to_int(argv[4]);

    printf("---------------\n");
    printf("Mode chosen : %d\n", mode);
    printf("Destination address : %s\n", ip);
    printf("Local port set at : %d\n", port_local);
    printf("Destination port set at : %d\n", port_medium);
    printf("---------------\n");

    connection_status_t connectionStatus = DISCONNECTED;
    packet_status_t packetStatus = CAN_SEND_PACKET;

    int outSocket = createSocket();
    if (outSocket == -1)
        raler("socket");
    struct sockaddr_in sockAddr = prepareSendSocket(outSocket, ip, port_medium);
    struct sockaddr *sockaddr = (struct sockaddr *) &sockAddr;

    int inSocket = createSocket();
    if (inSocket == -1)
    {
        closeSocket(outSocket);
        raler("socket");
    }
    if (prepareRecvSocket(inSocket, port_local) == -1)
    {
        closeSocket(outSocket);
        closeSocket(inSocket);
        raler("bind");
    }

    srand(time(NULL));

    int numSeq = rand() % (UINT16_MAX / 2);
    int ret = 0;
    struct pollfd fds[1];
    fds[0].fd = inSocket;
    fds[0].events = POLLIN;

    int timeout = 100; // 100ms



    int congestionWindowSize = 52;
    printf("Pour eviter erreur dans makefile, SIZE : %d\n", congestionWindowSize);


    /**
     * 3. une division de la fenêtre de congestion par deux lorsqu’une perte est détectée par l’expiration d’un timer,
     * 4. un retour de la fenêtre de congestion à 52 octets lors de la détection d’une perte par la réception de 3 acquis dupliqués.
     * 5. une réduction de la fenêtre de congestion de 10% lors de la réception d’un message avec le champ ECN > 0
     */

    do
    {
        // Poll timeout
        if (ret == 0)
        {
            /*
             * S'il y'a timeout, si connectionStatus est waiting syn ack
             * alors on disconnecte, car le packet a peut-être été perdu
             */
            if (connectionStatus == WAITING_SYN_ACK)
            {
                connectionStatus = DISCONNECTED;
            } else if (connectionStatus == ESTABLISHED && packetStatus == WAITING_ACK)
            {
                // S'il y'a timeout ici, et qu'on est dans le gas du go back n,
                // on reduit la taille de la fenêtre à 52.
                if (mode == GO_BACK_N)
                    congestionWindowSize = 52;

                packetStatus = CAN_SEND_PACKET; // On souhaite renvoyer le packet
            }

        } else
        {

            if (connectionStatus != DISCONNECTED)
            {
                // Socket readable
                if (fds[0].revents & POLLIN)
                {

                    if (connectionStatus == WAITING_SYN_ACK)
                    {
                        connectionStatus = proceedHandshake(connectionStatus, sockaddr, inSocket, outSocket, numSeq);
                    } else if (connectionStatus == ESTABLISHED && packetStatus == WAITING_ACK)
                    {

                        // Traiter ici le stop & wait et le go back n

                        if (mode == STOP_AND_WAIT)
                        {
                            // num seq 0 ou 1 ici
                        } else
                        { // GO BACK N (unknown impossible)
                            // changement num seq && augmentation taille
                        }

                        packetStatus = CAN_SEND_PACKET;
                    } else
                    {
                        // WTF ??!?
                    }

                }

            }

        }

        if (connectionStatus == DISCONNECTED || connectionStatus == WAITING_SYN_ACK)
        {
            connectionStatus = proceedHandshake(connectionStatus, sockaddr, inSocket, outSocket, numSeq);
            continue;
        }

        if (connectionStatus == ESTABLISHED)
        {

            if (mode == STOP_AND_WAIT)
            {

            } else
            {

            }

        }

    } while ((ret = poll(fds, 1, timeout) != -1));

    if (ret == -1)
    {
        perror("poll");
        return -1;
    }

    closeSocket(outSocket);
    closeSocket(inSocket);

    return 0;
}