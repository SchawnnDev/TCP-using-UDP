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

#if defined(DEBUG) && DEBUG > 0
#define DEBUG_PRINT(fmt, args...) printf(fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...) /* Don't do anything in release builds */
#endif

enum flux_status {
    DISCONNECTED = 0x0,
    WAITING_SYN_ACK = 0x1,
    WAITING_ACK = 0x2,
    ESTABLISHED = 0x3,
    TERM_SEND_FIN = 0x4,
    TERM_WAIT_ACK = 0x5,
    TERM_WAIT_FIN = 0x6,
    TERM_WAIT_TERM = 0x7
};
typedef enum flux_status flux_status_t;

enum packet_status {
    WAIT_ACK = 0,
    SEND_PACKET = 1,
    RESEND_PACKET = 2
};
typedef enum packet_status packet_status_t;

enum thread_status {
    START = 0,
    STOP = 1
};
typedef enum thread_status thread_status_t;

enum modeTCP {
    UNKNOWN = -1,
    STOP_AND_WAIT = 0,
    GO_BACK_N = 1
};
typedef enum modeTCP modeTCP_t; // mode_t already used

modeTCP_t parseMode(char *mode) {
    if (strcmp(mode, "stop-wait") != 0)
        return STOP_AND_WAIT;
    if (strcmp(mode, "go-back-n") != 0)
        return GO_BACK_N;
    return UNKNOWN; // Default
}

struct flux {
    int fluxId;
    flux_status_t status;
    char *buf;
    int bufLen;
};
typedef struct flux *flux_t;

struct flux_args {
    tcp_t tcp;
    int idFlux;
    int pipe_read;
    char *buf;
    int bufLen;
};

struct manager {
    tcp_t tcp;
    int *pipes;
    int nb_flux;
    thread_status_t *thr_status;
};

