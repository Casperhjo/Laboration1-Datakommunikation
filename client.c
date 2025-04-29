#define _XOPEN_SOURCE 500 //Used for sigaction
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <netinet/udp.h>
#include <signal.h>
#include <sys/time.h>

#define PORT 5555
#define hostNameLength 50

// Define state machine states
#define INIT 0
#define WAIT_FOR_SYNACK 2
#define SEND_ACK 3
#define CONNECTED 4
#define WAIT_FOR_FINACK 5
#define DISCONNECTED 6
#define WAIT_GBN 7
#define WAIT_SR 8


//Message flags
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
int windowSize = 0;
int windowBase = 0;
int nextPacket = 0;

int timerRunning = 0;
int newMessage = 0;
message messageRecvd;
message msgToSend = { 0 };

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
    case WAIT_FOR_SYNACK:
      stop_timer();
      printf("CLIENT: SYNACK TIMEOUT -> SENDING SYN\n");
      msgToSend.flag = SYN;
      msgToSend.seqNr = 0;
      msgToSend.checkSum = checksumCalc(msgToSend);
      mySendTo(sock, (struct sockaddr*)&clientAddr, &msgToSend);
      start_timer(3);
      break;
    case WAIT_FOR_FINACK:
        stop_timer();
        printf("CLIENT: FIN TIMEOUT -> SENDING FIN\n");

        msgToSend.flag = FIN;
        msgToSend.seqNr = 0;
        msgToSend.checkSum = checksumCalc(msgToSend);

        mySendTo(sock, (struct sockaddr*)clientAddr, &msgToSend);
        start_timer(3);

        break;
    case WAIT_GBN:
        nextPacket = windowBase;
        break;
    default:
        printf("Invalid option\n");
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
        perror("Reading message didn't work\n");
        continue;
      } else {
        newMessage = 1;
      }
    }
  }
  return NULL;
}

uint32_t checksumActualCalculation(const uint8_t* messageToSend, uint32_t messageLength) {
  uint32_t reg = 0;
  uint32_t poly = 0xEDB88320;
  for (uint32_t i = 0; i < messageLength; ++i) {
    reg ^= messageToSend[i];
    for (uint32_t j = 0; j < 8; ++j) {
      if (reg & 1) reg = (reg >> 1) ^ poly;
      else reg >>= 1;
    }
  }
  return reg;
}

int checksumCalc(message m) {
  m.checkSum = 0;
  return checksumActualCalculation((const uint8_t*)&m, sizeof(message));
}

int mySendTo(int sock, struct sockaddr* recvAddr, message* msg) {
  int r = rand() % 100;
  int len = 0;
  if (r <= 9) {
    msg->checkSum--;
    len = sendto(sock, msg, sizeof(message), 0, recvAddr, sizeof(struct sockaddr_in));
    perror("sendto failed");
    printf("Altering checksum\n");
  } else if (r < 20) {
    printf("Didn't send a packet at all\n");
  } else {
    len = sendto(sock, msg, sizeof(message), 0, recvAddr, sizeof(struct sockaddr_in));
    if (len == -1) perror("sendto failed");
    printf("Sent like normal\n");
  }
  return len;
}

void myconnect(int sock, struct sockaddr_in* clientAddr) {
  while (state != CONNECTED) {
    switch (state) {
      case INIT:
        printf("CLIENT: INIT -> SENDING SYN\n");
        msgToSend.flag = SYN;
        msgToSend.seqNr = 0;
        msgToSend.checkSum = checksumCalc(msgToSend);
        mySendTo(sock, (struct sockaddr*)clientAddr, &msgToSend);
        start_timer(3);
        state = WAIT_FOR_SYNACK;
        break;
      case WAIT_FOR_SYNACK:
        if (newMessage == 1 && messageRecvd.flag == SYNACK) {
          newMessage = 0;
          int receivedChecksum = messageRecvd.checkSum;
          messageRecvd.checkSum = 0;
          int calculatedChecksum = checksumCalc(messageRecvd);
          if (receivedChecksum == calculatedChecksum) {
            printf("CLIENT: WAIT_FOR_SYNACK -> SENDING ACK\n");
            msgToSend.flag = DATAACK;
            msgToSend.seqNr = 0;
            msgToSend.checkSum = checksumCalc(msgToSend);
            mySendTo(sock, (struct sockaddr*)clientAddr, &msgToSend);
            state = CONNECTED;
          } else {
            printf("CLIENT: Checksum mismatch - ignoring packet\n");
          }
        }
        break;
      default:
        printf("Invalid connect state\n");
    }
  }
}

