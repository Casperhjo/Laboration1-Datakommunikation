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


//Define state machine states here, e.g.:
#define INIT 0
#define WAIT_FOR_SYNACK 2
#define SEND_ACK 3
#define CONNECTED 4
#define WAIT_FOR_FINACK 5
#define DISCONNECTED 6



//Message flags
#define SYN 0
#define SYNACK 1
#define DATA 2
#define DATAACK 3
#define DATANACK 4
#define FIN 5
#define FINACK 6


typedef struct messageToSend
{
  int flag;
  int seqNr;
  int checkSum;
  char data[256];
}message;


//Global variables
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


//Timer functions
void start_timer(int duration)
{
  if (!timerRunning)
  {
    struct itimerval timer;
    timer.it_value.tv_sec = duration;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    timerRunning = 1;
  }
}

void stop_timer()
{
  struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    timerRunning = 0;
}

void timeout_handler(int signum)
{
  /*Specify the actions to be executed
  based on the state when the timer expires*/
  switch (state)
  {
    case WAIT_FOR_SYNACK:
        stop_timer();
        printf("CLIENT: SYNACK TIMEOUT -> SENDING SYN\n");

        msgToSend.flag = SYN;
        msgToSend.seqNr = 0;
        msgToSend.checkSum = checksumCalc(msgToSend);

        mySendTo(sock, (struct sockaddr*)&clientAddr);
        start_timer(3);
        break;
    case WAIT_FOR_FINACK:
        stop_timer()
        printf("CLIENT: FIN TIMEOUT -> SENDING FIN\n");

        msgToSend.flag = FIN;
        msgToSend.seqNr = 0;
        msgToSend.checkSum = checksumCalc(msgToSend);

        mySendTo(sock, (struct sockaddr*)&clientAddr);
        start_timer(3);

        break;
    default:
      printf("Invalid option\n");
  }
}


//Message handling functions
void* recieveThread(int* sock)
{
  socklen_t len = 0;
  struct sockaddr_in serverAddress;

  while (1)
  {
    if (newMessage == 0) {
      int bytesRecieved = recvfrom(*sock, &messageRecvd, sizeof(message), 0,
                                  (struct sockaddr*)&serverAddress, &len);

      if (bytesRecieved < 0)
      {
        perror("Reading message didn't work\n");
        continue;
      }
      else
      {
        newMessage = 1;
      }
    }
  }
  return NULL;
}

uint32_t checksumActualCalculation(const uint8_t* messageToSend, uint32_t messageLength)
{
    uint32_t workingRegister = 0;            // working register to not destroy messageToSend
    uint32_t polynom = 0xEDB88320;          // polynom used in CRC-32 in reversed order

    for (uint32_t i = 0; i < messageLength; ++i)   // for each byte in messageToSend:
    {
      workingRegister ^= messageToSend[i];       //  XOR it into the rightmost byte of the workingRegister
      for (uint32_t j = 0; j < 8; ++j)          //  process 8 bits
      {
        uint32_t rightmost_bit_set = workingRegister & 1; // check whether shifting out a 1
        workingRegister >>= 1;                           //   shift right

        if (rightmost_bit_set)                          //  if we shifted out a 1:
        {
          workingRegister ^= polynom;                 //  Bitwise XOR the polynom into the workingRegister
        }
      }
    }
    return workingRegister;
}

int checksumCalc(message messageToSend)
{
  messageToSend.checkSum = 0;

  uint32_t result = checksumActualCalculation((const uint8_t*)&messageToSend, sizeof(message));

  return result;
}

int mySendTo(int sock, struct sockaddr* recvAddr)
{
  int randomNumber = rand()%100;
  int len = 0;

  if (randomNumber <= 9)
  {
    msgToSend.checkSum--;
    len = sendto(sock, &msgToSend, sizeof(message), 0, recvAddr,
      sizeof(struct sockaddr_in));
    if (len == -1)
    {
      printf("Error sending on socket\n");
    }

    printf("Altering checksum\n");
  }
  else if (randomNumber >= 10 && randomNumber < 20)
  {
    //Don't send anything at all.
    printf("Didn't send a packet at all\n");
  }
  else
  {
    len = sendto(sock, &msgToSend, sizeof(message), 0, recvAddr,
                sizeof(struct sockaddr_in));
    if (len == -1)
    {
      printf("Error sending on socket");
    }

    printf("Sent like normal\n");
  }
  return len;
}