void *doGoBackN(void *arg) {

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

    // set timeout : 500 ms
    struct timeval tv;

    // others
    fd_set working_set; // fd_set used for select
    char data[PACKET_DATA_SIZE]; // current data to send

    do
    {

        if(status == WAITING_ACK) // waiting for all the ACKs of the previous sequence
        {
            DEBUG_PRINT("%d ===== WAITING_ACK =====\n", flux.idFlux);

            // read packet received from manager (trough pipe)
            if (read(flux.pipe_read, packet, 52) != 52)
                raler("read pipe");
            DEBUG_PRINT("%d ===== Read packet =====\n", flux.idFlux);

            // receiving the type ACK|SYN here means the ACK we sent has been lost
            if(packet->type & ACK && packet->type & SYN) // ACK|SYN
            {
                status = WAITING_SYN_ACK; // we need to send a new ACK
                DEBUG_PRINT("%d ---> ACK|SYN Restart handshake : WAITING_ACK to WAITING_SYN_ACK\n", flux.idFlux);
            }
            else // it cannot be anything else other than an ACK here
            {
                // get the previous sliding window value
                if(numSeq >= (nb_done_packets + sliding_window)) // only true for the first ACK of a sequence
                    sliding_window = packet->tailleFenetre;

                DEBUG_PRINT("%d ---> Window size at start of ACK sequence = %d\n", flux.idFlux, sliding_window);

                if(return_value == 0) // TIMEOUT
                {
                    sliding_window /= 2; // size of the sliding window is divided by 2
                    status = ESTABLISHED; // we need to resend the packet instantly
                    DEBUG_PRINT("%d ---> TIMEOUT | new window %d | WAITING_ACK to ESTABLISHED\n", flux.idFlux, sliding_window);
                }
                else if((nb_done_packets + 1) == packet->numAcquittement) // check if numAcq is the one we were expecting
                {
                    nb_done_packets++; // this packet is over, it has been acknowledged
                    nb_lost_packet = 0; // reset the counter we are done with it
                    sliding_window++; // one more packet can fit in the sliding window

                    DEBUG_PRINT("%d ---> packets already done %d | new window %d\n", flux.idFlux, nb_done_packets, sliding_window);

                    if (nb_done_packets >= nb_packets) // if every packet has been sent, we are done here
                    {
                        status = TERM_SEND_FIN; // we start the close connection process
                        DEBUG_PRINT("%d ---> Start FIN | WAITING_ACK to TERM_SEND_FIN\n", flux.idFlux);
                    }
                    else if(numSeq == (nb_done_packets + 1)) // true if we received all the ACKs we were supposed to
                    {
                        status = ESTABLISHED; // we start a new sequence of packet to send
                        DEBUG_PRINT("%d ---> all ACKs -> new Sequence | WAITING_ACK to ESTABLISHED\n", flux.idFlux);
                    }
                }
                else // not the ACK we expected, we lost a packet
                {
                    numSeq = packet->numAcquittement; // get the numSeq of the packet we lost
                    nb_lost_packet++;

                    if(nb_lost_packet == 3) // if we lost 3x the same packet
                        sliding_window = 1; // reset sliding window to 1
                    status = ESTABLISHED; // we need to resend the packet instantly
                    DEBUG_PRINT("%d ---> Lost ACK | numSeq %d | lost %d | WAITING_ACK to ESTABLISHED\n", flux.idFlux, numSeq, nb_lost_packet);
                }

                if(packet->ECN == ECN_ACTIVE) // ECN is active
                {
                    sliding_window = (uint8_t) (sliding_window * 0.90); // -10%, rounded down by cast
                    DEBUG_PRINT("%d ---> ECN | new window %d\n", flux.idFlux, sliding_window);
                }
                DEBUG_PRINT("%d ---> WINDOWS END = %d\n", flux.idFlux, sliding_window);
            }
        }

        if (status == WAITING_SYN_ACK) // trying to establish a connection
        {

            DEBUG_PRINT("%d ===== WAITING_SYN_ACK =====\n", flux.idFlux);

            if (return_value == 0) // TIMEOUT
            {
                status = DISCONNECTED; // we need to restart the connection process
                DEBUG_PRINT("%d ---> TIMEOUT : WAITING_SYN_ACK to DISCONNECTED\n", flux.idFlux);
            }
            else if (return_value > 0) // no timeout : process normally
            {
                // read packet received from manager (trough pipe)
                if (read(flux.pipe_read, packet, 52) != 52)
                    raler("read pipe");
                DEBUG_PRINT("%d ===== Read packet =====\n", flux.idFlux);

                // we expect the type to be ACK|SYN in order to continue
                if (!(packet->type & ACK) || !(packet->type & SYN)) // not ACK|SYN
                {
                    status = DISCONNECTED; // we need to restart the connection process
                    DEBUG_PRINT("%d ---> not ACK|SYN : WAITING_SYN_ACK to DISCONNECTED\n", flux.idFlux);
                }
                else // ACK|SYN : process normally and send ACK
                {
                    packet->type = ACK;
                    packet->numAcquittement = packet->numSequence + 1;
                    sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                    status = ESTABLISHED;
                    DEBUG_PRINT("%d ---> ACK sent | WAITING_SYN_ACK to ESTABLISHED\n", flux.idFlux);
                }
            }
        }

        if (status == DISCONNECTED) // about to start the connection
        {
            DEBUG_PRINT("%d ===== DISCONNECTED =====\n", flux.idFlux);

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

            DEBUG_PRINT("%d ---> DISCONNECTED to WAITING_SYN_ACK\n", flux.idFlux);
        }

        if(status == ESTABLISHED) // sending a sequence
        {
            DEBUG_PRINT("%d ===== ESTABLISHED ====== Start Sequence | WINDOW = %d\n", flux.idFlux, sliding_window);

            // sending packets until we reach the edge of the sliding window
            while(numSeq >= (nb_done_packets + sliding_window) && numSeq < nb_packets)
            {
                // get the corresponding data we need to send
                int fromEnd = (numSeq + 1) * PACKET_DATA_SIZE;
                if (fromEnd > flux.bufLen)
                    fromEnd = flux.bufLen - fromEnd;
                substr(flux.buf, data, numSeq * PACKET_DATA_SIZE, fromEnd);

                DEBUG_PRINT("%d ---> MESSAGE = %d %s\n", flux.idFlux, numSeq, data);

                // prepare the packet and sending it
                setPacket(packet, flux.idFlux, 0, numSeq, 0, ECN_DISABLED, sliding_window, data);
                sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                numSeq++; // getting closer the edge of the sliding window
            }
            status = WAITING_ACK; // we need to make some space : waiting for the ACKs
            DEBUG_PRINT("%d ---> End Sequence | ESTABLISHED to WAITING_ACK\n", flux.idFlux);
        }

        if (status >= TERM_WAIT_ACK && status <= TERM_WAIT_TERM) // continue close connection process
        {
            DEBUG_PRINT("%d ===== TERM_WAIT_ACK - TERM_WAIT_TERM =====\n", flux.idFlux);

            if (return_value == 0) // TIMEOUT
            {
                if (status == TERM_WAIT_ACK || status == TERM_WAIT_FIN) // we need to restart the close connection process
                {
                    status = TERM_SEND_FIN;
                    DEBUG_PRINT("%d ---> TIMEOUT : to TERM_SEND_FIN\n", flux.idFlux);
                }
                else if (status == TERM_WAIT_TERM) // closing process has succeeded
                {
                    DEBUG_PRINT("%d ---> TIMEOUT : TERM_WAIT_TERM, thread stopping...\n", flux.idFlux);
                    break;
                }
            }
            else if (return_value > 0)
            {
                // read packet received from manager (trough pipe)
                if (read(flux.pipe_read, packet, 52) != 52)
                    raler("read pipe");
                DEBUG_PRINT("%d ===== Read packet =====\n", flux.idFlux);

                // waiting for FIN in order to send the last ACK
                if (status == TERM_WAIT_FIN && packet->type & FIN)
                {
                    setPacket(packet, flux.idFlux, ACK, packet->numSequence,packet->numSequence + 1, ECN_DISABLED, sliding_window, "");
                    sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                    status = TERM_WAIT_TERM; // last step before the end
                    DEBUG_PRINT("%d ---> Wait FIN : TERM_WAIT_FIN to TERM_WAIT_TERM\n", flux.idFlux);
                }

                // we sent FIN and are waiting for its ACK
                if(status == TERM_WAIT_ACK && packet->type & ACK)
                {
                    status = TERM_WAIT_FIN; // continue the close connection process
                    DEBUG_PRINT("%d ---> Wait ACK (FIN) : TERM_WAIT_ACK to TERM_WAIT_FIN\n", flux.idFlux);
                }
            }
        }

        if (status == TERM_SEND_FIN) // about to close the connection
        {
            DEBUG_PRINT("%d ===== TERM_SEND_FIN =====\n", flux.idFlux);

            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux.idFlux, FIN, numSeq, 0, ECN_DISABLED, sliding_window, "");
            sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
            status = TERM_WAIT_ACK; // now waiting for a packet with ACK

            DEBUG_PRINT("%d ---> TERM_SEND_FIN to TERM_WAIT_ACK\n", flux.idFlux);
        }

        FD_ZERO(&working_set);
        FD_SET(flux.pipe_read, &working_set);

        tv.tv_sec = 0;
        tv.tv_usec = status == TERM_WAIT_TERM ? 2 * TIMEOUT : TIMEOUT; // 2x longer after the last ACK in the close connection process
        DEBUG_PRINT("%d ===== SELECT ===== %d and wait sec = %ld, usec = %ld\n", flux.idFlux, flux.pipe_read, tv.tv_sec, tv.tv_usec);

        // waiting for the manager to send us a packet in order to continue (through pipe)
        return_value = select(flux.pipe_read + 1, &working_set, NULL, NULL, &tv);
        if (return_value == -1) raler("select ici\n");
        DEBUG_PRINT("%d ===== Received packet =====\n", flux.idFlux);

    }while(1);

    DEBUG_PRINT("========== %d IS OVER ==========\n", packet->idFlux);

    destroyPacket(packet);
    pthread_exit(NULL);
}

