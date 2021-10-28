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
#include <stdbool.h>

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
    int lastReceivedACK;
    int ackCounter;
    int lastSentSeq;
    packet_t packets[UINT8_MAX];
    char *buf;
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
noreturn int sendTcpSocket(tcp_socket_t socket, mode_t mode, flux_t *fluxes, int fluxCount)
{
    // not connected
    if (socket->status != ESTABLISHED)
        return -1;

    // Nombre de packets qu'il va falloir envoyer
    bool first = true;
    bool timeout;
    ssize_t ret;
    char buf[42];

    do
    {
        for (int i = 0; i < fluxCount; ++i)
        {

            flux_t flux = fluxes[i];
            packet_t receivedPacket = NULL;
            timeout = false;

            if (!first)
            {
                ret = recvfrom(socket->inSocket, &buf, 52, 0, NULL, NULL);

                if (ret < 0) // timeout // Y'a probleme!!!
                {
                    timeout = true;
                } else
                {
                    receivedPacket = malloc(sizeof(struct packet));
                    parsePacket(receivedPacket, buf);

                    if (!(receivedPacket->type & ACK))
                    {
                        destroyPacket(receivedPacket);
                        continue;
                    }

                    // on estime que le flux existe vraiment
                    flux = fluxes[receivedPacket->idFlux];
                }
            }

            // Si bit ECN alors flux recoit fenêtre congestion plus petite
            if (!timeout && receivedPacket->ECN > 0)
            {
                flux->congestionWindowSize = (uint16_t) (flux->congestionWindowSize * 0.9);
            }

            int windowSize = flux->congestionWindowSize / 52;

            if (flux->lastReceivedACK == receivedPacket->numAcquittement)
            {
                flux->ackCounter++;
            } else
            {
                flux->ackCounter = 0;
            }

            if (flux->ackCounter >= 3)
            {
                // Attention 3 ACK recu, probleme!
            }

            if (flux->lastReceivedACK + 1 != receivedPacket->numAcquittement)
            {
                // on veut que l'acquittement soit égal au dernier + 1
            }

            for (int j = 0; j < windowSize; ++j)
            {
                packet_t packet = malloc(sizeof(struct packet));
                setPacket(packet, flux->fluxId, 0, ++flux->lastSentSeq, 0, 0, 0, "");
                sendPacket(socket->outSocket, packet, socket->sockaddr);

                // if()

            }

            if (receivedPacket != NULL)
                destroyPacket(receivedPacket);

        }


        first = false;


    } while (1);


    //
    return 0;
}

// Following function extracts characters present in `src`
// between `m` and `n` (excluding `n`)
char *substr(const char *src, int m, int n)
{
    // get the length of the destination string
    int len = n - m;

    // allocate (len + 1) chars for destination (+1 for extra null character)
    char *dest = (char *) malloc(sizeof(char) * (len + 1));

    // start with m'th char and copy `len` chars into the destination
    strncpy(dest, (src + m), len);

    // return the destination string
    return dest;
}


void processus_fils(tcp_socket_t socket, flux_t flux, int pipefd[2])
{
    // on close le pipe write
    close(pipefd[1]);

    // cb de packets il faut envoyer pour ce flux
    int packetNb = flux->bufLen / 42;
    int currentAckNb = 0;
    int currSeq = 0;
    struct packet receivedPacket;
    bool first = true;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;
    fd_set working_set;
    int ret;
    int ecnCount = 0;
    int timeoutCount = 0;
    int packetsDone = 0;
    int ackCounter = 0;
    uint8_t lastReceivedACK = 0;
    packet_t currentPackets[UINT8_MAX];

    for (int i = 0; i < UINT8_MAX; ++i)
        currentPackets[i] = NULL;

    do
    {
        // Nombre de places dans la fenêtre
        bool new = false;

        if (!first)
        {

            if (ret == 0) // TIMEOUT
            {
                timeoutCount++;
            } else
            {
                if (read(pipefd[0], &receivedPacket, 52) != 52)
                    raler("read pipe");

                // on attend un ack
                if (!(receivedPacket.type & ACK))
                    continue;

                if(receivedPacket.numAcquittement == currentAckNb + 1)
                    currentAckNb++;

                if (lastReceivedACK == receivedPacket.numAcquittement)
                {
                    ackCounter++;
                } else
                {
                    ackCounter = 0;
                    lastReceivedACK = receivedPacket.numAcquittement;
                }

                // Si bit ECN alors flux recoit fenêtre congestion plus petite
                if (receivedPacket.ECN > 0)
                {
                    flux->congestionWindowSize = (uint16_t) (flux->congestionWindowSize * 0.9);
                }


                if (flux->ackCounter >= 3)
                {
                    // Attention 3 ACK recu, probleme!
                }

                if (flux->lastReceivedACK + 1 != receivedPacket->numAcquittement)
                {
                    // on veut que l'acquittement soit égal au dernier + 1
                }


            }

        }

        int windowSize = flux->congestionWindowSize / 52;

        for (int i = currentAckNb; i <= windowSize; ++i)
        {
            packet_t packet = NULL;
            if (currentPackets[i] != NULL)
            {
                packet = currentPackets[i];
            } else
            {
                newPacket(packet);
                setPacket(packet, flux->fluxId, 0, i, 0, 0, 52, substr(flux->buf, i * 42, (i + 1) * 42));
                currentPackets[i] = packet;
            }

            sendPacket(socket->outSocket, packet, socket->sockaddr);
        }

        if (currentAckNb > windowSize)
        {
            // reinitialise tout ici
            flux->congestionWindowSize += 52 * currentAckNb;

            for (int i = 0; i < timeoutCount; ++i)
                flux->congestionWindowSize /= 2;

            for (int i = 0; i < ecnCount; ++i)
                flux->congestionWindowSize = (uint16_t) (flux->congestionWindowSize * 0.9);

            for (int i = 0; i < currentAckNb; ++i)
            {
                destroyPacket(currentPackets[i]);
                currentPackets[i] = NULL;
            }

            currentAckNb = 0;
            timeoutCount = 0;
            ecnCount = 0;
        }


        first = false;

        FD_ZERO(&working_set);
        FD_SET(pipefd[0], &working_set);

        ret = select(pipefd[0] + 1, &working_set, NULL, NULL, &timeout);

        if(ret == -1) raler("select");

    } while (packetsDone < packetNb);


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

    return 0;
}