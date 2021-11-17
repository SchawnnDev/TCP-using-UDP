#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"
#include "../../headers/global/socket_utils.h" // needs a TCP structure

#define TIMEOUT 50000
#define DEBUG 1
#define FLUX_NB 1

#if defined(DEBUG) && DEBUG > 0
#define DEBUG_PRINT(fmt, args...) printf(fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...) /* Don't do anything in release builds */
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))

/** @enum flux_status
 *  @brief This enum describes the current status of a flux
 */
enum flux_status
{
    DISCONNECTED = 0x0,         /**< Connection is not established */
    WAITING_SYN_ACK = 0x1,      /**< Connection is about to be open */
    WAITING_ACK = 0x2,          /**< Packets send, waiting for ACKs */
    ESTABLISHED = 0x3,          /**< Connection is established */
    TERM_SEND_FIN = 0x4,        /**< Starting to close the connection */
    TERM_WAIT_ACK = 0x5,        /**< Waiting for ACK after FIN */
    TERM_WAIT_FIN = 0x6,        /**< Waiting for FIN */
    TERM_WAIT_TERM = 0x7        /**< Connection is closed */
};
typedef enum flux_status flux_status_t;

/** @enum packet_status
 *  @brief This enum describes the current status of a packet
 */
enum packet_status
{
    WAIT_ACK = 0,       /**< a packet has been sent, waiting for the ACK */
    SEND_PACKET = 1,    /**< correct ACK received, sending new packet */
    RESEND_PACKET = 2   /**< issue, sending the same packet again */
};
typedef enum packet_status packet_status_t;

/** @enum thread_status
 *  @brief This enum describes the current status of the main thread, the manager
 */
enum thread_status
{
    START = 0,          /**< the manager is runing */
    STOP = 1            /**< the manager is not running */
};
typedef enum thread_status thread_status_t;

/** @enum modeTCP
 *  @brief This enum describes the mechanism chosen by the user
 */
enum modeTCP
{
    UNKNOWN = -1,           /**< Mechanism doesn't exists */
    STOP_AND_WAIT = 0,      /**< Stop and wait */
    GO_BACK_N = 1           /**< Go-bach-n */
};
typedef enum modeTCP modeTCP_t; // mode_t already used

/**
 * @fn      modeTCP_t parseMode(char *mode)
 * @brief   Checks if the mode actually exists and sends the corresponding enum back
 * @param   mode     Argument chosen by the user at the beginning of the program
 * @return  Returns the recognized mechanism
 */
modeTCP_t parseMode(char *mode)
{
    if (strcmp(mode, "stop-wait") == 0)
        return STOP_AND_WAIT;
    if (strcmp(mode, "go-back-n") == 0)
        return GO_BACK_N;
    return UNKNOWN; // Default
}

/** @struct flux
 *  @brief This structure helps us to create the main flux structures
 */
/** @var  int::fluxId
*  Member 'fluxId' identifies a flux
*/
/** @var  flux_status_t::status
*  Member 'status' contains the current status of a flux
*/
/** @var  char *::buf
*  Member 'buf' contains the string we want to send
*/
/** @var  int::bufLen
*  Member 'bufLen' indicates the size of the buffer
*/
struct flux {
    int fluxId;
    flux_status_t status;
    char *buf;
    int bufLen;
};
typedef struct flux *flux_t;

/** @struct flux_args
 *  @brief This structure stores information about a specific flux
 */
/** @var tcp_t::tcp
 *  Member 'tcp' contains the tcp structure in order to communicate
 */
/** @var  int::idFlux
*  Member 'idFlux' identifies a flux
*/
/** @var int::pipe_read
 *  Member 'pipes' is used to receive packets from the manager
 */
/** @var  char *::buf
*  Member 'buf' contains the string we want to send
*/
/** @var  int::bufLen
*  Member 'bufLen' indicates the size of the buffer
*/
struct flux_args {
    tcp_t tcp;
    int idFlux;
    int pipe_read;
    char *buf;
    int bufLen;
};

/** @struct manager
 *  @brief This structure stores information about the main thread, the manager
 */
/** @var tcp_t::tcp
 *  Member 'tcp' contains the tcp structure in order to communicate
 */
/** @var int *::pipes
 *  Member 'pipes' is used to transfer a received packet to the corresponding flux
 */
/** @var  int::nb_flux
*  Member 'nb_flux' contains the number of active fluxes
*/
/** @var  thread_status_t *::thr_status
*  Member 'thr_status' contains its current status
*/
struct manager
{
    tcp_t tcp;
    int *pipes;
    int nb_flux;
    thread_status_t *thr_status;
};