//State machine functions
void connect(int sock, struct sockaddr_in* clientAddr)//Add input parameters if needed
{
    /*Implement the three-way handshake state machine
    for the connection setup*/

    //Loop switch-case
    while(state != CONNECTED) //Condition to leave the state machine
    {
        switch (state)
        {
            case INIT:
                printf("CLIENT: INIT -> SENDING SYN\n");

                msgToSend.flag = SYN;
                msgToSend.seqNr = 0;
                msgToSend.checkSum = checksumCalc(msgToSend);

                mySendTo(sock, (struct sockaddr*)&clientAddr);
                start_timer(3);

                state = WAIT_FOR_SYNACK;

                break;
            case WAIT_FOR_SYNACK:
                if (timerRunning == 0)
                    timeout_handler(sock, clientAddr);
                if (newMessage == 1 && messageRecvd.flag == SYNACK)
                {
                    newMessage = 0;

                    // Check the checksum
                    int recivedChecksum = messageRecvd.checkSum;
                    messageRecvd.checkSum = 0;
                    int calculatedChecksum = checksumCalc(messageRecvd);

                    if (recivedChecksum == calculatedChecksum)
                    {
                      printf("CLIENT: WAIT_FOR_SYNACK -> SENDING ACK\n");

                      msgToSend.flag = DATAACK;
                      msgToSend.seqNr = 0;
                      msgToSend.checkSum = checksumCalc(msgToSend);
  
                      mySendTo(sock, (struct sockaddr*)&clientAddr);
  
                      state = CONNECTED;
                    }
                    else
                    {
                      printf("CLIENT: Checksum mismatch - ignoring packet\n");
                    }
                    
                }
                break;
            default:
                printf("Invalid option\n");
        }
    }
}

void transmit()//Add input parameters if needed
{

  /*Implement the sliding window state machine*/

  //local variables if needed


  //Loop switch-case
  while(1) //Add the condition to leave the state machine
  {
    switch (state)
    {
      case YOUR_STATE:
        /*actions to be executed if state == YOUR_STATE*/
        state = NEW_STATE;
        break;
      case NEW_STATE:
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
  while(1) //Add the condition to leave the state machine
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
          if (timeout_handler(, ))
          printf("CLIENT: WAIT_FOR_FINACK -> TERMINATING CONNECTION\n");

          msgToSend.flag = ACK;
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


  //The data to be sent
  char dataContent[20][3] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19"};

  char dstHost[] = "10.0.2.15";
  int dstUdpPort = 5555;

  //Setup signal handler for timeout
  struct sigaction saTimeout;

  saTimeout.sa_handler = timeout_handler;
  saTimeout.sa_flags = 0;
  sigaction(SIGALRM, &saTimeout, NULL);


  //Check arguments
  if(argv[1] == NULL)
  {
    perror("Usage: client [host name]\n");
    exit(EXIT_FAILURE);
  }
  else
  {
    strncpy(hostName, argv[1], hostNameLength);
    hostName[hostNameLength - 1] = '\0';
  }


  //Create the socket
  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    fprintf(stderr, "Can't create UDP socket\n");
    exit(EXIT_FAILURE);
  }

  clientSock.sin_family = AF_INET;
  clientSock.sin_port = htons(5552);
  clientSock.sin_addr.s_addr = inet_addr("10.0.2.15");


  if(bind(sock, (struct sockaddr *)&clientSock, sizeof(clientSock)) < 0) {
    perror("Could not bind a name to the socket\n");
    exit(EXIT_FAILURE);
  }


  //Create the receiving thread
  pthread_create(&recvt, NULL, recieveThread, &sock);

  clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(dstUdpPort);
  clientAddr.sin_addr.s_addr = inet_addr(dstHost);


  connect(sock, &clientAddr);//Add arguments if needed
  transmit();//Add arguments if needed
  disconnect(sock, &clientAddr);//Add arguments if needed


  return 0;
}