/**
 * Thread pour gérer un flux pour lire et renvoyer les paquets d'un flux
 * Dans le schéma du stop and wait
 * @param arg Structure contenant arguments
 * @return
 */
void *doStopWait(void *arg) {
    // creates flux
    struct flux_args* flux = (struct flux_args *) arg;
    flux_status_t status = DISCONNECTED; // flux status by default

    // creates a packet
    packet_t packet = newPacket();
    packet_status_t packet_status = SEND_PACKET; // packet status by default

    // variables
    uint16_t numSeq = 0; // numSeq by default
    ssize_t return_value = -1; // error return
    fd_set working_set; // fd_set used for select
    char data[PACKET_DATA_SIZE]; // current data to send
  
    DEBUG_PRINT("flux flux=%d, Len: %d\n", flux->idFlux, flux->bufLen);

    // set counters
    int nb_packets = (flux->bufLen - 1) / PACKET_DATA_SIZE + 1; // nb packets to send
    int nb_done_packets = 0; // nb packets already sent

    // set timeout : 500 ms
    struct timeval tv;

    DEBUG_PRINT("Start flux=%d, thread with data=%s (%d packets to send)\n", flux->idFlux, flux->buf, nb_packets);

    do {

        if (status == WAITING_SYN_ACK) {
            if (return_value == 0) // if : timeout
                status = DISCONNECTED; // switch back to default status
            else if (return_value > 0) {
                // read packet received from manager trough pipe
                if (read(flux->pipe_read, packet, 52) != 52)
                    raler("read pipe");

                // expect : ACK SYN
                if (!(packet->type & ACK) || !(packet->type & SYN))
                    status = DISCONNECTED; // switch back to default status
                else {
                    packet->type = ACK;
                    packet->numAcquittement = packet->numSequence + 1;
                    sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
                    status = ESTABLISHED;
                }
            }
        }

        if (status == DISCONNECTED) { // send SYN, numSeq = random, numAcq = 0
            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux->idFlux, SYN, numSeq, 0, ECN_DISABLED, 0, "");
            sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
            status = WAITING_SYN_ACK; // switch status
        }

        if (status == ESTABLISHED) {
            // if : packet has been sent, waiting for his ACK
            if (packet_status == WAIT_ACK) {
                if (return_value == 0) // if : timeout
                    packet_status = RESEND_PACKET; // switch packet status, we want to resend a packet
                else if (return_value > 0) {

                    DEBUG_PRINT("doStopWait: ret > 0\n");

                    // read packet received from manager trough pipe
                    if (read(flux->pipe_read, packet, 52) != 52)
                        raler("read pipe");

                    DEBUG_PRINT("doStopWait: Flux thread = %d, go packet, ack = %d, seqNum = %d, type = %s \n",
                                flux->idFlux, packet->numAcquittement, packet->numSequence,
                                packet->type & ACK ? "ACK" : "Other");

                    // Si le serveur renvoie un ACK & SYN, même si le status est ESTABLISHED,
                    // alors on renvoie un ACK et on set le statut du paquet à RESEND_PAQUET
                    // pour qu'il renvoie le paquet DATA, car il a été ignoré par le serveur
                    if (packet->type & ACK && packet->type & SYN) {
                        packet->type = ACK;
                        packet->numAcquittement = packet->numSequence + 1;
                        sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
                        packet_status = RESEND_PACKET;
                    } else {
                        // check : is this an ACK ?
                        if (!(packet->type & ACK) || packet->numAcquittement != numSeq)
                            packet_status = RESEND_PACKET; // switch packet status, we want to resend a packet
                        else {
                            nb_done_packets++; // packet is done
                            packet_status = SEND_PACKET; // next up, we want to send another packet

                            // if every packet has been sent, we are done here
                            if (nb_done_packets >= nb_packets) {
                                status = TERM_SEND_FIN;
                                DEBUG_PRINT("doStopWait: debut TERM_SEND_FIN\n");
                            }
                        }
                    }
                }
            }
        }

        if (status == ESTABLISHED) {
            // Si le statut est d'envoyer un paquet,
            // Alors on change le numéro de séquence,
            // On y ajoute les données en question
            // en fonction du numéro de paquet que l'on envoie
            if (packet_status == SEND_PACKET) {
                numSeq = numSeq == 0 ? 1 : 0;

                // Ici on lit dans le buffer du flux,
                int fromEnd = (nb_done_packets + 1) * PACKET_DATA_SIZE;

                // on vérifie que le dernier buffer n'est pas plus grand que le buffer.
                if (fromEnd > flux->bufLen)
                    fromEnd = flux->bufLen - fromEnd;

                substr(flux->buf, data, nb_done_packets * PACKET_DATA_SIZE, fromEnd);
            }

            DEBUG_PRINT("HSW: Send packet fluxid=%d, status=%s, content=%s\n", flux->idFlux,
                        packet_status == SEND_PACKET ? "Send packet" : (packet_status == RESEND_PACKET ? "Resend packet"
                                                                                                       : "Wait ACK"),
                        data);
            // On set le paquet en question,
            // puis on l'envoie
            setPacket(packet, flux->idFlux, 0, numSeq, 0, 0, 0, data);
            sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
            // on met le mode du thread en WAIT ACK
            packet_status = WAIT_ACK;

        }

        if (status >= TERM_WAIT_ACK && status <= TERM_WAIT_TERM) {
            if (return_value == 0) // timeout
            {
                if (status == TERM_WAIT_ACK || status == TERM_WAIT_FIN) {
                    status = TERM_SEND_FIN;
                } else if (status == TERM_WAIT_TERM) {
                    // Si le double temps d'attente du rtt est dépassé,
                    // alors on peut stopper le programme.
                    DEBUG_PRINT("doStopWait: TERM_WAIT_TERM, thread stopping...\n");
                    break;
                }

            } else if (return_value > 0) {
                // read packet received from manager trough pipe
                if (read(flux->pipe_read, packet, 52) != 52)
                    raler("read pipe");

                if (status == TERM_WAIT_FIN && packet->type & FIN) {
                    setPacket(packet, flux->idFlux, ACK, packet->numSequence, packet->numSequence + 1, 0, 0, "");
                    sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
                    status = TERM_WAIT_TERM;
                }

                if (status == TERM_WAIT_ACK && packet->type & ACK) {
                    status = TERM_WAIT_FIN;
                }
            }
        }

        if (status == TERM_SEND_FIN) {
            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, flux->idFlux, FIN, numSeq, 0, 0, 0, "");
            sendPacket(flux->tcp->outSocket, packet, flux->tcp->sockaddr);
            status = TERM_WAIT_ACK;
        }

        FD_ZERO(&working_set);
        FD_SET(flux->pipe_read, &working_set);

        //DEBUG_PRINT("doStopWait: Select %d and wait sec = %ld, usec = %ld\n", flux->pipe_read, tv.tv_sec, tv.tv_usec);

        // timeout
        tv.tv_sec = 0;
        tv.tv_usec =
                status == TERM_WAIT_TERM ? 8 * TIMEOUT : 4* TIMEOUT; // Si on attend la terminaison, on attends 2x le RTT

        // waiting for a packet to be send from the manager (trough pipe)
        return_value = select(flux->pipe_read + 1, &working_set, NULL, NULL, &tv);

        if (return_value == -1) raler("select ici\n");

    } while (1);

}

