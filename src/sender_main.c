#include <stddef.h>
#include <stdint.h>
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
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include "lib/circular_buffer.h"
#define MIN(a, b) (a < b ? a : b)
#define TIMEOUT_ITNERVAL 1 
void send_buffer(int port, char *buf, size_t size);
void add_to_window(char *buf, size_t size);
void sender_thread();
void ack_receiver_thread();
void timer_thread(); 
const int MAX_DATA_SIZE = 1950;
const size_t HEADER_SIZE = sizeof(uint32_t);
int finished = 0;
int goodbye = 0;
uint32_t dup_seq = -1;
int dup_count = 0;
pthread_t timer_tid = -1;
int timer_exist = 0;

struct sockaddr_in si_other;
int s, slen;

typedef struct {
    uint32_t seq;
    size_t data_size;
    char *data;
    char sent;
    char need_resend;
    char acked;
} rdt_packet_t;

typedef struct {
    size_t max_size;
    size_t window_size;
    size_t size;
    size_t batch_size;
    circular_buffer *cir_buf;
    uint32_t base;
    uint32_t next_seq;
    pthread_mutex_t lock;
    pthread_cond_t cv;
    char timeout;
    int processed;
} global_window;

global_window window;

void init() {
    window.max_size = 200;
    window.window_size = 3;
    window.size = 0;
    window.cir_buf = calloc(1, sizeof(circular_buffer));
    window.base = 0;
    window.next_seq = 0;
    window.timeout = 0;
    window.processed = 0;
    window.batch_size = 0;
    cb_init(window.cir_buf, window.max_size, sizeof(rdt_packet_t *));
    pthread_mutex_init(&window.lock, NULL);
    pthread_cond_init(&window.cv, NULL);
}

void diep(char *s) {
    perror(s);
    exit(1);
}

size_t get_size(FILE *fp) {
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return size;
}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */
    // unsigned long long int file_size = get_size(fp);
    unsigned long long bytes_size = bytesToTransfer;

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }


	/* Send data and receive acknowledgements on s*/
    init();
    pthread_t packet_sender_tid, ack_receiver_tid;
    pthread_create(&ack_receiver_tid, NULL, ack_receiver_thread, NULL);
    char buffer[MAX_DATA_SIZE];
    while (bytes_size) {
       
        
        pthread_mutex_lock(&window.lock);
        printf("%lu bytes remaining size %u window size %u\n", bytes_size, window.size, window.window_size);
        while (window.size < window.window_size && bytes_size) {
            size_t to_send = MIN(bytes_size, MAX_DATA_SIZE);
            size_t ret = fread(buffer, 1, to_send, fp);
            if (ret != to_send) {
                bytes_size = ret;
                to_send = ret;   
            }
            puts("Push data to window");
            add_to_window(buffer, to_send);
            bytes_size -= to_send; 
        }
        puts("Data added release lock");
        pthread_cond_broadcast(&window.cv);
        pthread_mutex_unlock(&window.lock);
        send_current_window();
    }
    finished = 1;
    add_to_window(buffer, 0);
    send_current_window();
    // pthread_join(packet_sender_tid, NULL);

    printf("Closing the socket\n");
    close(s);
    return;

}

void send_current_window() {
    printf("Sending current window of %u packets\n", window.size);
    pthread_mutex_lock(&window.lock);
    window.processed = 0;
    window.timeout = 0;
    while (!cb_is_empty(window.cir_buf)) {
        // If processed, don't busy wait
        while (window.processed) {
            pthread_cond_wait(&window.cv, &window.lock);
            if (window.size == 0) {
                if (window.timeout == 0) {
                    puts("not time out");
                    window.window_size += 1;
                } else {
                    window.window_size = (window.window_size + 1) / 2;
                }
                pthread_mutex_unlock(&window.lock);
                return;
            }
        }
        node *cur = cb_head(window.cir_buf);
        for (size_t i = 0; i < window.size; i++) {
            rdt_packet_t *packet = (rdt_packet_t *)cur->data;
            printf("Send packet at %p of size %u seq %d\n", packet, packet->data_size, packet->seq);
            sendto(s, packet->data, packet->data_size + HEADER_SIZE, MSG_CONFIRM, (const struct sockaddr *) &si_other, sizeof(si_other)); 
            cur = cur->next;
        }
        window.processed = 1;
        if (!timer_exist) {
            pthread_create(&timer_tid, NULL, timer_thread, NULL);
            timer_exist = 1;
        }
    }
    if (window.timeout == 0) {
        puts("not time out");
        window.window_size += 2;
    } else {
        window.window_size = (window.window_size + 1) / 2;
    }
    pthread_mutex_unlock(&window.lock);
    puts("Sending finished");
}


rdt_packet_t *packet_init(uint32_t seq, size_t size, char *buf) {
    rdt_packet_t *packet = malloc(sizeof(rdt_packet_t));
    packet->seq = seq;
    packet->data_size = size;
    packet->data = calloc(size + HEADER_SIZE, 1);
    packet->sent = 0;
    packet->acked = 0;
    packet->need_resend = 0;
    size_t net_seq = htonl(seq);
    memcpy(packet->data, &net_seq, sizeof(net_seq));
    memcpy(packet->data + HEADER_SIZE, buf, size);
    return packet;
}

