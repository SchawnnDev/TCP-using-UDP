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

#define TIMEOUT 2000000
#define DEBUG 1

#if defined(DEBUG) && DEBUG > 0
#define DEBUG_PRINT(fmt, args...) printf(fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...) /* Don't do anything in release builds */
#endif

enum flux_status {
    DISCONNECTED = 0x0,
    WAITING_SYN_ACK = 0x1,
    ESTABLISHED = 0x2
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
    packet_t
    packets[UINT8_MAX];
    int congestionWindowSize;
    int lastReceivedACK;
    int ackCounter;
    int lastSentSeq;
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
    int **pipes;
    int nb_flux;
    thread_status_t *thr_status;
};

void *doGoBackN(void *arg)
{
    struct flux_args flux = *(struct flux_args *) arg;
    printf("%d\n", flux.idFlux);
    pthread_exit(NULL);
}

/**
 * Thread pour gérer un flux pour lire et renvoyer les paquets d'un flux
 * Dans le schéma du stop and wait
 * @param arg Structure contenant arguments
 * @return
 */
void *doStopWait(void *arg)
{
    // creates flux
    struct flux_args flux = *(struct flux_args *) arg;
    flux_status_t status = DISCONNECTED; // flux status by default

    // creates a packet
    packet_t packet = newPacket();
    packet_status_t packet_status = SEND_PACKET; // packet status by default

    // variables
    uint16_t numSeq = 0; // numSeq by default
    ssize_t return_value = -1; // error return
    fd_set working_set; // fd_set used for select
    char data[PACKET_DATA_SIZE]; // current data to send

    // set counters
    int nb_packets = flux.bufLen / PACKET_DATA_SIZE; // nb packets to send
    int nb_done_packets = 0; // nb packets already sent

    // set timeout : 500 ms
    struct timeval tv;

    do {

        if(status == WAITING_SYN_ACK)
        {
            if(return_value == 0) // if : timeout
                status = DISCONNECTED; // switch back to default status
            else if(return_value > 0)
            {
                // read packet received from manager trough pipe
                if (read(flux.pipe_read, packet, 52) != 52)
                    raler("read pipe");

                // expect : ACK SYN
                if (!(packet->type & ACK) || !(packet->type & SYN))
                    status = DISCONNECTED; // switch back to default status
                else {
                    /*int b = packet->numSequence;
                    packet->numSequence = numSeq + 1;
                    packet->numAcquittement = b + 1;*/

                    // packet->numSequence reste le même non ?
                    packet->type = ACK;
                    packet->numAcquittement = packet->numSequence + 1;
                    sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
                    status = ESTABLISHED;
                }
            }
        }

        if(status == DISCONNECTED) { // send SYN, numSeq = random, numAcq = 0
            numSeq = rand() % (UINT16_MAX / 2);
            setPacket(packet, 0, SYN, numSeq, 0, ECN_DISABLED, 52, "");
            sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
            status = WAITING_SYN_ACK; // switch status
        }

        if(status == ESTABLISHED)
        {
            // if : packet has been sent, waiting for his ACK
            if (packet_status == WAIT_ACK) {
                if (return_value == 0) // if : timeout
                    packet_status = RESEND_PACKET; // switch packet status, we want to resend a packet
                else if (return_value > 0) {

                    DEBUG_PRINT("doStopWait: ret > 0\n");

                    // read packet received from manager trough pipe
                    if (read(flux.pipe_read, packet, 52) != 52)
                        raler("read pipe");

                    DEBUG_PRINT("doStopWait: Flux thread = %d, go packet, ack = %d, seqNum = %d, type = %s \n", flux.idFlux, packet->numAcquittement, packet->numSequence, packet->type & ACK ? "ACK" : "Other");

                    // AJOUTER ICI CHECK SI SYN-ACK POUR RENVOI ACK AU SERVEUR
                    // (gestion du 3way)

                    // check : is this an ACK ?
                    if (!(packet->type & ACK) || packet->numAcquittement != numSeq)
                        packet_status = RESEND_PACKET; // switch packet status, we want to resend a packet
                    else
                    {
                        nb_done_packets++; // packet is done
                        packet_status = SEND_PACKET; // next up, we want to send another packet

                        // if every packet has been sent, we are done here
                        if (nb_done_packets >= nb_packets)
                            break;
                    }
                }
            }

            // Si le statut est d'envoyer un paquet,
            // Alors on change le numéro de séquence,
            // On y ajoute les données en question
            // en fonction du numéro de paquet que l'on envoie
            if (packet_status == SEND_PACKET) {
                numSeq = numSeq == 0 ? 1 : 0;

                // Ici on lit dans le buffer du flux,
                int fromEnd = (nb_done_packets + 1) * PACKET_DATA_SIZE;

                // on vérifie que le dernier buffer n'est pas plus grand que le buffer.
                if (fromEnd > flux.bufLen)
                    fromEnd = flux.bufLen - fromEnd;

                substr(flux.buf, data, nb_done_packets * PACKET_DATA_SIZE, fromEnd);
            }

            DEBUG_PRINT("HSW: Send packet fluxid=%d, status=%s\n", flux.idFlux, packet_status == SEND_PACKET ? "Send packet" : (packet_status == RESEND_PACKET ? "Resend packet" : "Wait ACK"));
            // On set le paquet en question,
            // puis on l'envoie
            setPacket(packet, flux.idFlux, 0, numSeq, 0, 0, 0, data);
            sendPacket(flux.tcp->outSocket, packet, flux.tcp->sockaddr);
            // on met le mode du thread en WAIT ACK
            packet_status = WAIT_ACK;

        }

        FD_ZERO(&working_set);
        FD_SET(flux.pipe_read, &working_set);

        DEBUG_PRINT("doStopWait: Select %d and wait sec = %ld, usec = %ld\n", flux.pipe_read, tv.tv_sec, tv.tv_usec);

        // timeout
        tv.tv_sec = 0;
        tv.tv_usec = TIMEOUT;

        // waiting for a packet to be send from the manager (trough pipe)
        return_value = select(flux.pipe_read + 1, &working_set, NULL, NULL, &tv);

        if (return_value == -1) raler("select ici\n");

        DEBUG_PRINT("doStopWait: Fin boucle\n");

    } while (1);

    pthread_exit(NULL);
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

    do {
        /* receive packet */
        return_value = recvfrom(main_thr.tcp->inSocket, packet, 52, 0, NULL, NULL);
        DEBUG_PRINT("doManager: recvfrom socket = %d\n", main_thr.tcp->inSocket);

        // S'il y'a un timeout on ignore, car les timeouts sont gérés indépendamment
        if (return_value < 0) // timeout
            continue;
        DEBUG_PRINT("doManager: no timeout\n");

        // check : idFlux exists
        if (packet->idFlux >= main_thr.nb_flux)
            continue;
        DEBUG_PRINT("doManager: write to flux: %d, pipe_write = %d\n", packet->idFlux, main_thr.pipes[packet->idFlux][1]);

        // send packet to flux using pipes (flux corresponding to packet->idFlux)
        return_value = write(main_thr.pipes[packet->idFlux][1], packet, 52);
        if (return_value < 0)
            perror("write");

        // Tant que la variable partagée "threadStatus" n'est pas terminée, on continue
        // Car dans ce thread, on ne sait pas si les flux sont tous terminés ou non.
    } while (*main_thr.thr_status != STOP);

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
void handle(tcp_t tcp, modeTCP_t mode, flux_t *fluxes, int nb_flux)
{
    thread_status_t thr_status = START;
    // list of all the threads id : manager + one for each flux
    pthread_t *thr_id = malloc(sizeof(pthread_t) * (nb_flux + 1));
    // list of all the threads related to fluxes : one for each flux
    struct flux_args *fluxes_thr = malloc(sizeof(struct flux_args) * nb_flux);
    // list of all the pipes : one for each flux, fluxes can communicat with the manager
    int **pipes = malloc(sizeof(int) * nb_flux);

    // creates manager of all the fluxes
    struct manager main_thr;
    main_thr.tcp = tcp; // Socket receveur
    main_thr.nb_flux = nb_flux; // Nombre de flux
    main_thr.pipes = pipes; // Tableaux avec les descripteurs des tubes
    main_thr.thr_status = &thr_status; // Statut du thread (ne marche surement pas)

    // creates main thread (manager)
    DEBUG_PRINT("Start manager thread\n");
    pthread_create(&thr_id[0], NULL, (void*) doManager, (void*) &main_thr);

    // creates a thread for each flux
    for (int i = 1; i <= nb_flux; ++i)
    {
        flux_t flux = fluxes[i - 1];
        fluxes_thr[i].tcp = tcp;
        fluxes_thr[i].idFlux = flux->fluxId;
        // fluxes_thr[i].status = &threadStatus;
        DEBUG_PRINT("handle, fluxid=%d\n", flux->fluxId);

        fluxes_thr[i].bufLen = flux->bufLen;
        fluxes_thr[i].buf = malloc(flux->bufLen);
        strcpy(fluxes_thr[i].buf, flux->buf);

        // open pipe for the thread (flux) to communicate with the manager
        pipes[i - 1] = malloc(sizeof(int) * 2);
        if (pipe(pipes[i - 1]) < 0) perror("pipe");
        fluxes_thr[i].pipe_read = pipes[i - 1][0];
        DEBUG_PRINT("Opened new pipe for flux=%d; read=%d, write=%d\n", i - 1, pipes[i - 1][0], pipes[i - 1][1]);

        // creates a thread, corresponding to a flux
        // different function, depending on the mode the user chose
        // argument : flux informations -> idFlux, buffer, pipe fd
        if (pthread_create(&thr_id[i], NULL, mode == STOP_AND_WAIT ? (void*) doStopWait : (void*) doGoBackN, &fluxes_thr[i]) > 0) {
            perror("pthread");
        }
    }

    // waiting for each thread (flux) to end
    for (int i = 1; i <= nb_flux; ++i) {
        if (pthread_join(thr_id[i], NULL) > 0)
            perror("pthread_join");
    }

    // Variable globale pour arrêter le thread de gestion
    // Fonctionne surement pas
    thr_status = STOP;

    // waiting for the manager to end
    if (pthread_join(thr_id[0], NULL) > 0)
        perror("pthread_join");

    // END : close and free everything

    for (int i = 0; i < nb_flux; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
        free(pipes[i]);
    }

    free(pipes);
    free(fluxes_thr);
    free(thr_id);
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

    printf("---------------\n");
    printf("Mode chosen : %d\n", mode);
    printf("Destination address : %s\n", ip);
    printf("Local port set at : %d\n", port_local);
    printf("Destination port set at : %d\n", port_medium);
    printf("---------------\n");

    tcp_t tcp = createTcp(ip, port_local, port_medium);

    flux_t fluxes[1];
    fluxes[0] = malloc(sizeof(struct flux));
    fluxes[0]->buf = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    fluxes[0]->bufLen = 62;
    fluxes[0]->fluxId = 0;
    handle(tcp, STOP_AND_WAIT, fluxes, 1);

    destroyTcp(tcp);

    return 0;
}