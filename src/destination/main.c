#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"

enum status
{
    DISCONNECTED = 0x0,
    WAITING_OPEN = 0x1,
    WAITING_CLOSE = 0x2,
    ESTABLISHED = 0x3
};
typedef enum status status_t;

struct flux
{
    status_t status;
    uint16_t last_numSeq;
    size_t size;
    char *data;
};
typedef struct flux *flux_t;

struct tcp
{
    flux_t flux[UINT8_MAX]; // list of all fluxes
    uint8_t nb_flux; // current nb of fluxes
    packet_t packet;
    int inSocket;
    int outSocket;
    struct sockaddr_in *sockaddr;
};
typedef struct tcp *tcp_t;

#include "../../headers/global/socket_utils.h" // needs a TCP structure

uint16_t checkPacket(tcp_t tcp, uint8_t idFlux)
{
    // stop and wait, no window is needed, same numSeq
    if(tcp->packet->tailleFenetre == 0)
        return tcp->packet->numSequence;

    // go back n, expect numSeq to be lastNumSeq + 1
        // true -> increment lastNumSeq, new lastNumSeq
        // false -> same lastNumSeq
    if(tcp->packet->numSequence == tcp->flux[idFlux]->last_numSeq + 1)
        tcp->flux[idFlux]->last_numSeq++;
    return tcp->packet->numSequence + 1;
}

void sendACK(tcp_t tcp, int doCheck, uint8_t type, int isCustom)
{
    // idFlux, bit ECN, windowSize => remains the same
    uint8_t idFlux = tcp->packet->idFlux;
    uint8_t ECN = tcp->packet->ECN;
    uint8_t size = tcp->packet->tailleFenetre;

    uint8_t numSeq = tcp->packet->numSequence; /* generally, remain the same */
    if(isCustom) /* unless it's 3 way hand-shake : random numSeq */
    {
        srand(time(NULL));
        numSeq = rand() % (UINT16_MAX / 2);
    }

    uint8_t numAcq = tcp->packet->numSequence + 1; /* unless it's open/close hand-shake */
    if(doCheck) numAcq = checkPacket(tcp, idFlux); /* should always be this, details in function */

    /* sets packet data */
    if(setPacket(tcp->packet, idFlux, type, numSeq, numAcq, ECN, size, "") == -1)
    {
        destroyPacket(tcp->packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }
    /* send packet */
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
    /* flux data size -> size = size + data_size */
    size_t size = tcp->flux[idFlux]->size + PACKET_DATA_SIZE;
    /* reallocs data related to its new size */
    tcp->flux[idFlux]->data = realloc(tcp->flux[idFlux]->data, size);
    /* update data buffer */
    char *str = tcp->flux[idFlux]->data;
    size_t r = snprintf(tcp->flux[idFlux]->data, size, "%s%s", str, data);
    if(r >= size) // error while concat
        destroyTcp(tcp);
}

void handle(tcp_t tcp)
{
    status_t status;

    while(1)
    {
        /* receives a packet */
        if(recvPacket(tcp->packet, tcp->inSocket, 52) == -1)
        {
            /* free packet */
            destroyTcp(tcp);
            raler("recvfrom");
        }

        // destination is a server so it should'nt close, but in this case we use RST since it's never used
        // at least that's what the teacher said, in order to close and free everything
        if(tcp->nb_flux == 0 && tcp->packet->type == RST) // close TCP
            break;

        /* check if the flux exists and get its status */
        if(tcp->flux[tcp->packet->idFlux] != NULL) // already exists, get status
            status = tcp->flux[tcp->packet->idFlux]->status;
        else // doesnt exists yet, DISCONNECTED
            status = DISCONNECTED;

        /* check packet type */

        if(tcp->packet->type == SYN) /* start 3 way hand-shake */
        {
            if(status == ESTABLISHED) /* already connected */
                continue;
            if(status == DISCONNECTED) /* flux doesn't exist yet, needs to be created first */
            {
                tcp->flux[tcp->packet->idFlux] = malloc(PACKET_DATA_SIZE); // flux_t ? alloc a new flux
                tcp->flux[tcp->packet->idFlux]->last_numSeq = tcp->packet->numSequence + 1; // à voir, +1 ?
                tcp->nb_flux++; // increments the total count of fluxes
            }
            // else : want to connect
            /* no lastSeq check ; SYN | ACK ; random numSeq */
            sendACK(tcp, 0, SYN | ACK, 1);
            tcp->flux[tcp->packet->idFlux]->status = WAITING_OPEN; // waiting for ACK from the source to open
            printf("> idFlux %d : WAITING_OPEN\n", tcp->packet->idFlux);
        }
        else if(tcp->packet->type == ACK) /* continue 3 way hand-shake */
        {
            if(status == WAITING_OPEN) /* is waiting to be open, not fully connected yet */
            {
                tcp->flux[tcp->packet->idFlux]->status = ESTABLISHED;
                printf("> idFlux %d : ESTABLISHED\n", tcp->packet->idFlux);
            } else if(status == WAITING_CLOSE)
            {
                tcp->flux[tcp->packet->idFlux]->status = DISCONNECTED;
                free(tcp->flux[tcp->packet->idFlux]);
                tcp->nb_flux--; // decrements the total count of fluxes
                printf("> idFlux %d : DISCONNECTED\n", tcp->packet->idFlux);
            }
        }
        else if(tcp->packet->type == FIN) /* close connection */
        {
            if(status == DISCONNECTED) /* already disconnected */
                continue;

            // SEND ACK
            sendACK(tcp, 0, ACK, 0); /* no lastSeq check ; ACK ; classic numSeq */

            // SEND FIN
            sendACK(tcp, 0, FIN, 1); /* no lastSeq check ; FIN ; random numSeq */

            tcp->flux[tcp->packet->idFlux]->status = WAITING_CLOSE; // switch status : waiting for ACK
            printf("> idFlux %d : WAITING_CLOSE\n", tcp->packet->idFlux);
        }
        else
        {
            if(status != ESTABLISHED) /* not connected */
                continue;

            /* check last numSeq ; classic ACK ; classic numSeq */
            storeData(tcp, tcp->packet->idFlux, tcp->packet->data); // stores data
            sendACK(tcp, 1, ACK, 0);
            printf("> idFlux %d : DATA\n", tcp->packet->idFlux);
        }
    }
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
    tcp->packet = newPacket(); // alloc TCP packet

    handle(tcp); // handle destination

    destroyPacket(tcp->packet); // destroy TCP packet
    destroyTcp(tcp);

    return 0;
}