void transmit(int sock, struct sockaddr_in* clientAddr, char[] dataToSend)//Add input parameters if needed
{

    /*Implement the sliding window state machine*/

    //local variables if needed
    int[8] ackBuffer;
    nrBuffered = 0;
    start_timer(3);

    //Loop switch-case
    while (1) //Add the condition to leave the state machine
    {
        switch (state)
        {
        case WAIT_GBN:
            if (next >= windowBase && next <= (windowBase + windowSize))      // sending packages
            {
                printf("CLIENT: GBN -> SENDING package nr:%n\n", &nextPacket);

                msgToSend.flag = DATA;
                msgToSend.seqNr = nextPacket;
                msgToSend.data = dataToSend[nextPacket++];
                msgToSend.checkSum = checksumCalc(msgToSend);

                mySendTo(sock, (struct sockaddr*)&clientAddr);
            }
            else if (newMessage == 1 && messageRecvd.flag == DATAACK)     // new message
            {
                if (messageRecvd.seqNr == windowBase)
                {
                    printf("CLIENT: GBN -> RECEIVED package nr:%n\n", &messageRecvd.seqNr);
                    windowBase++;
                    stop_timer;
                    start_timer(3);
                }
                else if (messageRecvd.seqNr > windowBase)
                    ackBuffer[nrBuffered++] = messageRecvd.seqNr;    // add an out of order ACK to the buffer
                newMessage = 0;
            }
            else if (nrBuffered > 0)      // check the buffer
            {
                for (int i = 0; i < 8; i++)
                {
                    if (ackBuffer[i] == windowBase)
                    {
                        windowBase++;
                        stop_timer;
                        start_timer(3);
                        ackBuffer[i] = default;
                        nrBuffered--;
                    }
                    else if (ackBuffer[i] < windowBase)
                    {
                        ackBuffer[i] = default;
                        nrBuffered--;
                    }
                }
            }
            break;
        case WAIT_SR:
            if (next >= windowBase && next <= (windowBase + windowSize))      // sending packages
            {
                printf("CLIENT: SR -> SENDING package nr:%n\n", &nextPacket);

                msgToSend.flag = DATA;
                msgToSend.seqNr = nextPacket;
                msgToSend.data = dataToSend[nextPacket++];
                msgToSend.checkSum = checksumCalc(msgToSend);

                mySendTo(sock, (struct sockaddr*)&clientAddr);
                start_timer(3);
            }
            else if (newMessage == 1)     // new message
            {
                if (messageRecvd.seqNr == windowBase && messageRecvd.flag == DATAACK)
                {
                    stop_timer;
                    printf("CLIENT: GBN -> RECEIVED package nr:%n\n", &messageRecvd.seqNr);
                    windowBase++;
                    start_timer(3);
                }
                else if (messageRecvd.seqNr > windowBase)
                    ackBuffer[nrBuffered++] = messageRecvd.seqNr;    // add an out of order ACK to the buffer
                newMessage = 0;
            }
            else if (nrBuffered > 0)      // check the buffer
            {
                for (int i = 0; i < 8; i++)
                {
                    if (ackBuffer[i] == windowBase)
                    {
                        windowBase++;
                        stop_timer;
                        start_timer(3);
                        ackBuffer[i] = default;
                        nrBuffered--;
                    }
                    else if (ackBuffer[i] < windowBase)
                    {
                        ackBuffer[i] = default;
                        nrBuffered--;
                    }
                }
            }
            break;
        default:
            printf("Invalid option\n");
        }
    }
}

void disconnect(int sock, struct sockaddr_in* clientAddr)//Add input parameters if needed
{

    /*Implement the three-way handshake state machine
    for the teardown*/

    //local variables if needed
    int nrTimeouts = 0;

    //Loop switch-case
    while (1) //Add the condition to leave the state machine
    {
        switch (state)
        {
        case INIT:
            printf("CLIENT: INIT -> SENDING FIN\n");

            msgToSend.flag = FIN;
            msgToSend.seqNr = 0;
            msgToSend.checkSum = checksumCalc(msgToSend);

            mySendTo(sock, (struct sockaddr*)&clientAddr);
            start_timer(3);

            state = WAIT_FOR_FINACK;
            break;
        case WAIT_FOR_FINACK:
            printf("CLIENT: WAIT_FOR_FINACK -> TERMINATING CONNECTION\n");

            msgToSend.flag = DATAACK;
            msgToSend.seqNr = 0;
            msgToSend.checkSum = checksumCalc(msgToSend);

            mySendTo(sock, (struct sockaddr*)&clientAddr);

            state = INIT;
            break;

        default:
            printf("Invalid option\n");
        }
    }
}

int main(int argc, char *argv[]) {
  struct sockaddr_in clientSock = {0};
  char hostName[hostNameLength];
  pthread_t recvt;

  srand(time(NULL));

  char dstHost[] = "127.0.0.1";
  int dstUdpPort = PORT;

  struct sigaction saTimeout;
  saTimeout.sa_handler = timeout_handler;
  saTimeout.sa_flags = 0;
  sigaction(SIGALRM, &saTimeout, NULL);

  if (argc < 2) {
    fprintf(stderr, "Usage: %s [host name]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  strncpy(hostName, argv[1], hostNameLength);
  hostName[hostNameLength - 1] = '\0';

  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("Can't create UDP socket");
    exit(EXIT_FAILURE);
  }

  if (bind(sock, (struct sockaddr *)&clientSock, sizeof(clientSock)) < 0) {
    perror("Could not bind a name to the socket");
    exit(EXIT_FAILURE);
  }

  pthread_create(&recvt, NULL, recieveThread, &sock);

  clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(dstUdpPort);
  clientAddr.sin_addr.s_addr = inet_addr(dstHost);


  myconnect(sock, &clientAddr);//Add arguments if needed
  transmit(sock, &clientAddr);//Add arguments if needed
  disconnect(sock, &clientAddr);//Add arguments if needed


  return 0;
}