/**
 * @fn      void *doStopWait(void *arg)
 * @brief   Receives packets from the manager trough pipes, treat them  and sends a sequence of packets back (go-back-n mechanism)
 * @param   arg         Argument send when the thread was created, struct flux in this case
 */
void *doGoBackN(void *arg)
{

    // creates flux
    struct flux_args flux = *(struct flux_args *) arg;
    flux_status_t status = DISCONNECTED; // flux status by default

    // creates a packet
    packet_t packet = newPacket();

    // variables
    uint16_t numSeq = 0; // numSeq by default
    uint8_t sliding_window = 1; // size of the window
    ssize_t return_value = -1; // error return

    // set counters
    int nb_packets = (flux.bufLen - 1) / PACKET_DATA_SIZE + 1; // nb packets to send
    int nb_done_packets = 0; // nb packets already sent
    int nb_lost_packet = 0; // times lost the same packet
    /*int *sending_window = malloc(sizeof(int)*nb_packets);*/

    // set timeout : 500 ms
    struct timeval tv;

    // others
    fd_set working_set; // fd_set used for select
    char data[PACKET_DATA_SIZE]; // current data to send

    do
    {

        if (status == WAITING_ACK) // waiting for all the ACKs of the previous sequence
        {
            //DEBUG_PRINT("%d ===== WAITING_ACK =====\n", flux.idFlux);

            // read packet received from manager (trough pipe)
            if (read(flux.pipe_read, packet, 52) != 52)
                raler("read pipe");
            if(flux.idFlux == 0)
                DEBUG_PRINT("%d ===== Read packet ===== numSeq %d ; numAck %d\n", flux.idFlux, packet->numSequence, packet->numAcquittement);

            // receiving the type ACK|SYN here means the ACK we sent has been lost
            if (packet->type & ACK && packet->type & SYN) // ACK|SYN
            {
                status = WAITING_SYN_ACK; // we need to send a new ACK
                numSeq = 0;
                sliding_window = 1;
                return_value = -1;
                nb_done_packets = 0;
                nb_lost_packet = 0;
                //DEBUG_PRINT("%d ---> ACK|SYN Restart handshake : WAITING_ACK to WAITING_SYN_ACK\n", flux.idFlux);
            }
            else // it cannot be anything else other than an ACK here
            {

                // get the previous sliding window value
                if (numSeq >= (nb_done_packets + sliding_window)) // only true for the first ACK of a sequence
                    sliding_window = packet->tailleFenetre;

                if(flux.idFlux == 0)
                    DEBUG_PRINT("\t ===== READ ACK %d ===== Window = %d & done = %d | ACK = %d\n", flux.idFlux, sliding_window, nb_done_packets, packet->numAcquittement);

                DEBUG_PRINT("\t done + window = %d | ack = %d\n", nb_done_packets + sliding_window, packet->numAcquittement);
                if (return_value == 0) // TIMEOUT
                {
                    sliding_window /= 2; // size of the sliding window is divided by 2
                    if(sliding_window < 1) sliding_window = 1;
                    numSeq = nb_done_packets; // restart sending again from the last received ACK
                    status = ESTABLISHED; // we need to resend the packet instantly
                    if(flux.idFlux == 0)
                        DEBUG_PRINT("\t\t%d ---> TIMEOUT | new window %d | numSeq %d\n", flux.idFlux, sliding_window, numSeq);
                }
                else if (nb_done_packets + sliding_window == packet->numAcquittement) // check if numAcq is the one we were expecting
                {
                    DEBUG_PRINT("\t\t%d ---> SUCCESS = %d\n", flux.idFlux, sliding_window);

                    status = ESTABLISHED;
                    nb_done_packets = packet->numAcquittement;
                    nb_lost_packet = 0; // reset the counter we are done with it
                    sliding_window *= 2; // every packet has been acquitted
                    // sliding_window++; // not clear on this point, which version is correct

                    if(flux.idFlux == 0)
                        DEBUG_PRINT("\t\t\t%d ---> packets already done %d | new window %d\n", flux.idFlux, nb_done_packets, sliding_window);

                    if (nb_done_packets >= nb_packets) // if every packet has been sent, we are done here
                    {
                        status = TERM_SEND_FIN; // we start the close connection process
                        //DEBUG_PRINT("%d ---> Start FIN | WAITING_ACK to TERM_SEND_FIN\n", flux.idFlux);
                    }

                    /* this packet is over, it has been acknowledged
                    sending_window[nb_done_packets] = 1;*/

                    //DEBUG_PRINT("\t\t%d ---> SUCCESS = %d\n", flux.idFlux, sliding_window);
                    /*status = ESTABLISHED;
                    nb_done_packets = packet->numAcquittement;
                    nb_lost_packet = 0; // reset the counter we are done with it
                    sliding_window *= 2; // every packet has been acquitted
                    // sliding_window++; // not clear on this point, which version is correct

                    if(flux.idFlux == 0)
                        DEBUG_PRINT("\t\t\t%d ---> packets already done %d | new window %d\n", flux.idFlux, nb_done_packets, sliding_window);

                    //DEBUG_PRINT("numseq(%d) ?= (nb_done_packets: %d + 1 = %d)\n", numSeq, nb_done_packets, nb_done_packets+1);

                    if (nb_done_packets >= nb_packets) // if every packet has been sent, we are done here
                    {
                        status = TERM_SEND_FIN; // we start the close connection process
                        //DEBUG_PRINT("%d ---> Start FIN | WAITING_ACK to TERM_SEND_FIN\n", flux.idFlux);
                    }*/

                }
                else // not the ACK we expected, we lost a packet
                {
                    // restart sending again from the last received ACK
                    nb_done_packets = packet->numAcquittement;
                    numSeq = packet->numAcquittement;
                    nb_lost_packet++;

                    if (nb_lost_packet == 3) // if we lost 3x the same packet
                    {
                        sliding_window = 1; // reset sliding window to 1
                        nb_lost_packet = 0;
                    }

                    status = ESTABLISHED; // we need to resend the packet instantly
                    if(flux.idFlux == 0)
                        DEBUG_PRINT("\t\t%d ---> LOST | done = %d and lost = %d\n", flux.idFlux, nb_done_packets, nb_lost_packet);
                }

                /*if (packet->ECN == ECN_ACTIVE) // ECN is active
                {
                    sliding_window = (uint8_t) (sliding_window * 0.90); // -10%, rounded down by cast
                    if(flux.idFlux == 0)
                        DEBUG_PRINT("%d ---> ECN | new window %d\n", flux.idFlux, sliding_window);
                }*/
                if(flux.idFlux == 0)
                    DEBUG_PRINT("\t ===== STOP READ %d ===== WINDOW = %d\n", flux.idFlux, sliding_window);
            }
        }

        if (status == WAITING_SYN_ACK) // trying to establish a connection
        {

            //DEBUG_PRINT("%d ===== WAITING_SYN_ACK =====\n", flux.idFlux);

            if (return_value == 0) // TIMEOUT
            {
                status = DISCONNECTED; // we need to restart the connection process
                //DEBUG_PRINT("%d ---> TIMEOUT : WAITING_SYN_ACK to DISCONNECTED\n", flux.idFlux);
            } else if (return_value > 0) // no timeout : process normally
            {
                // read packet received from manager (trough pipe)
                if (read(flux.pipe_read, packet, 52) != 52)
                    raler("read pipe");
                //DEBUG_PRINT("%d ===== Read packet =====\n", flux.idFlux);

                // we expect the type to be ACK|SYN in order to continue
                if (!(packet->type & ACK) || !(packet->type & SYN)) // not ACK|SYN
                {
                    status = DISCONNECTED; // we need to restart the connection process
                    //DEBUG_PRINT("%d ---> not ACK|SYN : WAITING_SYN_ACK to DISCONNECTED\n", flux.idFlux);
                } else // ACK|SYN : process normally and send ACK
                {
                    packet->type = ACK;
                    packet->numAcquittement = packet->numSequence + 1;
                    sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                    status = ESTABLISHED;
                    numSeq = 0;
                    //DEBUG_PRINT("%d ---> ACK sent | WAITING_SYN_ACK to ESTABLISHED\n", flux.idFlux);
                }
            }
        }

        if (status == DISCONNECTED) // about to start the connection
        {
            //DEBUG_PRINT("%d ===== DISCONNECTED =====\n", flux.idFlux);

            // reset variables in case there was an issue somewhere
            numSeq = 0;
            sliding_window = 1;
            return_value = -1;
            nb_done_packets = 0;
            nb_lost_packet = 0;

            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux.idFlux, SYN, numSeq, 0, ECN_DISABLED, sliding_window, "");
            sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
            status = WAITING_SYN_ACK; // now waiting for a packet with SYN|ACK

            //DEBUG_PRINT("%d ---> DISCONNECTED to WAITING_SYN_ACK\n", flux.idFlux);
        }

        if (status == ESTABLISHED) // sending a sequence
        {
            //DEBUG_PRINT("%d ===== ESTABLISHED ====== Start Sequence | WINDOW = %d\n", flux.idFlux, sliding_window);

            // sending packets until we reach the edge of the sliding window
            if(flux.idFlux == 0)
                DEBUG_PRINT("\n\t===== START SEQUENCE %d ===== numSeq: %d, nbDonePackets: %d, sliding_window: %d, nb_packets: %d\n", flux.idFlux, numSeq, nb_done_packets, sliding_window, nb_packets);

            if(nb_done_packets + sliding_window > nb_packets)
                sliding_window = nb_packets - nb_done_packets;

            while (numSeq < (nb_done_packets + sliding_window) && numSeq < nb_packets)
            {
/*                if(sending_window[numSeq] == 1) // already sent and acquitted
                    continue;*/

                // get the corresponding data we need to send
                int fromEnd = (numSeq + 1) * PACKET_DATA_SIZE;
                if (fromEnd > flux.bufLen)
                    fromEnd = flux.bufLen - fromEnd;
                substr(flux.buf, data, numSeq * PACKET_DATA_SIZE, fromEnd);

                if(flux.idFlux == 0)
                    DEBUG_PRINT("\t\t%d ---> MESSAGE = %d %s\n", flux.idFlux, numSeq, data);

                // prepare the packet and sending it
                setPacket(packet, flux.idFlux, 0, numSeq, 0, ECN_DISABLED, sliding_window, data);
                sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                numSeq++; // getting closer the edge of the sliding window
            }
            status = WAITING_ACK; // we need to make some space : waiting for the ACKs
            if(flux.idFlux == 0)
                DEBUG_PRINT("\t===== END SEQUENCE %d =====\n", flux.idFlux);
        }

        if (status >= TERM_WAIT_ACK && status <= TERM_WAIT_TERM) // continue close connection process
        {
            //DEBUG_PRINT("%d ===== TERM_WAIT_ACK - TERM_WAIT_TERM =====\n", flux.idFlux);

            if (return_value == 0) // TIMEOUT
            {
                if (status == TERM_WAIT_ACK ||
                    status == TERM_WAIT_FIN) // we need to restart the close connection process
                {
                    status = TERM_SEND_FIN;
                    //DEBUG_PRINT("%d ---> TIMEOUT : to TERM_SEND_FIN\n", flux.idFlux);
                }
                else if (status == TERM_WAIT_TERM) // closing process has succeeded
                {
                    //DEBUG_PRINT("%d ---> TIMEOUT : TERM_WAIT_TERM, thread stopping...\n", flux.idFlux);
                    break;
                }
            }
            else if (return_value > 0)
            {
                // read packet received from manager (trough pipe)
                if (read(flux.pipe_read, packet, 52) != 52)
                    raler("read pipe");
                //DEBUG_PRINT("%d ===== Read packet =====\n", flux.idFlux);

                // waiting for FIN in order to send the last ACK
                if (status == TERM_WAIT_FIN && packet->type & FIN)
                {
                    setPacket(packet, flux.idFlux, ACK, packet->numSequence, packet->numSequence + 1, ECN_DISABLED,
                              sliding_window, "");
                    sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                    status = TERM_WAIT_TERM; // last step before the end
                    //DEBUG_PRINT("%d ---> Wait FIN : TERM_WAIT_FIN to TERM_WAIT_TERM\n", flux.idFlux);
                }

                // we sent FIN and are waiting for its ACK
                if (status == TERM_WAIT_ACK && packet->type & ACK)
                {
                    status = TERM_WAIT_FIN; // continue the close connection process
                    //DEBUG_PRINT("%d ---> Wait ACK (FIN) : TERM_WAIT_ACK to TERM_WAIT_FIN\n", flux.idFlux);
                }
            }
        }

        if (status == TERM_SEND_FIN) // about to close the connection
        {
            //DEBUG_PRINT("%d ===== TERM_SEND_FIN =====\n", flux.idFlux);

            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux.idFlux, FIN, numSeq, 0, ECN_DISABLED, sliding_window, "");
            sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
            status = TERM_WAIT_ACK; // now waiting for a packet with ACK

            //DEBUG_PRINT("%d ---> TERM_SEND_FIN to TERM_WAIT_ACK\n", flux.idFlux);
        }

        FD_ZERO(&working_set);
        FD_SET(flux.pipe_read, &working_set);

        tv.tv_sec = 0;
        if(status == TERM_WAIT_TERM) // 2x longer after the last ACK in the close connection process
            tv.tv_usec = 2 * TIMEOUT;
        if(status == ESTABLISHED)
            tv.tv_usec = sliding_window * TIMEOUT;
        else // one timeout for each packet send
            tv.tv_usec = TIMEOUT;

        DEBUG_PRINT("%d ===== SELECT ===== %d and wait sec = %ld, usec = %ld ", flux.idFlux, flux.pipe_read, tv.tv_sec, tv.tv_usec);

        // waiting for the manager to send us a packet in order to continue (through pipe)
        return_value = select(flux.pipe_read + 1, &working_set, NULL, NULL, &tv);
        if (return_value == -1) raler("select ici\n");
        DEBUG_PRINT("%d ===== Received packet ===== ", flux.idFlux);

    } while (1);

    DEBUG_PRINT("========== %d IS OVER ==========\n", packet->idFlux);

    destroyPacket(packet);
    pthread_exit(NULL);
}

