#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"
#include "../../headers/global/socket_utils.h"

#define NB_MAX_FLUX 10

enum status
{
    DISCONNECTED = 0x0,
    WAITING = 0x1,
    ESTABLISHED = 0x2,
    CLOSED = 0x3
};
typedef enum status status_t;

struct flux
{
    uint8_t id;
    status_t status;
    uint8_t nb_packets;
    uint16_t lastSeq;
    size_t size;
    char *data;
};
typedef struct flux *flux_t;

enum sendMode {
    UNKNOWN = -1,
    STOP_AND_WAIT = 0,
    GO_BACK_N = 1
};
typedef enum sendMode send_mode_t;

send_mode_t parseMode(char *mode) {
    if (strcmp(mode, "stop-wait") != 0)
        return STOP_AND_WAIT;
    if (strcmp(mode, "go-back-n") != 0)
        return GO_BACK_N;
    return UNKNOWN; // Default
}

struct tcp
{
    flux_t flux[UINT8_MAX];
    uint8_t nb_flux;
    packet_t packet;
    int inSocket;
    int outSocket;
    struct sockaddr_in *sockaddr;
};
typedef struct tcp *tcp_t;

tcp_t createTcp(char *ip, int port_local, int port_medium)
{
    tcp_t tcp = malloc(sizeof(struct tcp));
    if(tcp == NULL) raler("malloc tcp");

    tcp->packet = newPacket();

    tcp->outSocket = createSocket();
    if(tcp->outSocket == -1)
        raler("create outSocket");

    struct sockaddr_in *sockAddr = prepareSendSocket(tcp->outSocket, ip, port_medium);
    tcp->sockaddr = sockAddr;

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
    destroyPacket(tcp->packet);
    free(tcp);
}

void initFluxes(tcp_t tcp)
{
    for(uint8_t i = 0; i < tcp->nb_flux; i++)
    {
        tcp->flux[i] = malloc(sizeof(flux_t)); // flux_t ?
        /*srand(time(NULL));
        tcp->flux[i]->nb_packets = rand() % (UINT16_MAX / 2);*/
        tcp->flux[i]->nb_packets = 10;
        tcp->flux[i]->status = DISCONNECTED;
        tcp->flux[i]->id = i;
    }
}

void destroyFlux(tcp_t tcp, uint8_t idFlux)
{
    tcp->flux[idFlux]->status = CLOSED;
    // free(tcp->flux[idFlux]->data);
    // free(tcp->flux[idFlux]);
    tcp->nb_flux--;
}

void destroyFluxes(tcp_t tcp)
{
    for(uint8_t i = 0; i < tcp->nb_flux; i++)
    {
        destroyFlux(tcp, i);
    }
}

void startFlux(tcp_t tcp, uint8_t idFlux)
{

    /* SEND SYN */

    srand(time(NULL));
    uint8_t numSeq = rand() % (UINT16_MAX / 2);
    uint8_t numAcq = 0;
    tcp->flux[idFlux]->status = WAITING;

    if(setPacket(tcp->packet, idFlux, SYN, numSeq, numAcq, ECN_DISABLED, 52, "") == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }
    if(sendPacket(tcp->outSocket, tcp->packet, tcp->sockaddr) == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("sendto");
    }

    /* WAITING SYN ACK */

    /* timeout */
    /* renvoyer ou envoyer ack */

    while(tcp->flux[idFlux]->status == WAITING)
    {
        if(recvPacket(tcp->packet, tcp->inSocket, 52) == -1)
        {
            /* free packet */
            destroyTcp(tcp);
            raler("recvfrom");
        }
        if((tcp->packet->idFlux == idFlux) && (tcp->packet->type & ACK) && (tcp->packet->type & SYN))
            tcp->flux[idFlux]->status = ESTABLISHED;
    }

    /* SEND ACK */

    numSeq = tcp->packet->numAcquittement;
    numAcq = tcp->packet->numSequence + 1;

    if(setPacket(tcp->packet, idFlux, ACK, numSeq, numAcq, ECN_DISABLED, 52, "") == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }
    if(sendPacket(tcp->outSocket, tcp->packet, tcp->sockaddr) == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("sendto");
    }
}

void closeFlux(tcp_t tcp, uint8_t idFlux)
{
    /* SEND FIN */

    uint8_t numSeq = tcp->packet->numAcquittement;
    uint8_t numAcq = tcp->packet->numSequence + 1;
    tcp->flux[idFlux]->status = WAITING;

    if(setPacket(tcp->packet, idFlux, FIN, numSeq, numAcq, ECN_DISABLED, 52, "") == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }
    if(sendPacket(tcp->outSocket, tcp->packet, tcp->sockaddr) == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("sendto");
    }

    /* WAITING ACK */
    if(recvPacket(tcp->packet, tcp->inSocket, 52) == -1)
    {
        /* free packet */
        destroyTcp(tcp);
        raler("recvfrom");
    }
    if(tcp->packet->idFlux == idFlux && (tcp->packet->type & ACK))
    {
        tcp->flux[idFlux]->status = CLOSED;
        tcp->nb_flux--;
    }
}

void stopAndWait(tcp_t tcp, uint8_t idFlux)
{
    tcp->flux[idFlux]->nb_packets--;
}

void goBackN(tcp_t tcp, uint8_t idFlux)
{
    tcp->flux[idFlux]->nb_packets--;
}

void handle(tcp_t tcp, send_mode_t mode)
{
    /*srand(time(NULL));
    int flux_count = rand() % (UINT16_MAX / 2);*/
    int flux_count = 3;
    tcp->nb_flux = flux_count;
    initFluxes(tcp);
    int idFlux;

    while(tcp->nb_flux > 0)
    {

        srand(time(NULL));
        idFlux =  rand() % flux_count;

        if(tcp->flux[idFlux]->status == CLOSED) // already closed
            continue;

        //printf("==============================\n");

        if(tcp->flux[idFlux]->status == DISCONNECTED)
        {
            startFlux(tcp, idFlux); // ESTABLISHED
            //printf("idFlux %d is CONNECTED\n", idFlux);
            //printf("==============================\n\n");
            continue;
        }

        // else : status ESTABLISHED

        if(tcp->flux[idFlux]->nb_packets == 0) // nothing more to send
        {
            closeFlux(tcp, idFlux); // DISCONNECTED
            //printf("idFlux %d is DISCONNECTED\n", idFlux);
            //printf("==============================\n\n");
            continue;
        }

        // else : CLASSIC -> send DATA

        if(mode == STOP_AND_WAIT)
            stopAndWait(tcp, idFlux);
        if(mode == GO_BACK_N)
            goBackN(tcp, idFlux);
    }

    destroyFluxes(tcp);
}

/********************************
 * Main program
 * *******************************/
int main(int argc, char *argv[])
{

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

    tcp_t tcp = createTcp(ip, port_local, port_medium);
    handle(tcp, mode);
    destroyTcp(tcp);

    return 0;
}