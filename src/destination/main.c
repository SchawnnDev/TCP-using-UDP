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

enum status
{
    DISCONNECTED = 0x0,
    WAITING = 0x1,
    ESTABLISHED = 0x2
};
typedef enum status status_t;

struct flux
{
    uint8_t id;
    status_t status;
    uint16_t lastSeq;
    size_t size;
    char *data;
};
typedef struct flux *flux_t;

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

uint16_t checkPacket(tcp_t tcp, uint8_t idFlux)
{
    if(tcp->packet->tailleFenetre == 0)
        return tcp->packet->numSequence;
    if(tcp->packet->numSequence == tcp->flux[idFlux]->lastSeq + 1)
        tcp->flux[idFlux]->lastSeq++;
    return tcp->packet->numSequence + 1;
}

void sendACK(tcp_t tcp, int doCheck, int isDuo, int isCustom)
{
    uint8_t idFlux = tcp->packet->idFlux;
    uint8_t ECN = tcp->packet->ECN;
    uint8_t size = tcp->packet->tailleFenetre;

    uint8_t type = ACK; /* general */
    uint8_t numSeq = tcp->packet->numSequence; /* general*/
    uint8_t numAcq = checkPacket(tcp, idFlux); /* general */

    if(isDuo) type = SYN | ACK; /* only to start connection */
    if(!doCheck) numAcq = tcp->packet->numSequence + 1; /* only open/close connection */
    if(isCustom) /* only to start connection */
    {
        srand(time(NULL));
        numSeq = rand() % (UINT16_MAX / 2);
    }

    if(setPacket(tcp->packet, idFlux, type, numSeq, numAcq, ECN, size, "") == -1)
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

void storeData(tcp_t tcp, uint8_t idFlux, char *data)
{
    size_t size = tcp->flux[idFlux]->size + PACKET_DATA_SIZE;
    tcp->flux[idFlux]->data = realloc(tcp->flux[idFlux]->data, size);
    char *str = tcp->flux[idFlux]->data;
    size_t r = snprintf(tcp->flux[idFlux]->data, size, "%s%s", str, data);
    if(r >= size)
        destroyTcp(tcp);
}

void handle(tcp_t tcp)
{

    status_t status;
    //int first = 1;

    while(1)
    {
        /* receive a packet */
        if(recvPacket(tcp->packet, tcp->inSocket, 52) == -1)
        {
            /* free packet */
            destroyTcp(tcp);
            raler("recvfrom");
        }

        //showPacket(tcp->packet);

        /* check if the flux already exists */
        if(tcp->flux[tcp->packet->idFlux] != NULL)
            status = tcp->flux[tcp->packet->idFlux]->status;
        else
            status = DISCONNECTED;

        /* check type */

        if(tcp->packet->type == SYN) /* start connection */
        {
            if(status == ESTABLISHED) /* already connected */
                continue;
            if(status == DISCONNECTED) /* flux doesn't exist */
            {
                tcp->flux[tcp->packet->idFlux] = malloc(PACKET_DATA_SIZE); // flux_t ?
                tcp->flux[tcp->packet->idFlux]->id = tcp->packet->idFlux;
                tcp->flux[tcp->packet->idFlux]->lastSeq = tcp->packet->numSequence + 1; // à voir, +1 ?
                tcp->nb_flux++;
                // first = 0;
            }

            /* no lastSeq check ; SYN | ACK ; random numSeq */
            sendACK(tcp, 0, 1, 1);
            tcp->flux[tcp->packet->idFlux]->status = WAITING;
            printf("> idFlux %d : WAITING\n", tcp->packet->idFlux);
        }
        else if(tcp->packet->type == ACK) /* do connection */
        {
            if(status == WAITING) /* not fully connected */
                tcp->flux[tcp->packet->idFlux]->status = ESTABLISHED;
            printf("> idFlux %d : ESTABLISHED\n", tcp->packet->idFlux);
        }
        else if(tcp->packet->type == FIN) /* close connection */
        {
            if(status == DISCONNECTED) /* already disconnected */
                continue;

            sendACK(tcp, 0, 0, 0); /* executes normally */
            tcp->flux[tcp->packet->idFlux]->status = DISCONNECTED;
            free(tcp->flux[tcp->packet->idFlux]);
            tcp->nb_flux--;
            printf("> idFlux %d : DISCONNECTED\n", tcp->packet->idFlux);
        }
        else
        {
            if(status != ESTABLISHED) /* not connection */
                continue;

            storeData(tcp, tcp->packet->idFlux, tcp->packet->data);
            sendACK(tcp, 1, 0, 0); /* check last numSeq*/
            printf("> idFlux %d : DATA\n", tcp->packet->idFlux);
        }

        /* end of the connection */
        /*if(tcp->nb_flux == 0 && !first)
            break;*/

    }
    printf("> Close connection\n");
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

    tcp_t tcp = createTcp(ip, port_local, port_medium);
    handle(tcp);
    destroyTcp(tcp);

    return 0;
}