/**
 * @fn      void *doStopWait(void *arg)
 * @brief   Receives the packets from the manager trough pipes, treat it and send a packet back (stop and wait mechanism)
 * @param   arg         Argument send when the thread was created, struct flux in this case
 */
void *doStopWait(void *arg)
{
    struct flux_args *flux = (struct flux_args *) arg; // structure
    flux_status_t status = DISCONNECTED; // flux status by default

    packet_t packet = newPacket(); // init a packet used to store and send data
    packet_status_t packet_status = SEND_PACKET; // packet status by default

    // variables
    uint16_t numSeq = 0; // numSeq by default
    struct timeval tv; // set timeout : 500 ms
    ssize_t return_value = -1; // used to check for timeouts
    fd_set working_set; // fd_set used for select
    char data[PACKET_DATA_SIZE]; // current data to send

    DEBUG_PRINT("flux flux=%d, Len: %d\n", flux->idFlux, flux->bufLen);

    int nb_packets = (flux->bufLen - 1) / PACKET_DATA_SIZE + 1; // nb packets to send
    int nb_done_packets = 0; // nb packets already sent

    DEBUG_PRINT("Start flux=%d, thread with data=%s (%d packets to send)\n", flux->idFlux, flux->buf, nb_packets);

    do
    {
        if (status == WAITING_SYN_ACK) // trying to establish a connection
        {
            DEBUG_PRINT("%d ===== WAITING_SYN_ACK =====\n", flux->idFlux);

            if (return_value == 0) // TIMEOUT
            {
                status = DISCONNECTED; // we need to restart the connection process
                DEBUG_PRINT("%d ---> TIMEOUT : WAITING_SYN_ACK to DISCONNECTED\n", flux->idFlux);
            }
            else if (return_value > 0) // no timeout : process normally
            {
                // read packet received from manager trough pipe
                if (read(flux->pipe_read, packet, 52) != 52)
                    raler("read pipe");
                DEBUG_PRINT("%d ===== Read packet =====\n", flux->idFlux);

                // we expect the type to be ACK|SYN in order to continue
                if (!(packet->type & ACK) || !(packet->type & SYN)) // not ACK|SYN
                {
                    status = DISCONNECTED; // we need to restart the connection process
                    DEBUG_PRINT("%d ---> not ACK|SYN : WAITING_SYN_ACK to DISCONNECTED\n", flux->idFlux);
                }
                else // ACK|SYN : process normally and send ACK
                {
                    packet->type = ACK;
                    packet->numAcquittement = packet->numSequence + 1;
                    sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
                    status = ESTABLISHED;
                    DEBUG_PRINT("%d ---> ACK sent | WAITING_SYN_ACK to ESTABLISHED\n", flux->idFlux);
                }
            }
        }

        if (status == DISCONNECTED) // about to start the connection
        {
            DEBUG_PRINT("%d ===== DISCONNECTED =====\n", flux->idFlux);

            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux->idFlux, SYN, numSeq, 0, ECN_DISABLED, 0, "");
            sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
            status = WAITING_SYN_ACK; // now waiting for a packet with SYN|ACK

            DEBUG_PRINT("%d ---> DISCONNECTED to WAITING_SYN_ACK\n", flux->idFlux);
        }

        if (status == ESTABLISHED) {

            DEBUG_PRINT("%d ===== ESTABLISHED =====\n", flux->idFlux);

            // if : packet has been sent, waiting for his ACK
            if (packet_status == WAIT_ACK)
            {
                if (return_value == 0) // TIMEOUT
                {
                    packet_status = RESEND_PACKET; // we need to resend a packet
                    DEBUG_PRINT("%d ---> TIMEOUT : RESEND_PACKET\n", flux->idFlux);
                }
                else if (return_value > 0) // no timeout : process normally
                {
                    // read packet received from manager trough pipe
                    if (read(flux->pipe_read, packet, 52) != 52)
                        raler("read pipe");

                    DEBUG_PRINT("Flux thread = %d, go packet, ack = %d, seqNum = %d, type = %s \n",
                                flux->idFlux, packet->numAcquittement, packet->numSequence,
                                packet->type & ACK ? "ACK" : "Other");

                    if (packet->type & ACK && packet->type & SYN) // issue during the open connection process
                    {
                        packet->type = ACK;
                        packet->numAcquittement = packet->numSequence + 1;
                        sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
                        packet_status = RESEND_PACKET; // not the type expected, we need to resend the packet
                        DEBUG_PRINT("%d ---> ISSUE : ack syn : RESEND_PACKET\n", flux->idFlux);
                    }
                    else
                    {
                        if (!(packet->type & ACK) || packet->numAcquittement != numSeq) // not corresponding ACK expected
                        {
                            packet_status = RESEND_PACKET; // we need to resend a packet
                            DEBUG_PRINT("%d ---> ISSUE : not ack expected : RESEND_PACKET\n", flux->idFlux);
                        }
                        else
                        {
                            nb_done_packets++; // packet is done
                            packet_status = SEND_PACKET; // next up, we want to send another packet

                            // if every packet has been sent, we are done here
                            if (nb_done_packets >= nb_packets)
                            {
                                status = TERM_SEND_FIN;
                                DEBUG_PRINT("%d ---> start closing process : TERM_SEND_FIN\n", flux->idFlux);
                            }
                        }
                    }
                }
            }
        }

        if (status == ESTABLISHED) // sending a packet
        {
            // SEND_PACKET : update numSeq and data
            // RESEND_PACKET : nothing to update

            if (packet_status == SEND_PACKET)
            {
                numSeq = numSeq == 0 ? 1 : 0; // alternative bit

                // get the corresponding data we need to send
                int fromEnd = (nb_done_packets + 1) * PACKET_DATA_SIZE;
                if (fromEnd > flux->bufLen)
                    fromEnd = flux->bufLen - fromEnd;
                substr(flux->buf, data, nb_done_packets * PACKET_DATA_SIZE, fromEnd);
            }

            DEBUG_PRINT("Send packet idFlux = %d, status = %s, data = %s\n", flux->idFlux, packet_status == SEND_PACKET ?
                                                                                           "Send packet" : (packet_status == RESEND_PACKET ? "Resend packet": "Wait ACK"), data);

            // prepare the packet and sending it
            setPacket(packet, flux->idFlux, 0, numSeq, 0, ECN_DISABLED, 0, data);
            sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
            packet_status = WAIT_ACK; // waiting for the ACK before sending another packet
        }

        if (status >= TERM_WAIT_ACK && status <= TERM_WAIT_TERM) // continue close connection process
        {
            DEBUG_PRINT("%d ===== TERM_WAIT_ACK - TERM_WAIT_TERM =====\n", flux->idFlux);

            if (return_value == 0) // TIMEOUT
            {
                if (status == TERM_WAIT_ACK || status == TERM_WAIT_FIN) // we need to restart the close connection process
                {
                    status = TERM_SEND_FIN;
                    DEBUG_PRINT("%d ---> TIMEOUT : to TERM_SEND_FIN\n", flux->idFlux);
                }
                else if (status == TERM_WAIT_TERM) // closing process has succeeded
                {
                    DEBUG_PRINT("%d ---> TIMEOUT : TERM_WAIT_TERM, thread stopping...\n", flux->idFlux);
                    break;
                }
            }
            else if (return_value > 0)
            {
                // read packet received from manager trough pipe
                if (read(flux->pipe_read, packet, 52) != 52)
                    raler("read pipe");
                DEBUG_PRINT("%d ===== Read packet =====\n", flux->idFlux);

                // waiting for FIN in order to send the last ACK
                if (status == TERM_WAIT_FIN && packet->type & FIN)
                {
                    setPacket(packet, flux->idFlux, ACK, packet->numSequence, packet->numSequence + 1, 0, 0, "");
                    sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
                    status = TERM_WAIT_TERM; // last step before the end
                    DEBUG_PRINT("%d ---> Wait FIN : TERM_WAIT_FIN to TERM_WAIT_TERM\n", flux->idFlux);
                }

                // we sent FIN and are waiting for its ACK
                if (status == TERM_WAIT_ACK && packet->type & ACK) {
                    status = TERM_WAIT_FIN; // continue the close connection process
                    DEBUG_PRINT("%d ---> Wait ACK (FIN) : TERM_WAIT_ACK to TERM_WAIT_FIN\n", flux->idFlux);
                }
            }
        }

        if (status == TERM_SEND_FIN) // about to close the connection
        {
            DEBUG_PRINT("%d ===== TERM_SEND_FIN =====\n", flux->idFlux);

            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux->idFlux, FIN, numSeq, 0, 0, 0, "");
            sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
            status = TERM_WAIT_ACK; // now waiting for a packet with ACK

            DEBUG_PRINT("%d ---> TERM_SEND_FIN to TERM_WAIT_ACK\n", flux->idFlux);
        }

        FD_ZERO(&working_set);
        FD_SET(flux->pipe_read, &working_set);

        tv.tv_sec = 0;
        tv.tv_usec = status == TERM_WAIT_TERM ? 8 * TIMEOUT : 4 * TIMEOUT; // 2x longer after the last ACK in the close connection process
        DEBUG_PRINT("%d ===== SELECT ===== %d and wait sec = %ld, usec = %ld\n", flux->idFlux, flux->pipe_read, tv.tv_sec, tv.tv_usec);

        // waiting for a packet to be send from the manager (trough pipe)
        return_value = select(flux->pipe_read + 1, &working_set, NULL, NULL, &tv);
        if (return_value == -1) raler("select ici\n");
        DEBUG_PRINT("%d ===== Received packet =====\n", flux->idFlux);

    } while (1);

    return NULL;
}

