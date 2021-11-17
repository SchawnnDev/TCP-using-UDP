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

#define DEBUG 1

#if defined(DEBUG) && DEBUG > 0
#define DEBUG_PRINT(fmt, args...) printf(fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...) /* Don't do anything in release builds */
#endif

/** @enum status
 *  @brief This enum describes the current status of a flux
 */
enum status
{
    DISCONNECTED = 0x0,         /**< Connection is not established */
    WAITING_OPEN = 0x1,         /**< Connection is about to be open */
    WAITING_CLOSE = 0x2,        /**< Connection is about to be closed */
    ESTABLISHED = 0x3,          /**< Connection is established */
    CLOSED = 0x4                /**< Connection is closed */
};
typedef enum status status_t;

/** @struct flux
 *  @brief This structure stores information about a flux
 */
/** @var status_t::status
 *  Member 'status' contains the current status
 */
/** @var uint16_t::last_numSeq
 *  Member 'last_numSeq' contains the last sequence number received
 */
/** @var  size_t::size
*  Member 'size' contains the size of the current data string
*/
/** @var  char *::data
*  Member 'data' contains the message sent since the beginning of the flux
*/
struct flux
{
    status_t status;
    size_t size;
    char *data;
    uint8_t window;
    uint16_t end;
    uint16_t done;
};
typedef struct flux *flux_t;

/**
 * @fn      uint16_t checkPacket(packet_t packet, flux_t *flux, uint8_t idFlux)
 * @brief   Checks if the numSeq received is the one expected and updates it
 * @param   packet     Packet received
 * @param   *flux      All the fluxes
 * @param   idFlux     Indicates in which flux to search
 * @return  The sequence number
 */
/*uint16_t checkPacket(packet_t packet, flux_t *flux, uint8_t idFlux)
{
    // stop and wait, no window is needed, same numSeq
    if(packet->tailleFenetre == 0)
        return packet->numSequence;

    // go back n, expect numSeq to be lastNumSeq + 1
        // true -> increment lastNumSeq, new lastNumSeq
        // false -> same lastNumSeq
    if(packet->numSequence == flux[idFlux]->last_numSeq + 1)
        flux[idFlux]->last_numSeq++;
    return packet->numSequence;
}*/

/**
 * @fn      void sendACK(tcp_t tcp, packet_t packet, flux_t *flux, int doCheck, uint8_t type, int isCustom)
 * @brief   sending ACK to the source depending on multiple parameters
 * @param   tcp         TCP structure
 * @param   packet      Packet received
 * @param   *flux       All the fluxes
 * @param   doCheck     Boolean if we have to check the sequence number with the last sequence number received (false during the handshakes, else it's true)
 * @param   type        Type of the packet we will be sending
 * @param   isCustom    Boolean if we want to send a random sequence number (true during the handshakes, else it's true)
 * @return  The sequence number
 */
