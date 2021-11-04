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
    ESTABLISHED = 0x2
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
    int congestionWindowSize;
    int lastReceivedACK;
    int ackCounter;
    int lastSentSeq;
    packet_t packets[UINT8_MAX];
    char *buf;
    int bufLen;
};

typedef struct flux *flux_t;

noreturn int sendTcpSocket(tcp_socket_t socket, mode_t mode, flux_t *fluxes, int fluxCount) {
    // not connected
    if (socket->status != ESTABLISHED)
        return -1;

    // Nombre de packets qu'il va falloir envoyer
    bool first = true;
    bool timeout;
    ssize_t ret;
    char buf[44];

    do {
        ret = recvfrom(socket->inSocket, &buf, 52, 0, NULL, NULL);

        for (int i = 0; i < fluxCount; ++i) {

            flux_t flux = fluxes[i];
            packet_t receivedPacket = NULL;
            timeout = false;

            if (!first) {


                if (ret < 0) // timeout // Y'a probleme!!!
                {
                    timeout = true;
                } else {
                    receivedPacket = malloc(sizeof(struct packet));
                    parsePacket(receivedPacket, buf);

                    if (!(receivedPacket->type & ACK)) {
                        destroyPacket(receivedPacket);
                        continue;
                    }

                    // on estime que le flux existe vraiment
                    flux = fluxes[receivedPacket->idFlux];
                }
            }

            // Si bit ECN alors flux recoit fenêtre congestion plus petite
            if (!timeout && receivedPacket->ECN > 0) {
                flux->congestionWindowSize = (uint16_t) (flux->congestionWindowSize * 0.9);
            }

            int windowSize = flux->congestionWindowSize / 52;

            if (flux->lastReceivedACK == receivedPacket->numAcquittement) {
                flux->ackCounter++;
            } else {
                flux->ackCounter = 0;
            }

            if (flux->ackCounter >= 3) {
                // Attention 3 ACK recu, probleme!
            }

            if (flux->lastReceivedACK + 1 != receivedPacket->numAcquittement) {
                // on veut que l'acquittement soit égal au dernier + 1
            }

            for (int j = 0; j < windowSize; ++j) {
                packet_t packet = malloc(sizeof(struct packet));
                setPacket(packet, flux->fluxId, 0, ++flux->lastSentSeq, 0, 0, 0, "");
                sendPacket(socket->outSocket, packet, socket->sockaddr);
            }

            if (receivedPacket != NULL)
                destroyPacket(receivedPacket);

        }


        first = false;


    } while (1);


    //
    return 0;
}

enum threadStatus {
    START = 0,
    STOP = 1
};

typedef enum threadStatus thread_status_t;

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

struct args {
    tcp_socket_t socket;
    //flux_t flux;
    int pipe_read;

    int fluxId;
    char *buf;
    int bufLen;
};

struct incomingArgs {
    tcp_socket_t socket;
    int **pipesfd;
    int fluxCount;
    thread_status_t *threadStatus;
};

void *handleGoBackN(void *arg) {
    struct args args = *(struct args *) arg;

}