/**
 * @fn      void *doManager(void *arg)
 * @brief   Receives the packets from the medium and sends them to the corresponding flux (threads) through pipes
 * @param   arg         Argument send when the thread was created, struct manager in this case
 */
void *doManager(void *arg)
{
    struct manager main_thr = *(struct manager *) arg; // structure
    packet_t packet = newPacket(); // init a packet used to store and send data
    ssize_t return_value; // used to check for timeouts

    // timeout parameters
    struct timeval timeval;
    timeval.tv_usec = TIMEOUT;
    timeval.tv_sec = 0;

    // On considère que les timeout sont important ici,
    // Car on souhaite checker si le pointeur thread_status change
    // Et si recvfrom est bloquant on pourra pas sortir du thread
    if (setsockopt(main_thr.tcp->inSocket, SOL_SOCKET, SO_RCVTIMEO, &timeval, sizeof(timeval)) < 0)
        raler("setsockopt");

    do // until thread_status value is "STOP"
    {
        /* receive packet */
        return_value = recvfrom(main_thr.tcp->inSocket, packet, 52, 0, NULL, NULL);
        //DEBUG_PRINT("doManager: recvfrom socket = %d\n", main_thr.tcp->inSocket);

        if (return_value < 0) // timeout
        {
            //DEBUG_PRINT("doManager: timeout...\n");
            continue; // ignored because it's handled separately
        }

        //DEBUG_PRINT("recv for flux = %d\n", packet->idFlux);

        if (packet->idFlux >= main_thr.nb_flux) // check : idFlux exists
            continue;

        //DEBUG_PRINT("doManager: write to flux: %d, pipe_write = %d\n", packet->idFlux,
                    //main_thr.pipes[packet->idFlux]);

        // send packet to flux using pipes (flux corresponding to packet->idFlux)
        return_value = write(main_thr.pipes[packet->idFlux], packet, 52);
        if (return_value < 0)
        {
            printf("Write failed for flux=%d, pipe fd=%d\n", packet->idFlux, main_thr.pipes[packet->idFlux]);
            raler("manager: write");
        }

    } while (*main_thr.thr_status != STOP);

    /* End of the manager thread */

    //DEBUG_PRINT("doManager: main thread stopping...\n");

    destroyPacket(packet);
    pthread_exit(NULL);
}

