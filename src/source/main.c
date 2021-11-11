#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"
#include "../../headers/global/socket_utils.h"

#define TIMEOUT 2000000
#define DEBUG 1

#if defined(DEBUG) && DEBUG > 0
#define DEBUG_PRINT(fmt, args...) printf(fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...) /* Don't do anything in release builds */
#endif

enum connectionStatus {
    DISCONNECTED = 0x0,
    WAITING_SYN_ACK = 0x1,
    ESTABLISHED = 0x2,
    CLOSED = 0x3
};

typedef enum connectionStatus connection_status_t;

enum packetStatus {
    WAIT_ACK = 0,
    SEND_PACKET = 1,
    RESEND_PACKET = 2
};

typedef enum packetStatus packet_status_t;

enum sendMode {
    UNKNOWN = -1, STOP_AND_WAIT = 0, GO_BACK_N = 1
};

typedef enum sendMode send_mode_t;

send_mode_t parseMode(char *mode) {
    if (strcmp(mode, "stop-wait") != 0)
        return STOP_AND_WAIT;
    if (strcmp(mode, "go-back-n") != 0)
        return GO_BACK_N;
    return UNKNOWN; // Default
}

struct tcpSocket {
    int inSocket;
    int outSocket;
    int numSeq;
    struct sockaddr_in *sockaddr;
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
tcp_socket_t connectTcpSocket(char *ip, int localPort, int destinationPort) {
    tcp_socket_t sock = malloc(sizeof(struct tcpSocket));
    sock->inSocket = createSocket();
    sock->outSocket = createSocket();
    sock->status = DISCONNECTED;

    // vers destination
    sock->sockaddr = prepareSendSocket(sock->outSocket, ip, destinationPort);

    // On set un timeout de 500 ms
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT / 2;

    // depuis destination
    if (prepareRecvSocket(sock->inSocket, localPort) < 0 ||
        setsockopt(sock->inSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
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

    do {

        if (sock->status == DISCONNECTED) {
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
        if (!(packet->type & ACK) || !(packet->type & SYN)) {
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

struct flux {
    int fluxId;
    char *buf;
    int bufLen;
    int numSeq;
    connection_status_t status;
    packet_status_t packetStatus;
    packet_t packets[UINT8_MAX];
    int packetsCount;
    int packetsToSend;
};

typedef struct flux *flux_t;

// Fonction pas très safe car aucune verif de taille pour to
void substr(const char *from, char *to, int fromStart, int fromEnd) {
    int j = 0;
    int len = fromEnd - fromStart;

    for (int i = 0; i < len; ++i) {
        if (j >= fromStart && j < fromEnd) {
            to[j++] = from[i];
        } else {
            to[j++] = 0;
        }
    }
}


/**
 *
 * @param socket
 */
void closeTcpSocket(tcp_socket_t socket) {
    // 4 way handshake here
    close(socket->inSocket);
    close(socket->outSocket);
    free(socket);
}

void handleStopWait(flux_t flux, packet_t packet, int outSocket, struct sockaddr_in *sockaddr, bool timeout) {

    // TIMEOUT
    if (flux->packetStatus == WAIT_ACK) {
        if (timeout) {
            // TIMEOUT
            flux->packetStatus = RESEND_PACKET;
        } else {
            // SUCCESS

            // Si l'ACQ passe cette condition alors il est valide
            if (!(packet->type & ACK) || packet->numAcquittement != flux->numSeq + 1) { // on attend un ack
                flux->packetStatus = RESEND_PACKET;
            } else {
                flux->packetStatus = SEND_PACKET;

                // si tous les paquets sont passés, alors on arrête la boucle

            }

        }
    }

    if (flux->packetStatus == SEND_PACKET) {
        flux->numSeq = outSocket == 0 ? -1 : 0;
    }

    // TODO: BUFF?
    setPacket(packet, flux->fluxId, 0, flux->numSeq, 0, 0, 0, "");
    sendPacket(outSocket, packet, sockaddr);
    flux->packetStatus = WAIT_ACK;

}

void processFlux(flux_t flux, packet_t packet, int outSocket, struct sockaddr_in *sockaddr, bool timeout, mode_t mode) {

    /*
     *  GESTION 3 WAY
     */

    if(flux->packetsCount == 0)
        flux->packets[flux->packetsCount++] = newPacket();

    if (flux->status == DISCONNECTED) {

        flux->numSeq = rand() % (UINT16_MAX / 2);
        setPacket(flux->packets[0], flux->fluxId, SYN, flux->numSeq, 0, ECN_DISABLED, 52, "");
        flux->packetsToSend++;
        flux->status = WAITING_SYN_ACK;
        return;
    }

    if (timeout && flux->status == WAITING_SYN_ACK) {
        flux->status = DISCONNECTED;
        return;
    }

    // verification du type packet doit etre ACK & SYN
    if(packet->type & ACK && packet->type & SYN)
    {
        int b = packet->numSequence;
        packet->idFlux = flux->fluxId;
        packet->type = ACK;
        packet->numSequence = flux->numSeq + 1;
        packet->numAcquittement = b + 1;

        sendPacket(outSocket, packet, sockaddr);

        flux->status = ESTABLISHED;
        return;
    } else if(flux->status == WAITING_SYN_ACK) {
        flux->status = DISCONNECTED;
        return;
    }

    // Handle stop and wait
    if(mode == STOP_AND_WAIT)
    {
        handleStopWait(flux, packet, outSocket, sockaddr, timeout);
    }

    // TODO: handle go back n
}

void stopWait(flux_t* fluxes, int fluxCount) {

}


/********************************
 * Main program
 * *******************************/
int main(int argc, char *argv[]) {
    // if : args unvalid

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <mode> <IP_distante> <port_local> <port_ecoute_src_pertubateur>\n", argv[0]);
        exit(1);
    }

    send_mode_t mode = parseMode(argv[1]);

    if (mode == UNKNOWN) {
        fprintf(stderr, "Usage: <mode> must be either 'stop-wait' or 'go-back-n'\n");
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

    srand(time(NULL));

    int fluxCount = 1;
    flux_t fluxes[fluxCount];
    fluxes[0] = malloc(sizeof(struct flux));
    fluxes[0]->buf = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    fluxes[0]->bufLen = 62;
    fluxes[0]->fluxId = 0;
    fluxes[0]->status = DISCONNECTED;
    fluxes[0]->packetsToSend = 0;
    fluxes[0]->packetsCount = 0;

    int inSocket = createSocket();
    int outSocket = createSocket();

    // vers destination
    struct sockaddr_in *sockaddr = prepareSendSocket(outSocket, ip, port_medium);

    // On set un timeout de 500 ms
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT;

    // RANDOM
    srand(time(NULL));

    // depuis destination
    if (prepareRecvSocket(inSocket, port_local) < 0 ||
        setsockopt(inSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        close(inSocket);
        close(outSocket);
        return -1;
    }

    packet_t packet = newPacket();
    size_t ret = 0;
    bool first = true;
    bool loop = true;
    int done = 0;

    do {

        for (int i = 0; i < fluxCount; ++i) {
            flux_t flux = fluxes[i];
            processFlux(flux, packet, outSocket, sockaddr, false, mode);
        }

        loop = true;

        // On envoie les paquets en des flux simultané
        while(loop)
        {
            loop = false;

            for (int i = 0; i < fluxCount; ++i)
            {
                flux_t flux = fluxes[i];

                if(flux->packetsToSend == 0)
                    continue;

                loop = true;
                sendPacket(outSocket, flux->packets[--flux->packetsToSend], sockaddr);
            }

        }

        for (int i = 0; i < fluxCount; ++i)
        {
            ret = recvfrom(inSocket, packet, 52, 0, NULL, NULL);
        }

        // ret = recvfrom(inSocket, packet, 52, 0, NULL, NULL);

        // on vérifie si tous les flux ne sont pas terminés
        for (int i = 0; i < fluxCount; ++i)
        {
            if(fluxes[i]->status == CLOSED)
                continue;
            loop = true;
            break;
        }


    } while (loop);


    return 0;
}