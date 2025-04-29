#define _XOPEN_SOURCE 500 //Used for sigaction
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <netinet/udp.h>

#define PORT 5555
#define MAXMSG 512

// State definitions
#define INIT 0
#define WAIT_FOR_SYN 1
#define WAIT_FOR_ACK 2
#define CONNECTED 3

// Message flags
#define SYN 0
#define SYNACK 1
#define DATA 2
#define DATAACK 3
#define DATANACK 4
#define FIN 5
#define FINACK 6

typedef struct messageToSend {
    int flag;
    int seqNr;
    int checkSum;
    char data[256];
} message;

// Global variables
int state = INIT;
int timerRunning = 0;
int newMessage = 0;
message messageRecvd;
message msgToSend = {0};
int sock;
struct sockaddr_in clientAddr;

// Function declarations
int checksumCalc(message msg);
int mySendTo(int sock, struct sockaddr* recvAddr, message* msg);

void start_timer(int duration) {
    if (!timerRunning) {
        struct itimerval timer;
        timer.it_value.tv_sec = duration;
        timer.it_value.tv_usec = 0;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        setitimer(ITIMER_REAL, &timer, NULL);
        timerRunning = 1;
    }
}

void stop_timer() {
    struct itimerval timer = {0};
    setitimer(ITIMER_REAL, &timer, NULL);
    timerRunning = 0;
}

void timeout_handler(int signum) {
    switch (state) {
        case WAIT_FOR_ACK:
            printf("SERVER: ACK TIMEOUT -> RESENDING SYNACK\n");
            msgToSend.checkSum = checksumCalc(msgToSend);
            mySendTo(sock, (struct sockaddr*)&clientAddr, &msgToSend);
            start_timer(3);
            break;
        default:
            printf("SERVER: Timeout occurred in unexpected state.\n");
    }
}

void* recieveThread(void* sockArg) {
    int sockFD = *((int*)sockArg);
    socklen_t len = sizeof(struct sockaddr_in);

    while (1) {
        if (newMessage == 0) {
            int bytesRecieved = recvfrom(sockFD, &messageRecvd, sizeof(message), 0,
                                         (struct sockaddr*)&clientAddr, &len);
            if (bytesRecieved < 0) {
                perror("recvfrom failed");
                continue;
            } else {
                newMessage = 1;
            }
        }
    }
    return NULL;
}

uint32_t checksumActualCalculation(const uint8_t* msg, uint32_t len) {
    uint32_t reg = 0;
    uint32_t poly = 0xEDB88320;

    for (uint32_t i = 0; i < len; ++i) {
        reg ^= msg[i];
        for (int j = 0; j < 8; ++j) {
            if (reg & 1)
                reg = (reg >> 1) ^ poly;
            else
                reg >>= 1;
        }
    }
    return reg;
}

int checksumCalc(message msg) {
    msg.checkSum = 0;
    return checksumActualCalculation((const uint8_t*)&msg, sizeof(message));
}

int mySendTo(int sock, struct sockaddr* recvAddr, message* msg) {
    int randVal = rand() % 100;
    int len;

    if (randVal < 10) {
        msg->checkSum--;
        len = sendto(sock, msg, sizeof(message), 0, recvAddr, sizeof(struct sockaddr_in));
        perror("sendto failed (corrupted)");
        printf("SERVER: Altering checksum\n");
    } else if (randVal < 20) {
        printf("SERVER: Simulated packet loss\n");
        return 0;
    } else {
        len = sendto(sock, msg, sizeof(message), 0, recvAddr, sizeof(struct sockaddr_in));
        if (len == -1) perror("sendto failed");
        printf("SERVER: Sent message normally\n");
    }
    return len;
}

int makeSocket(unsigned short int port) {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Could not create socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in name = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(sock, (struct sockaddr*)&name, sizeof(name)) < 0) {
        perror("Could not bind socket");
        exit(EXIT_FAILURE);
    }
    return sock;
}

void myconnect(int sock) {
    while (state != CONNECTED) {
        switch (state) {
            case INIT:
                printf("SERVER: INIT -> WAITING FOR SYN\n");
                state = WAIT_FOR_SYN;
                break;
            case WAIT_FOR_SYN:
                if (newMessage == 1 && messageRecvd.flag == SYN) {
                    newMessage = 0;
                    int recChecksum = messageRecvd.checkSum;
                    messageRecvd.checkSum = 0;
                    if (recChecksum == checksumCalc(messageRecvd)) {
                        printf("SERVER: Received valid SYN -> Sending SYNACK\n");
                        msgToSend.flag = SYNACK;
                        msgToSend.seqNr = 0;
                        msgToSend.checkSum = checksumCalc(msgToSend);
                        mySendTo(sock, (struct sockaddr*)&clientAddr, &msgToSend);
                        start_timer(3);
                        state = WAIT_FOR_ACK;
                    } else {
                        printf("SERVER: Invalid SYN checksum\n");
                    }
                }
                break;
            case WAIT_FOR_ACK:
                if (newMessage == 1 && messageRecvd.flag == DATAACK) {
                    newMessage = 0;
                    int recChecksum = messageRecvd.checkSum;
                    messageRecvd.checkSum = 0;
                    stop_timer();
                    if (recChecksum == checksumCalc(messageRecvd)) {
                        printf("SERVER: Received valid ACK -> Connection established\n");
                        state = CONNECTED;
                    } else {
                        printf("SERVER: Invalid ACK checksum\n");
                    }
                }
                break;
            default:
                printf("SERVER: Invalid state\n");
        }
    }
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    pthread_t recvt;

    sock = makeSocket(PORT);

    struct sigaction saTimeout = {.sa_handler = timeout_handler, .sa_flags = 0};
    sigaction(SIGALRM, &saTimeout, NULL);

    pthread_create(&recvt, NULL, recieveThread, &sock);
    myconnect(sock);

    return 0;
}
