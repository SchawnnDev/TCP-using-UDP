#include <stdnoreturn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#include "../../headers/global/utils.h"
#include "../../headers/global/packet.h"
#include "../../headers/global/socket_utils.h"

/********************************
 * Program
 * *******************************/
void destination(char *address, int port_local, int port_medium) {

    printf("Address : %s\nLocal : %d\n", address, port_medium);
    printf("---------------\n");
    int inSocket = createSocket();

    inSocket = prepareRecvSocket(inSocket, port_local);
    packet_t packet = recvPacket(inSocket, 52);
    showPacket(packet);
    destroyPacket(packet);

    closeSocket(inSocket);
}

/********************************
 * Main program
 * *******************************/
int main(int argc, char *argv[]) {

    // if : args unvalid

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <IP_distante> <port_local> <port_ecoute_dst_pertubateur>\n", argv[0]);
        exit(1);
    }

    // else

    printf("---------------\n");
    printf("Destination address : %s\n", argv[1]);
    printf("Local port set at : %s\n", argv[2]);
    printf("Destination port set at : %s\n", argv[3]);
    printf("---------------\n");

    destination(argv[1], string_to_int(argv[2]), string_to_int(argv[3]));

    return 0;
}