void *handleStopWait(void *arg) {
    struct args args = *(struct args *) arg;
    packet_t packet = newPacket();
    int packetsNb = args.bufLen / PACKET_DATA_SIZE;
    int packetsDone = 0;
    uint8_t seq = 2;
    packet_status_t packetStatus = SEND_PACKET;
    ssize_t ret = -1;
    fd_set working_set;
    char currentBuff[PACKET_DATA_SIZE];

    // On set un timeout de 500 ms
    struct timeval tv;

    do {

        // TIMEOUT
        if (packetStatus == WAIT_ACK) {
            if (ret == 0) {
                // TIMEOUT
                packetStatus = RESEND_PACKET;
            } else if (ret > 0) {
                // SUCCESS

                DEBUG_PRINT("HSW: ret > 0\n");

                if (read(args.pipe_read, packet, 52) != 52)
                    raler("read pipe");

                DEBUG_PRINT("HSW: Flux thread=%d, go packet, ack=%d, seqNum:%d, type=%s \n", args.fluxId, packet->numAcquittement, packet->numSequence, packet->type & ACK ? "ACK" : "Other");

                // Si l'ACQ passe cette condition alors il est valide
                if (!(packet->type & ACK) || packet->numAcquittement != seq) { // on attend un ack
                    packetStatus = RESEND_PACKET;
                } else {
                    packetsDone++;
                    packetStatus = SEND_PACKET;

                    // si tous les paquets sont passés, alors on arrête la boucle
                    if (packetsDone >= packetsNb)
                        break;

                }

            }
        }

        if (packetStatus == SEND_PACKET) {
            seq = seq == 0 ? -1 : 0;
            int fromEnd = (packetsDone + 1) + PACKET_DATA_SIZE;
            // on vérifie que le dernier buffer n'est pas plus grand que le buffer.
            if (fromEnd > args.bufLen)
                fromEnd = args.bufLen - fromEnd;

            substr(args.buf, currentBuff, packetsDone * PACKET_DATA_SIZE, fromEnd);
        }

        DEBUG_PRINT("HSW: Send packet fluxid=%d, status=%s\n", args.fluxId, packetStatus == SEND_PACKET ? "Send packet" : (packetStatus == RESEND_PACKET ? "Resend packet" : "Wait ACK"));
        setPacket(packet, args.fluxId, 0, seq, 0, 0, 0, currentBuff);
        sendPacket(args.socket->outSocket, packet, args.socket->sockaddr);
        packetStatus = WAIT_ACK;

        FD_ZERO(&working_set);
        FD_SET(args.pipe_read, &working_set);

        DEBUG_PRINT("HSW: Select %d and wait sec=%ld, usec=%ld\n", args.pipe_read, tv.tv_sec, tv.tv_usec);

        // timeout
        tv.tv_sec = 0;
        tv.tv_usec = TIMEOUT;

        ret = select(args.pipe_read + 1, &working_set, NULL, NULL, &tv);

        if (ret == -1) raler("select ici\n");

        DEBUG_PRINT("HSW: Fin boucle\n");

    } while (packetsDone < packetsNb);

    pthread_exit(NULL);
}

void *handleIncomingPackets(void *arg) {
    struct incomingArgs args = *(struct incomingArgs *) arg;
    ssize_t ret;
    packet_t packet = newPacket();

    do {
        ret = recvfrom(args.socket->inSocket, packet, 52, 0, NULL, NULL);
        DEBUG_PRINT("HIP: recvfrom socket=%d\n", args.socket->inSocket);

        if (ret < 0) // timeout
            continue;

        DEBUG_PRINT("HIP: no timeout\n");

        if (!(packet->type & ACK))
            continue;

        if (packet->idFlux >= args.fluxCount)
            continue;

        DEBUG_PRINT("HIP: write to flux: %d, pipe_write=%d\n", packet->idFlux, args.pipesfd[packet->idFlux][1]);

        ret = write(args.pipesfd[packet->idFlux][1], packet, 52);

        if (ret < 0)
            perror("write");

    } while (*args.threadStatus != STOP);

    destroyPacket(packet);

    pthread_exit(NULL);
}


/**
 * Démarrage les threads pour chaque flux.
 * Multi-thread
 * @param socket TCP Socket
 * @param mode Mode de transmission
 * @param fluxes Tableau contenant des flux.
 * @param fluxCount Nb de flux dans le tableau
 */
void sendTo(tcp_socket_t socket, mode_t mode, flux_t *fluxes, int fluxCount) {
    thread_status_t threadStatus = START;
    pthread_t *desc = malloc(sizeof(pthread_t) * (fluxCount + 1));
    struct args *table = malloc(sizeof(struct args) * fluxCount);
    int **pipes = malloc(sizeof(int) * fluxCount);

    struct incomingArgs args;
    args.socket = socket;
    args.fluxCount = fluxCount;
    args.pipesfd = pipes;
    args.threadStatus = &threadStatus;

    // démarrer le thread de gestion
    DEBUG_PRINT("start gestion thread\n");
    pthread_create(&desc[0], NULL, handleIncomingPackets, &args);

    // // // //
    for (int i = 1; i <= fluxCount; ++i) {
        flux_t flux = fluxes[i - 1];
        table[i].socket = socket;
        table[i].fluxId = flux->fluxId;
        table[i].bufLen = flux->bufLen;
        table[i].buf = malloc(flux->bufLen);
        strcpy(table[i].buf, flux->buf);
        // table[i].status = &threadStatus;
        pipes[i - 1] = malloc(sizeof(int) * 2);
        DEBUG_PRINT("sendTo, fluxid=%d\n", flux->fluxId);

        // OPEN PIPE
        if (pipe(pipes[i - 1]) < 0) perror("pipe");

        DEBUG_PRINT("Opened new pipe for flux=%d; read=%d, write=%d\n", i - 1, pipes[i - 1][0], pipes[i - 1][1]);

        table[i].pipe_read = pipes[i - 1][0];

        if (pthread_create(&desc[i], NULL, mode == STOP_AND_WAIT ? handleStopWait : handleGoBackN, &table[i]) > 0) {
            perror("pthread");
        }

        // Create thread communicating with
    }

    // on attend la terminaison de tout les flux
    for (int i = 1; i <= fluxCount; ++i) {
        if (pthread_join(desc[i], NULL) > 0)
            perror("pthread_join");
    }

    threadStatus = STOP;


    // on attend le thread de gestion
    if (pthread_join(desc[0], NULL) > 0)
        perror("pthread_join");

    for (int i = 0; i < fluxCount; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
        free(pipes[i]);
    }

    free(pipes);
    free(table);
    free(desc);

}

