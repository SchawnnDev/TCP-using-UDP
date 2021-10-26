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

int main() {

    int port = 5555;
    int s ;
    ssize_t r;

    struct sockaddr_in myAddr;
    myAddr.sin_family = AF_INET;
    myAddr.sin_port = port;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        perror("socket: ");
        exit(EXIT_FAILURE);
    }

    struct sockaddr* aPt = (struct sockaddr*) &myAddr;

    if (bind(s, aPt, sizeof(myAddr)) == -1)
    {
        perror("bind: ");
        exit(EXIT_FAILURE);
    }

    // printf("\n%d\n\n", a.sin_port);
    // sleep(20);

    struct sockaddr incoming;
    socklen_t addrlen = sizeof(incoming);
    char buffer[256];

    while(1)
    {
        if ((r = recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr*) &incoming, &addrlen)) == -1)
        {
            perror("recvfrom: ");
            exit(EXIT_FAILURE);
        }

        printf("%s\n", buffer);
    }

    return 0;
}