void add_to_window(char *buf, size_t size) {
    rdt_packet_t *packet = packet_init(window.next_seq, size, buf);
    printf("Packet at %p with seq %u is to be added to window\n", packet, window.next_seq);
    window.next_seq += size;
    window.size ++; 
    cb_push_back(window.cir_buf, packet);
    window.processed = 0;
}

void sender_thread() {
    puts("Sender thread started");
    pthread_mutex_lock(&window.lock);
    while (!finished || !cb_is_empty(window.cir_buf)) {
        while (cb_is_empty(window.cir_buf) || window.processed) {
            if (!timer_exist && !cb_is_empty(window.cir_buf)) {
                pthread_create(&timer_tid, NULL, timer_thread, NULL);
                timer_exist = 1;
                puts("Create timer");
            }
            if (finished && goodbye) {
                pthread_mutex_unlock(&window.lock);
                puts("Sender thread exits");
                return; 
            }
            puts("No data in or all processed cb sleep");
            pthread_cond_wait(&window.cv, &window.lock);
        }
        // Try to send any new packet received by window. 
        // A linear scan
        
        // node *cur = cb_head(window.cir_buf);
        // while (cur != cb_tail(window.cir_buf)) {
        //     // printf("Window size %d max size %d\n\n", window.size, window.window_size);
        //     rdt_packet_t *packet = (rdt_packet_t *)cur->data;
        //     if (cur == cb_head(window.cir_buf)) {
        //         if (!timer_exist) {
        //             puts("start timer");
        //             pthread_create(&timer_tid, NULL, timer_thread, NULL);
        //             timer_exist = 1;
        //         }
        //     }
        //     if (!packet->sent || packet->need_resend) {
        //         printf("Send packet at %p of size %u seq %d\n", packet, packet->data_size, packet->seq);
                
        //         sendto(s, packet->data, packet->data_size + HEADER_SIZE, MSG_CONFIRM, (const struct sockaddr *) &si_other, sizeof(si_other));
        //         packet->sent = 1;
        //         packet->need_resend = 0;
        //     }
        //     cur = cur->next;
        // }
        // if (window.batch_size == 0) {
        //     window.
        // }
        // node *cur = 
        // for (size_t i = 0; i < window.window_size; i++) {

        // }
        // window.processed = 1;
    }
    pthread_mutex_unlock(&window.lock);
    puts("Sender thread exits");
}

void timer_thread() {
    while (1) {
        sleep(TIMEOUT_ITNERVAL);
        puts("TIMEOUT");
        pthread_mutex_lock(&window.lock);
        window.timeout = 1;
        window.processed = 0;
        // window.window_size = (window.window_size + 1) / 2;
        pthread_cond_broadcast(&window.cv);
        pthread_mutex_unlock(&window.lock);
    }
    
}

void ack_receiver_thread() {
    puts("Ack_receiver_thread started");
    size_t seq;
    while (!finished || !cb_is_empty(window.cir_buf)) {
        int n = recvfrom(s, &seq, sizeof(seq), 
            0, NULL, NULL);
        seq = htonl(seq);
        printf("Ack for %u received\n", seq);
        pthread_mutex_lock(&window.lock);
        node *cur = cb_head(window.cir_buf);
        int one_pop = 0;
        for (size_t i = 0; i < window.size; i++) {
            rdt_packet_t *packet = (rdt_packet_t *)cur->data; 
        // printf("Expected ack seq is %u\n", packet->seq + packet->data_size);
            if (packet->seq + packet->data_size <= seq) {
                one_pop = 1;
                printf("Good ack, pop head\n");
                if (timer_exist) {
                    pthread_cancel(timer_tid);
                    puts("timer cancel");
                    timer_exist = 0;
                }
                if (packet->data_size > 0)
                    free(packet->data);
                free(packet);
                cur = cur->next;
                cb_pop_front(window.cir_buf);
                window.size --; 
            }
        }
        if (!one_pop) {
            if (seq == dup_seq) {
                    dup_count++;
                } else {
                    dup_seq = seq;
                    dup_count = 1;
                }
                if (dup_count >= 3) {
                    puts("Dup ack detected");
                    dup_count = 0;
                    node *cur = cb_head(window.cir_buf);
                    if (timer_exist) {
                        pthread_cancel(timer_tid);
                        timer_exist = 0;
                    }
                    window.processed = 0;
                }
        }
        if (window.size > 0 && !timer_exist) {
            pthread_create(&timer_tid, NULL, timer_thread, NULL);
            timer_exist = 1;
        }
        pthread_cond_broadcast(&window.cv);
        pthread_mutex_unlock(&window.lock);    
        puts("Receiver lock released");
    }
    pthread_cond_broadcast(&window.cv);
    puts("Ack receiver thread exits");
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}