/**
 * @fn      handle(tcp_t tcp, modeTCP_t mode, struct flux *fluxes, int nb_flux)
 * @brief   Executes the "source" mechanism
 * @param   tcp         TCP structure
 * @param   mode        Mechanism chosen by the user
 * @param   *fluxes     All of the fluxes
 * @param   nb_flux     Total number of fluxes we will be using
 */
void handle(tcp_t tcp, modeTCP_t mode, struct flux *fluxes, int nb_flux)
{
    thread_status_t *thr_status = malloc(sizeof(thread_status_t));
    pthread_t *thr_id = malloc(sizeof(pthread_t) * (nb_flux + 1)); // list of all the threads id : manager + one for each flux
    struct flux_args fluxes_thr[FLUX_NB]; // list of all the threads related to fluxes : one for each flux

    // list of all the pipes : one for each flux, fluxes can communicate with the manager
    int **pipes = malloc(sizeof(int) * nb_flux);
    int write_pipes[FLUX_NB]; // used for the manager (writing pipes)

    // creates the manager of all the fluxes
    struct manager main_thr;
    main_thr.tcp = tcp; // TCP structure used to communicate
    main_thr.nb_flux = nb_flux; // number of fluxes the main thread will manage
    main_thr.pipes = write_pipes; // writing part of pipes used to communicate with each flux
    main_thr.thr_status = thr_status; // thread status (not working)

    // creates main thread (manager)
    //DEBUG_PRINT("Start manager thread\n");
    pthread_create(&thr_id[0], NULL, (void *) doManager, (void *) &main_thr);

    // creates a thread for each flux
    for (int i = 0; i < nb_flux; i++)
    {
        // creates a flux structure
        struct flux flux = fluxes[i];
        fluxes_thr[i].tcp = tcp;
        fluxes_thr[i].idFlux = flux.fluxId;
        fluxes_thr[i].buf = malloc(flux.bufLen);
        fluxes_thr[i].bufLen = flux.bufLen;
        strcpy(fluxes_thr[i].buf, flux.buf);
        //DEBUG_PRINT("create flux_thr for flux=%d; idFlux=%d\n", i, flux.fluxId);

        // open pipe for the thread (flux) to communicate with the manager
        pipes[i] = malloc(sizeof(int) * 2);
        if (pipe(pipes[i]) < 0) perror("pipe");

        // set pipes for manager thread (write) and flux thread (read)
        fluxes_thr[i].pipe_read = pipes[i][0];
        write_pipes[i] = pipes[i][1];
        //DEBUG_PRINT("Opened new pipe for flux=%d; read=%d, write=%d\n", i, pipes[i][0], pipes[i][1]);
    }

    // creates nb_flux threads, each one corresponding to a flux
    // different function, depending on the mode the user chose
    for (int i = 1; i <= nb_flux; ++i)
        if (pthread_create(&thr_id[i], NULL, mode == STOP_AND_WAIT ? (void *) doStopWait : (void *) doGoBackN,(void *) &fluxes_thr[i - 1]) > 0)
            perror("pthread");

    // waiting for each thread (flux) to end
    for (int i = 1; i <= nb_flux; ++i)
        if (pthread_join(thr_id[i], NULL) > 0)
            perror("pthread_join");

    //DEBUG_PRINT("All flux threads stopped...\n");

    // stoping the manager (not working)
    *thr_status = STOP;

    // waiting for the manager to end
    if (pthread_join(thr_id[0], NULL) > 0)
        perror("pthread_join");

    // END : close and free everything

    for (int i = 0; i < nb_flux; ++i)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
        free(pipes[i]);
    }

    free(thr_id);
    free(thr_status);
}