void sendACK(tcp_t tcp, packet_t packet, int ack, uint8_t type, int isCustom)
{
    // idFlux, bit ECN, windowSize => remains the same
    uint8_t idFlux = packet->idFlux;
    uint8_t ECN = packet->ECN;
    uint8_t size = packet->tailleFenetre;
    uint16_t numSeq = packet->numSequence; /* generally, remain the same */
    uint16_t numAcq;
    if(isCustom) /* unless it's 3 way hand-shake : random numSeq */
    {
        srand(time(NULL));
        numSeq = rand() % (UINT16_MAX / 2);
    }

    if(ack == -1) // when original packet wasn't classic data that needs an ACK
        numAcq = (uint16_t) packet->numSequence + 1;
    else
        numAcq = (uint16_t) ack;

    /* sets packet data */
    if(setPacket(packet, idFlux, type, numSeq, numAcq, ECN, size, "") == -1)
    {
        destroyPacket(packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("snprintf");
    }
    /* send packet */
    if(sendPacket(tcp->outSocket, packet, tcp->sockaddr) == -1)
    {
        destroyPacket(packet);
        closeSocket(tcp->outSocket);
        closeSocket(tcp->inSocket);
        raler("sendto");
    }
}

/**
 * @fn      void storeData(tcp_t tcp, flux_t *flux, uint8_t idFlux, char *data)
 * @brief   Get and concat the data received in the flux structure
 * @param   tcp         TCP structure
 * @param   *flux       All the fluxes
 * @param   idFlux      Indicates in which flux to look
 * @param   *data       The data to add to a specified flux
 */
void storeData(tcp_t tcp, flux_t *flux, uint8_t idFlux, char *data)
{
    /* flux data size : size = size + data_size */
    size_t size = flux[idFlux]->size + PACKET_DATA_SIZE;

    /* reallocs data related to its new size */
    flux[idFlux]->data = realloc(flux[idFlux]->data, size);

    /* update data buffer */
    char *str = flux[idFlux]->data;
    size_t r = snprintf(flux[idFlux]->data, size, "%s%s", str, data);
    if(r >= size) // error while concatening
        destroyTcp(tcp);
}

/**
 * @fn      void handle(tcp_t tcp)
 * @brief   Executes the "destination" mechanism
 * @param   tcp         TCP structure
 */
void handle(tcp_t tcp)
{
    status_t status; // flux status
    packet_t packet = newPacket(); // alloc TCP packet
    flux_t *flux = malloc(sizeof(flux_t)*UINT8_MAX);// list of all fluxes
    uint8_t nb_flux = 0; // current nb of fluxes
    //int first = 0;

    while(1)
    {
        /* receives a packet */
        if(recvPacket(packet, tcp->inSocket, 52) == -1)
        {
            destroyPacket(packet);
            destroyTcp(tcp);
            raler("recvfrom");
        }
        DEBUG_PRINT("\n========== Packet received ==========\n");

        // destination is a server so it should'nt close, but in this case we use RST since it's never used
        // at least that's what the teacher said, in order to close and free everything
        if(nb_flux == 0 && packet->type == RST) // close TCP
            break;

        DEBUG_PRINT("Total active fluxes = %d\n", nb_flux);
        DEBUG_PRINT("Current idFlux = %d\n", packet->idFlux);

        /* check if the flux already exists and get its status */
        if(flux[packet->idFlux] != NULL) // already exists, get status
            status = flux[packet->idFlux]->status;
        else // doesnt exists yet, default DISCONNECTED
            status = DISCONNECTED;

        if(status == DISCONNECTED)
            DEBUG_PRINT("Current status = %s\n", "DISCONNECTED");
        else if(status == WAITING_OPEN)
            DEBUG_PRINT("Current status = %s\n", "WAITING_OPEN");
        else if(status == WAITING_CLOSE)
            DEBUG_PRINT("Current status = %s\n", "WAITING_CLOSE");
        else
            DEBUG_PRINT("Current status = %s\n", "ESTABLISHED");

        if(packet->type == ACK)
            DEBUG_PRINT("Current type = %s\n", "ACK");
        else if(packet->type == SYN)
            DEBUG_PRINT("Current type = %s\n", "SYN");
        else if(packet->type == FIN)
            DEBUG_PRINT("Current type = %s\n", "FIN");
        else if(packet->type == RST)
            DEBUG_PRINT("Current type = %s\n", "RST");
        else
            DEBUG_PRINT("Current type = %s\n", "DATA");

        /* check packet type */
        if(packet->type == SYN) /* start 3 way hand-shake */
        {
            if(status == ESTABLISHED) /* already connected */
                continue;

            // else : want to connect

            if(status == DISCONNECTED) /* flux doesn't exist yet, needs to be created first */
            {
                flux[packet->idFlux] = malloc(PACKET_DATA_SIZE); // flux_t ? alloc a new flux
                //flux[packet->idFlux]->last_numSeq = packet->numSequence + 1; // à voir, +1 ?
                nb_flux++; // increments the total count of fluxes
            }

            sendACK(tcp, packet, -1, SYN | ACK, 1);
            flux[packet->idFlux]->done = packet->numSequence;
            flux[packet->idFlux]->status = WAITING_OPEN; // waiting for ACK from the source to open
        }
        else if(packet->type == ACK) /* waiting for ACKs while trying to open/close connection */
        {
            if(status == WAITING_OPEN) /* is waiting to be open, not fully connected yet */
            {
                if(packet->numAcquittement == flux[packet->idFlux]->done + 1)
                    flux[packet->idFlux]->status = ESTABLISHED;
                else // SYN ACK needs to be sent again
                {
                    sendACK(tcp, packet, -1, SYN | ACK, 1);
                    flux[packet->idFlux]->done = packet->numSequence;
                    flux[packet->idFlux]->status = WAITING_OPEN; // waiting for ACK from the source to open
                }
            }
            else if(status == WAITING_CLOSE) /* is waiting to be close, not fully closed yet */
            {
                if(packet->numAcquittement == flux[packet->idFlux]->done + 1)
                {
                    DEBUG_PRINT("Flux %d is done\n", packet->idFlux);
                    DEBUG_PRINT("All data received : %s\n", flux[packet->idFlux]->data);
                    flux[packet->idFlux]->status = DISCONNECTED;
                    free(flux[packet->idFlux]);
                    nb_flux--; // decrements the total count of fluxes
                    status = CLOSED;
                } else // ACK && FIN needs to be sent again
                {
                    // SEND ACK
                    sendACK(tcp, packet, -1, ACK, 0); /* no lastSeq check ; ACK ; classic numSeq */

                    // SEND FIN
                    sendACK(tcp, packet, -1, FIN, 1); /* no lastSeq check ; FIN ; random numSeq */
                    flux[packet->idFlux]->done = packet->numSequence;

                    flux[packet->idFlux]->status = WAITING_CLOSE; // switch status : waiting for ACK
                }
            }
        }
        else if(packet->type == FIN) /* close connection */
        {
            if(status == DISCONNECTED) /* already disconnected */
                continue;

            // else : ESTABLISHED, WAITING_OPEN, WAITING_CLOSE

            // SEND ACK
            sendACK(tcp, packet, -1, ACK, 0); /* no lastSeq check ; ACK ; classic numSeq */

            // SEND FIN
            sendACK(tcp, packet, -1, FIN, 1); /* no lastSeq check ; FIN ; random numSeq */
            flux[packet->idFlux]->done = packet->numSequence;

            flux[packet->idFlux]->status = WAITING_CLOSE; // switch status : waiting for ACK
        }
        else
        {
            if(status == WAITING_CLOSE) // impossible
                continue;

            // source thinks connection is open while it is actually not, restart connection
            if(status == DISCONNECTED || status == WAITING_OPEN)
            {
                sendACK(tcp, packet, -1, SYN | ACK, 1);
                flux[packet->idFlux]->done = packet->numSequence;
                flux[packet->idFlux]->status = WAITING_OPEN; // waiting for ACK from the source to open
            }

            // else : classic packet with data

            if(packet->tailleFenetre == 0) // case : stop and wait, executes normally
            {
                /* check last numSeq ; classic ACK ; classic numSeq */
                storeData(tcp, flux, packet->idFlux, packet->data); // stores data
                sendACK(tcp, packet, packet->numSequence, ACK, 0);
                DEBUG_PRINT("numSeq = %d\n", packet->numSequence);
            }
            else // case : go back n, mutltiple checks
            {
                DEBUG_PRINT("tailleFenetre = %d && window = %d\n", packet->tailleFenetre, flux[packet->idFlux]->window);
                // windows size has changed : we have to start a new sequence
                if(packet->tailleFenetre == 1 || packet->tailleFenetre != flux[packet->idFlux]->window)
                {
                    DEBUG_PRINT("NEW SEQUENCE\n");
                    flux[packet->idFlux]->done = packet->numSequence; // start of sequence
                    flux[packet->idFlux]->end = packet->numSequence + packet->tailleFenetre; // end of sequence
                    flux[packet->idFlux]->window = flux[packet->idFlux]->end - flux[packet->idFlux]->done;
                    //first = 1;
                }
                DEBUG_PRINT("\tWindow = %d | Done = %d ; End = %d\n", flux[packet->idFlux]->window, flux[packet->idFlux]->done, flux[packet->idFlux]->end);
                // read packet and may send ACK if reached the end of sequence or issue is encountered
                if(/*first ||*/ packet->numSequence == flux[packet->idFlux]->done) // is expected packet, no issues
                {
                    //DEBUG_PRINT("\tExpected : first = %d || done = %d and numSeq = %d ; numSeq+1 = %d \n", first, flux[packet->idFlux]->done, packet->numSequence, packet->numSequence + 1);
                    DEBUG_PRINT("\tExpected : done = %d and numSeq = %d ; numSeq+1 = %d \n", flux[packet->idFlux]->done, packet->numSequence, packet->numSequence + 1);

                    /*if(first) // first packet of sequence, done is already incremented at line 311
                        first = 0;
                    else*/
                        flux[packet->idFlux]->done++;

                    //DEBUG_PRINT("\t\tfirst = %d || done = %d \n", first, flux[packet->idFlux]->done);
                    DEBUG_PRINT("\t\tdone = %d \n", flux[packet->idFlux]->done);

                    storeData(tcp, flux, packet->idFlux, packet->data); // not good, should check if data is already stored otherwise might be same data multiple times
                    if(flux[packet->idFlux]->done == flux[packet->idFlux]->end) // end of sequence, sending ACK
                    {
                        sendACK(tcp, packet, flux[packet->idFlux]->done - 1, ACK, 0);
                        DEBUG_PRINT("END OF SEQUENCE | send done = %d\n", flux[packet->idFlux]->done - 1);
                    }
                }
                else // not expected packet, issue
                {
                    DEBUG_PRINT("\t Not Expected : resend done = %d and numSeq = %d\n", flux[packet->idFlux]->done, packet->numSequence);
                    sendACK(tcp, packet, flux[packet->idFlux]->done - 1, ACK, 0);
                }

            }
        }

        if(status == CLOSED)
            DEBUG_PRINT("New status = %s\n", "CLOSED");
        else if(flux[packet->idFlux]->status == DISCONNECTED)
            DEBUG_PRINT("New status = %s\n", "DISCONNECTED");
        else if(flux[packet->idFlux]->status == WAITING_OPEN)
            DEBUG_PRINT("New status = %s\n", "WAITING_OPEN");
        else if(flux[packet->idFlux]->status == WAITING_CLOSE)
            DEBUG_PRINT("New status = %s\n", "WAITING_CLOSE");
        else
            DEBUG_PRINT("New status = %s\n", "ESTABLISHED");
    }

    DEBUG_PRINT("Close connection\n");
    free(flux);
    destroyPacket(packet); // destroy TCP packet
}

/**
 * @fn      int main(int argc, char *argv[])
 * @brief   Initialize and starts everything
 */
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

    DEBUG_PRINT("\nDestination address : %s\nLocal port set at : %d\nDestination port set at : %d\n=================================\n", ip, port_local, port_medium);

    tcp_t tcp = createTcp(ip, port_local, port_medium);
    handle(tcp); // handle destination
    destroyTcp(tcp);

    return 0;
}