/**
 *  Cette fonction gère le thread "principal"
 *  Ici, on recoit toutes les données du socket.
 *  Pour chaque paquet que l'on recoit, on prend le numéro du flux
 *  et on récupère le descripteur du tube en question.
 *  On renvoie le paquet sur le tube trouvé précédemment,
 *  (ce tube fait la communcation entre ce thread de gestion,
 *  et le thread qui gère le flux du paquet).
 * @param arg Structure contenant les descripteurs du socket, et des tubes
 * @return
 */
void *doManager(void *arg) {

    struct manager main_thr = *(struct manager *) arg;
    packet_t packet = newPacket();
    ssize_t return_value;

    struct timeval timeval;
    timeval.tv_usec = TIMEOUT;
    timeval.tv_sec = 0;

    // On considère que les timeout sont important ici,
    // Car on souhaite checker si le pointeur thread_status change
    // Et si recvfrom est bloquant on pourra pas sortir du thread
    if (setsockopt(main_thr.tcp->inSocket, SOL_SOCKET, SO_RCVTIMEO, &timeval, sizeof(timeval)) < 0) {
        raler("setsockopt");
    }

    do {
        /* receive packet */
        return_value = recvfrom(main_thr.tcp->inSocket, packet, 52, 0, NULL, NULL);
        //  DEBUG_PRINT("doManager: recvfrom socket = %d\n", main_thr.tcp->inSocket);

        // S'il y'a un timeout on ignore, car les timeouts sont gérés indépendamment
        if (return_value < 0) // timeout
        {
            //DEBUG_PRINT("doManager: timeout...\n");
            continue;
        }
        // DEBUG_PRINT("doManager: no timeout\n");

        DEBUG_PRINT("recv for flux = %d\n", packet->idFlux);
        // check : idFlux exists
        if (packet->idFlux >= main_thr.nb_flux)
            continue;
        DEBUG_PRINT("doManager: write to flux: %d, pipe_write = %d\n", packet->idFlux,
                    main_thr.pipes[packet->idFlux]);

        // send packet to flux using pipes (flux corresponding to packet->idFlux)
        return_value = write(main_thr.pipes[packet->idFlux], packet, 52);
        if (return_value < 0) {
            printf("Write failed for flux=%d, pipe fd=%d\n", packet->idFlux, main_thr.pipes[packet->idFlux]);
            raler("manager: write");
        }
        // Tant que la variable partagée "threadStatus" n'est pas terminée, on continue
        // Car dans ce thread, on ne sait pas si les flux sont tous terminés ou non.
    } while (*main_thr.thr_status != STOP);

    DEBUG_PRINT("doManager: main thread stopping...\n");

    destroyPacket(packet);
    pthread_exit(NULL);
}

