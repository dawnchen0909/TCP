#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include "lib/circular_buffer.h"


struct sockaddr_in si_me, si_other;
int s, slen;
uint32_t expected_seq = 0;
void diep(char *s) {
    perror(s);
    exit(1);
}

void send_ack(int socket, uint32_t seq) {
    seq = htonl(seq);
     sendto(s, &seq, sizeof(seq), MSG_CONFIRM, (const struct sockaddr *) &si_other, sizeof(si_other));

}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    
    slen = sizeof (si_other);


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");
    puts("Bind success");

	/* Now receive data and send acknowledgements */    
    FILE *f = fopen(destinationFile, "wb");
    size_t len = sizeof(si_other); 
    char buffer[2000];
    while (1) {
        int n = recvfrom(s, buffer, sizeof(buffer), 
            0, (struct sockaddr*)&si_other, &len);
        if (n == 4) {
            puts("goodbye");
            send_ack(s, expected_seq);
            break;
        }
        printf("%d bytes of msg received\n", n);
        uint32_t seq;
        memcpy(&seq, buffer, sizeof(seq));
        seq = htonl(seq);
        printf("seq: %u, expected_seq: %u\n", seq, expected_seq);
        if (seq == expected_seq) {
            fwrite(buffer + sizeof(uint32_t), (size_t)n - sizeof(uint32_t), 1, f);
            expected_seq += (size_t)n - sizeof(uint32_t);
        }
        send_ack(s, expected_seq);
    }
    close(s);
		printf("%s received.", destinationFile);
    fclose(f);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}