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
#include "../../headers/global/packet.h"

/********************************
 * Handle errors
 * *******************************/
noreturn void raler(char *message) {
    perror(message);
    exit(1);
}

/********************************
 * Cast a string into an int
 * *******************************/
int string_to_int(char *arg) {
    // variables
    char *endptr, *str;
    str = arg;

    errno = 0;
    long N = strtol(str, &endptr, 10);

    // check : error
    if ((errno == ERANGE && (N == LONG_MAX || N == LONG_MIN))
    || (errno != 0 && N == 0)) {
        raler("strtol");
    }

    // if : not found
    if (endptr == str)
        raler("string_to_int nothing found");

    // cast to int (use of signed values later)
    return (int) N;
}

/********************************
 * Creates a socket
 * *******************************/
int createSocket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sock == -1)
        raler("Create socket");

    return sock;
}

/********************************
 * Closes a socket
 * *******************************/
void closeSocket(int sock) {
    if(close(sock) < 0)
        raler("socket");
}

void sendPacket(int socket, packet_t packet, struct sockaddr *sockaddr) {
    char* bytes_arr = malloc(packet->tailleFenetre);
    memcpy(bytes_arr, packet, packet->tailleFenetre);

    if (sendto(socket, bytes_arr, packet->tailleFenetre, 0, sockaddr, sizeof(*sockaddr)) == -1) {
        close(socket);
        raler("sendto");
    }
}

void proceedHandshake(int socket) {

}

void source(char *mode, char *ip, int port_local, int port_medium) {
    int sock = createSocket();

    struct in_addr addr;

    if (inet_aton(ip, &addr) == 0)
    {
        close(sock);
        raler("aton");
    }

    struct sockaddr_in sockAddr;
    memset(&sockAddr, 0, sizeof(sockAddr));

    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(port_medium);
    sockAddr.sin_addr.s_addr = addr.s_addr;

    struct sockaddr *sockaddr = (struct sockaddr *) &sockAddr;

    packet_t packet = createPacket(0, SYN, 22, 20, ECN_DISABLED, 52, "bite");
    sendPacket(sock, packet, sockaddr);
    free(packet);

    closeSocket(sock);
}

/********************************
 * Main program
 * *******************************/
int main(int argc, char *argv[]) {

    // if : args unvalid

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <mode> <IP_distante> <port_local> <port_ecoute_src_pertubateur>\n", argv[0]);
        exit(1);
    } else if (strcmp(argv[1], "stop-wait") != 0 && strcmp(argv[1], "go-back-n") != 0) {
        fprintf(stderr, "Usage: <mode> must be either 'stop-wait' or 'go-back-n'\n");
        exit(1);
    }

    // else

    printf("---------------\n");
    printf("Mode chosen : %s\n", argv[1]);
    printf("Destination adress : %s\n", argv[2]);
    printf("Local port set at : %s\n", argv[3]);
    printf("Destination port set at : %s\n", argv[4]);
    printf("---------------\n");

    //source(argv[1], argv[2], string_to_int(argv[3]), string_to_int(argv[4]));

    char* ipDistante = argv[2];
    int portDistant = string_to_int(argv[4]);
    int sock = createSocket();

    struct in_addr addr;

    if (inet_aton(ipDistante, &addr) == 0)
    {
        close(sock);
        raler("aton");
    }

    struct sockaddr_in sockAddr;
    memset(&sockAddr, 0, sizeof(sockAddr));

    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(portDistant);
    sockAddr.sin_addr.s_addr = addr.s_addr;

    struct sockaddr *sockaddr = (struct sockaddr *) &sockAddr;

    packet_t packet = createPacket(0, SYN, 22, 20, ECN_DISABLED, 52, "bite");
    sendPacket(sock, packet, sockaddr);
    free(packet);

    closeSocket(sock);

    return 0;
}