/**
 * Démarre les threads pour chaque flux.
 * Multi-thread
 * @param socket TCP Socket
 * @param mode Mode de transmission
 * @param fluxes Tableau contenant des flux.
 * @param fluxCount Nb de flux dans le tableau
 */
void handle(tcp_t tcp, modeTCP_t mode, struct flux *fluxes, int nb_flux) {
    thread_status_t *thr_status = malloc(sizeof(thread_status_t));
    // list of all the threads id : manager + one for each flux
    pthread_t *thr_id = malloc(sizeof(pthread_t) * (nb_flux + 1));
    // list of all the threads related to fluxes : one for each flux
    struct flux_args fluxes_thr[nb_flux]; //= malloc(sizeof(struct flux_args) * nb_flux);
    // list of all the pipes : one for each flux, fluxes can communicate with the manager
    int **pipes = malloc(sizeof(int) * nb_flux);

    int write_pipes[nb_flux];

    // creates manager of all the fluxes
    struct manager main_thr;
    main_thr.tcp = tcp; // Socket receveur
    main_thr.nb_flux = nb_flux; // Nombre de flux
    main_thr.pipes = write_pipes;//malloc(sizeof(int) * nb_flux); // Tableaux avec les descripteurs des tubes
    main_thr.thr_status = thr_status; // Statut du thread (ne marche surement pas)

    // creates main thread (manager)
    DEBUG_PRINT("Start manager thread\n");
    pthread_create(&thr_id[0], NULL, (void *) doManager, (void *) &main_thr);

    // creates a thread for each flux
    for (int i = 0; i < nb_flux; i++) {
        struct flux flux = fluxes[i];
        //fluxes_thr[i] = malloc(sizeof(struct flux_args));
        fluxes_thr[i].tcp = tcp;
        fluxes_thr[i].idFlux = flux.fluxId;
        fluxes_thr[i].buf = malloc(flux.bufLen);
        fluxes_thr[i].bufLen = flux.bufLen;

        strcpy(fluxes_thr[i].buf, flux.buf);

        printf("create flux_thr for flux=%d; idFlux=%d\n", i , flux.fluxId);

        // open pipe for the thread (flux) to communicate with the manager
        pipes[i] = malloc(sizeof(int) * 2);
        if (pipe(pipes[i]) < 0) perror("pipe");

        // set pipes for manager thread and flux thread
        fluxes_thr[i].pipe_read = pipes[i][0];
        //main_thr.pipes[i] = pipes[i][1];
        write_pipes[i] = pipes[i][1];
        DEBUG_PRINT("Opened new pipe for flux=%d; read=%d, write=%d\n", i, pipes[i][0], pipes[i][1]);
    }

    for (int i = 1; i <= nb_flux; ++i) {
        // creates a thread, corresponding to a flux
        // different function, depending on the mode the user chose
        // argument : flux informations -> idFlux, buffer, pipe fd
        if (pthread_create(&thr_id[i], NULL, mode == GO_BACK_N ? (void *) doStopWait : (void *) doGoBackN,
                           (void*)&fluxes_thr[i - 1]) > 0) {
            perror("pthread");
        }
    }

    // waiting for each thread (flux) to end
    for (int i = 1; i <= nb_flux; ++i) {
        if (pthread_join(thr_id[i], NULL) > 0)
            perror("pthread_join");
    }

    DEBUG_PRINT("All flux threads stopped...\n");

    // Variable globale pour arrêter le thread de gestion
    // Fonctionne surement pas
    *thr_status = STOP;

    // waiting for the manager to end
    if (pthread_join(thr_id[0], NULL) > 0)
        perror("pthread_join");

    // END : close and free everything

    for (int i = 0; i < nb_flux; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
        free(pipes[i]);
    }

    free(thr_id);
    free(thr_status);
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

    modeTCP_t mode = parseMode(argv[1]);

    if (mode == UNKNOWN) {
        fprintf(stderr, "Usage: <mode> must be either 'stop-wait' or 'go-back-n'\n");
        exit(1);
    }

    // else

    char *ip = argv[2];
    int port_local = string_to_int(argv[3]);
    int port_medium = string_to_int(argv[4]);

    DEBUG_PRINT("\nMode chosen : %d\nDestination address : %s\nLocal port set at : %d\nDestination port set at : %d\n=================================\n", mode, ip, port_local, port_medium);

    tcp_t tcp = createTcp(ip, port_local, port_medium);

    int nbflux = 2;
    struct flux fluxes[nbflux];

    for (int i = 0; i < nbflux; ++i) {

        int spam = rand() % UINT8_MAX + UINT8_MAX;

        fluxes[i].buf = malloc(spam);

        for (int j = 0; j < spam; ++j) {
            fluxes[0].buf[j] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[random () % 26];
        }

        fluxes[i].bufLen = spam;
        fluxes[i].fluxId = i;
    }

    handle(tcp, mode, fluxes, nbflux);

    destroyTcp(tcp);

    return 0;
}