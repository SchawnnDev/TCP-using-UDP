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

    if (status == DISCONNECTED)
    {
        packet_t packet = createPacket(0, SYN, a, 0, ECN_DISABLED, 52, "");
        sendPacket(outSocket, packet, sockaddr);
        destroyPacket(packet);
        return WAITING_SYN_ACK;
    }
    // Inutile car la fonction est juste executée si disc ou wait : if(status == WAITING_SYN_ACK)
    packet_t parsedPacket = newPacket();
    parsedPacket = recvPacket(parsedPacket, inSocket, 52); // Fixed buffer size no congestion in 3way-handshake

    int b = parsedPacket->numSequence;
    parsedPacket->type = ACK;
    parsedPacket->numSequence = a + 1;
    parsedPacket->numAcquittement = b + 1;

    sendPacket(outSocket, parsedPacket, sockaddr);
    destroyPacket(parsedPacket);

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

    // Test de la source vers le medium
    int outSocket = createSocket();
    struct sockaddr_in sockAddr = prepareSendSocket(outSocket, ip, port_medium);
    struct sockaddr *sockaddr = (struct sockaddr *) &sockAddr;
    packet_t packet = createPacket(0, SYN, 22, 20, ECN_DISABLED, 52, "bite");
    sendPacket(outSocket, packet, sockaddr);
    destroyPacket(packet);
    closeSocket(outSocket);

    if (port_local != 1)
        return 0;

    connection_status_t connectionStatus = DISCONNECTED;
    packet_status_t packetStatus = CAN_SEND_PACKET;
    int inSocket = createSocket();

    srand(time(NULL));

    int numSeq = rand() % (UINT16_MAX / 2);
    int ret = 0;
    int size = 52;
    printf("Pour eviter erreur dans makefile, SIZE : %d\n", size);

    struct pollfd fds[1];
    fds[0].fd = inSocket;
    fds[0].events = POLLIN;

    int timeout = 2 * 1000;

    /**
     *
     * 3. une division de la fenêtre de congestion par deux lorsqu’une perte est détectée par l’expiration d’un timer,
     * 4. un retour de la fenêtre de congestion à 52 octets lors de la détection d’une perte par la réception de 3 acquis dupliqués.
     * 5. une réduction de la fenêtre de congestion de 10% lors de la réception d’un message avec le champ ECN > 0
     *
     *
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
                    size = 52;

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

    return 0;
}