/**
 * @fn      int main(int argc, char *argv[])
 * @brief   Initialize and starts everything
 */
int main(int argc, char *argv[])
{
    // if : args unvalid

    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s <mode> <IP_distante> <port_local> <port_ecoute_src_pertubateur>\n", argv[0]);
        exit(1);
    }

    modeTCP_t mode = parseMode(argv[1]);

    if (mode == UNKNOWN)
    {
        fprintf(stderr, "Usage: <mode> must be either 'stop-wait' or 'go-back-n'\n");
        exit(1);
    }

    // else

    char *ip = argv[2];
    int port_local = string_to_int(argv[3]);
    int port_medium = string_to_int(argv[4]);

    DEBUG_PRINT("\nMode chosen : %d\nDestination address : %s\nLocal port set at : %d\nDestination port set at : %d\n=================================\n", mode, ip, port_local, port_medium);

    tcp_t tcp = createTcp(ip, port_local, port_medium);

    int nbflux = FLUX_NB;
    struct flux fluxes[FLUX_NB];

    for (int i = 0; i < nbflux; ++i)
    {

        int spam = 10 * 44;
        //rand() % (UINT8_MAX) + UINT8_MAX * 30; + UINT8_MAX*500 ; * 10; + UINT8_MAX * 1000;
        fluxes[i].buf = malloc(spam);

        for (int j = 0; j < spam; ++j)
            fluxes[i].buf[j] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[random() % 26];

        fluxes[i].bufLen = spam;
        fluxes[i].fluxId = i;
    }

    handle(tcp, mode, fluxes, nbflux);

    destroyTcp(tcp);

    return 0;
}