void processus_fils(tcp_socket_t socket, flux_t flux, int pipefd[2]) {

    // cb de packets il faut envoyer pour ce flux
    int packetNb = flux->bufLen / PACKET_DATA_SIZE, currentAckNb = 0;
    struct packet receivedPacket;
    bool first = true;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;
    fd_set working_set;
    int ret, ecnCount = 0, timeoutCount = 0, packetsDone = 0, ackCounter = 0;
    int congestionWindowSize = 52;
    uint8_t lastReceivedACK = 0;
    packet_t currentPackets[UINT8_MAX];

    for (int i = 0; i < UINT8_MAX; ++i)
        currentPackets[i] = NULL;

    do {
        // Nombre de places dans la fenêtre

        if (!first) {

            if (ret == 0) // TIMEOUT
            {
                timeoutCount++;
            } else {
                if (read(pipefd[0], &receivedPacket, 52) != 52)
                    raler("read pipe");

                // on attend un ack
                if (!(receivedPacket.type & ACK))
                    continue;

                // Si l'ACQ passe cette condition alors il est valide
                if (receivedPacket.numAcquittement == currentAckNb + 1)
                    currentAckNb++;

                // Checker s'il n'y a pas 3 fois les mêmes ACK
                if (lastReceivedACK == receivedPacket.numAcquittement) {
                    ackCounter++;

                    if (ackCounter >= 3) {
                        // Attention 3 ACK recu, probleme!
                        congestionWindowSize = 52;

                        // On reset tout !

                        for (int i = 0; i < currentAckNb; ++i) {
                            destroyPacket(currentPackets[i]);
                            currentPackets[i] = NULL;
                        }

                        packetsDone += currentAckNb;

                        currentAckNb = 0;
                        timeoutCount = 0;
                        ecnCount = 0;
                    }

                } else {
                    ackCounter = 0;
                    lastReceivedACK = receivedPacket.numAcquittement;
                }

                // Si bit ECN alors flux recoit fenêtre congestion plus petite
                if (receivedPacket.ECN > 0) {
                    ecnCount++;
                }

            }

        }

        int windowSize = congestionWindowSize / 52;

        if (currentAckNb >= windowSize) {
            // reinitialise tout ici
            congestionWindowSize += 52 * currentAckNb;

            for (int i = 0; i < timeoutCount; ++i)
                congestionWindowSize /= 2;

            for (int i = 0; i < ecnCount; ++i)
                congestionWindowSize = (uint16_t) (flux->congestionWindowSize * 0.9);

            for (int i = 0; i < currentAckNb; ++i) {
                destroyPacket(currentPackets[i]);
                currentPackets[i] = NULL;
            }

            currentAckNb = 0;
            timeoutCount = 0;
            ecnCount = 0;
        } else {
            for (int i = currentAckNb; i <= windowSize; ++i) {
                packet_t packet = NULL;
                if (currentPackets[i] != NULL) {
                    packet = currentPackets[i];
                } else {
                    newPacket(packet);
                    //      setPacket(packet, flux->fluxId, 0, i, 0, 0, 52, substr(flux->buf, (packetsDone + i) * 44, (packetsDone + i + 1) * 44));
                    currentPackets[i] = packet;
                }

                sendPacket(socket->outSocket, packet, socket->sockaddr);
            }
        }

        first = false;

        FD_ZERO(&working_set);
        FD_SET(pipefd[0], &working_set);

        ret = select(pipefd[0] + 1, &working_set, NULL, NULL, &timeout);

        if (ret == -1) raler("select");

    } while (packetsDone < packetNb);


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

    tcp_socket_t sock = connectTcpSocket(ip, port_local, port_medium);
    flux_t fluxes[1];
    fluxes[0] = malloc(sizeof(struct flux));
    fluxes[0]->buf = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    fluxes[0]->bufLen = 62;
    fluxes[0]->fluxId = 0;
    sendTo(sock, STOP_AND_WAIT, fluxes, 1);

    return 0;
}