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

//Define state machine states here, e.g.:
#define INIT 0
#define WAIT_FOR_SYN 1
#define WAIT_FOR_ACK 2
#define CONNECTED 3

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
message ack;

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
  fflush(stdout);
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
    case WAIT_FOR_ACK:
		printf("SERVER: ACK TIMEOUT -> SENDING SYNACK\n");

		//recalculate checksum before resending
		messageToSend.checkSum = checksumCalc(messageToSend);
		mySendTo(sock, (struct sockaddr*)&clientAddr);

		start_timer(3); // restart timer
        break;
    default:
      printf("Invalid option\n");
  }
}


//Message handling functions
void* recieveThread(int* sock)
{
  socklen_t len = 0;
  struct sockaddr_in clientAddress;

  while (1)
  {
    if (newMessage == 0) {
      int bytesRecieved = recvfrom(*sock, &messageRecvd, sizeof(message), 0,
                                  (struct sockaddr*)&clientAddress, &len);

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

uint32_t checksumActualCalculation(const uint8_t* messageRecieved, uint32_t messageLength)
{
    uint32_t workingRegister = 0;            // working register to not destroy messageRecieved
    uint32_t polynom = 0xEDB88320;          // polynom used in CRC-32 in reversed order

    for (uint32_t i = 0; i < messageLength; ++i)   // for each byte in messageRecieved:
    {
      workingRegister ^= messageRecieved[i];       //  XOR it into the rightmost byte of the workingRegister
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

int checksumCalc(message packetRecieved)
{
  packetRecieved.checkSum = 0;

  uint32_t result = checksumActualCalculation((const uint8_t*)&packetRecieved, sizeof(message));

  return result;
}

int mySendTo(int sock, struct sockaddr* recvAddr)
{
  int randomNumber = rand()%100;
  int len = 0;

  if (randomNumber <= 9)
  {
    ack.checkSum--;
    len = sendto(sock, &ack, sizeof(message), 0, recvAddr,
          sizeof(struct sockaddr_in));
    if (len == -1)
    {
      printf("Error sending on socket");
    }

    printf("Altering checksum\n");
  }
  else if (randomNumber <= 20 && randomNumber >= 10)
  {
    printf("Didn't send packet at all\n");
  }
  else
  {
    len = sendto(sock, &ack, sizeof(message), 0, recvAddr,
                sizeof(struct sockaddr_in));
    if (len == -1)
    {
      printf("Error sending on socket");
    }

    printf("Sent like normal\n");
  }
  return len;
}


//Create the socket
int makeSocket(unsigned short int port) {
  int sock;
  struct sockaddr_in name;

  sock = socket(PF_INET, SOCK_DGRAM, 0);
  if(sock < 0) {
    perror("Could not create a socket\n");
    exit(EXIT_FAILURE);
  }

  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);

  if(bind(sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
    perror("Could not bind a name to the socket\n");
    exit(EXIT_FAILURE);
  }
  return(sock);
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
		  printf("SERVER: Waiting for SYN\n");
          state = WAIT_FOR_SYN;
          break;
      case WAIT_FOR_SYN:
          if (newMessage == 1 && messageRecvd.flag == SYN)
          {
              newMessage = 0;

			  // Check the checksum
			  int recivedChecksum = messageRecvd.checkSum;
			  messageRecvd.checkSum = 0;
			  int calculatedChecksum = checksumCalc(messageRecvd);

              if (recivedChecksum == calculatedChecksum) {
				  printf("SERVER: Recived valid syn. Sending SYNACK\n");
				  messageToSend.flag = SYNACK;
				  messageToSend.seqNr = 0;
				  messageToSend.checkSum = checksumCalc(messageToSend);
				  start_timer(3);

				  mySendTo(sock, (struct sockaddr*)&clientAddr);

				  state = WAIT_FOR_ACK;
              }
              else 
              {
				  printf("SERVER: Checksum mismatch on SYN - ignoring packet\n");
              }
          }
        break;
	  case WAIT_FOR_ACK:
          if (newMessage == 1 && messageRecvd.flag == DATAACK)
          {
			  newMessage = 0;

			  // Check the checksum
			  int recivedChecksum = messageRecvd.checkSum;
			  messageRecvd.checkSum = 0;
			  int calculatedChecksum = checksumCalc(messageRecvd);
			  stop_timer();

			  if (recivedChecksum == calculatedChecksum) 
              {
				  printf("SERVER: Recived valid ACK. Connection established\n");

				  state = CONNECTED;
              }
              else
              {
				  printf("SERVER: Checksum mismatch on ACK - ignoring packet\n");
              }
          }
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
        ...
        break;
      default:
        printf("Invalid option\n");
    }
  }
}

void disconnect()//Add input parameters if needed
{

  /*Implement the three-way handshake state machine
  for the teardown*/

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
        ...
        break;
      default:
        printf("Invalid option\n");
    }
  }
}


int main(int argc, char *argv[])
{
  int sock;
  struct sockaddr_in clientAddr = { 0 };
  pthread_t recvt;

  sock = makeSocket(PORT);

  clientAddr.sin_family = AF_INET; //Client Address
  clientAddr.sin_port = htons(5552);
  clientAddr.sin_addr.s_addr = inet_addr("10.0.2.15");


  //Setup signal handler for timeout
  struct sigaction saTimeout;

  saTimeout.sa_handler = timeout_handler;
  saTimeout.sa_flags = 0;
  sigaction(SIGALRM, &saTimeout, NULL);


  //Create the receiving thread
  pthread_create(&recvt, NULL, recieveThread, &sock);


  connect(sock &clientAddr);//Add arguments if needed
  transmit();//Add arguments if needed
  disconnect();//Add arguments if needed


  return 0;
}
