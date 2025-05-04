#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT 16.0      /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6  /* the maximum number of buffered unacked packet \
                        MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE (2*WINDOWSIZE)    /* the min sequence space for SR must be at least windowsize * 2 */
#define NOTINUSE (-1) /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for (i = 0; i < 20; i++)
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return -1;
  else
    return 0;
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE]; /* array for storing packets waiting for ACK */
static int windowcount;               /* the number of packets currently awaiting an ACK */
static int A_baseseqnum;              /* the first sequece number in sender's window */
static int A_nextseqnum;              /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
/* A_output: Processes new messages from application layer and sends packets
 *
 * This function implements the core transmission logic of selective repeat:
 * 1. Calculates the current window boundaries (seqfirst to seqlast)
 * 2. Checks if next sequence number falls within the window:
 *    - If within window: creates packet, assigns sequence number,
 *      calculates checksum, buffers packet, transmits to network layer,
 *      starts timer if it's the first packet, advances sequence counter
 *    - If window full: increments blocked message counter
 * 3. Handles sequence number wraparound in both window calculations
 *    and next sequence number assignment
 */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;
  int index;
  int seqfirst = A_baseseqnum;
  int seqlast = (A_baseseqnum + WINDOWSIZE - 1) % SEQSPACE;

  /* if the A_nextseqnum is inside the window */
  if (((seqfirst <= seqlast) && (A_nextseqnum >= seqfirst && A_nextseqnum <= seqlast)) ||
      ((seqfirst > seqlast) && (A_nextseqnum >= seqfirst || A_nextseqnum <= seqlast)))
  {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    if (A_nextseqnum >= seqfirst)
      index = A_nextseqnum - seqfirst;
    else
      index = WINDOWSIZE - seqfirst + A_nextseqnum;
    buffer[index] = sendpkt;
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    /* start timer if first packet in window */
    if (A_nextseqnum == seqfirst)
      starttimer(A, RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  }
  /* if blocked, window is full */
  else
  {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
/* A_input: Handles acknowledgments received from receiver B
 *
 * This function processes incoming ACK packets and manages the sliding window:
 * 1. Verifies packet integrity using checksum
 * 2. For valid ACKs within the current window:
 *    - Detects and handles duplicate ACKs
 *    - Marks new ACKs in the buffer and decrements window count
 *    - For ACKs of the base packet (oldest unacknowledged):
 *      > Counts consecutive ACKs in buffer
 *      > Slides window forward accordingly
 *      > Updates buffer by shifting packets
 *      > Manages timer (stops and restarts if needed)
 *    - For ACKs of other packets, updates buffer without sliding window
 * 3. Handles wraparound sequence numbers with proper window boundary calculations
 */

void A_input(struct pkt packet)
{
  int ackcount = 0;
  int i;
  int seqfirst;
  int seqlast;
  int index;
  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet))
  {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    /* need to check if new ACK or duplicate */
    seqfirst = A_baseseqnum;
    seqlast = (A_baseseqnum + WINDOWSIZE - 1) % SEQSPACE;

    /* check case when seqnum has and hasn't wrapped */
    if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
        ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast)))
    {
      /* check coresponding position in window buffer */
      if (packet.acknum >= seqfirst)
        index = packet.acknum - seqfirst;
      else
        index = WINDOWSIZE - seqfirst + packet.acknum;

      if (buffer[index].acknum == NOTINUSE)
      {
        /* packet is a new ACK */
        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;
        windowcount--;
        buffer[index].acknum = packet.acknum;
      }
      else
      {
        if (TRACE > 0)
          printf("----A: duplicate ACK received, do nothing!\n");
      }
      /* check if it is the first one*/
      if (packet.acknum == seqfirst)
      {
        /* check how many concsecutive acks received in buffer */
        for (i = 0; i < WINDOWSIZE; i++)
        {
          if (buffer[i].acknum != NOTINUSE && strcmp(buffer[i].payload, "") != 0)
            ackcount++;
          else
            break;
        }

        /* slide window */
        A_baseseqnum = (A_baseseqnum + ackcount) % SEQSPACE;

        /* update buffer */
        for (i = 0; i < WINDOWSIZE; i++)
        {
          if (buffer[i + ackcount].acknum == NOTINUSE || (buffer[i].seqnum + ackcount) % SEQSPACE == A_nextseqnum)
            buffer[i] = buffer[i + ackcount];
        }

        /* restart timer */
        stoptimer(A);
        if (windowcount > 0)
          starttimer(A, RTT);
      }
      else
      {
        /* update buffer */
        buffer[index].acknum = packet.acknum;
      }
    }
  }
  else
  {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* called when A's timer goes off */
/* When it is necessary to resend a packet, the oldest unacknowledged packet should be resent*/
void A_timerinterrupt(void)
{
  if (TRACE > 0)
  {
    printf("----A: time out,resend packets!\n");
    printf("---A: resending packet %d\n", (buffer[0]).seqnum);
  }
  tolayer3(A, buffer[0]);
  packets_resent++;
  starttimer(A, RTT);
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
/* Initialize sender A's state variables */
void A_init(void)
{
  A_baseseqnum = 0;
  A_nextseqnum = 0; /* A starts with seq num 0, do not change this */
  windowcount = 0;
}

/********* Receiver (B)  variables and procedures ************/

static struct pkt B_buffer[WINDOWSIZE]; /* array for storing packets waiting for packet from A */
static int B_baseseqnum;                /* first sequence number of the receiver's window */
static int receivelast;                 /* record the last packet received position */

/* called from layer 3, when a packet arrives for layer 4 at B*/
/* B_input: Handles data packets received from sender A
 *
 * This function implements receiver-side selective repeat protocol logic:
 * 1. Verifies packet integrity using checksum
 * 2. For valid packets:
 *    - Immediately generates and sends ACK with matching sequence number
 *    - Determines if packet falls within current receive window
 *    - For in-window packets:
 *      > Calculates appropriate buffer position
 *      > Tracks the furthest received packet position
 *      > Checks for and handles duplicate packets
 *      > For packets at window base:
 *        - Counts consecutive received packets
 *        - Slides window forward accordingly
 *        - Updates buffer by shifting packets
 *      > Delivers valid packet data to application layer
 * 3. Properly handles sequence number wraparound in window calculations
 *
 * The implementation follows selective repeat by accepting out-of-order
 * packets while still maintaining ordered delivery to the application.
 */
void B_input(struct pkt packet)
{
  int pckcount = 0;
  struct pkt sendpkt;
  int i;
  int seqfirst;
  int seqlast;
  int index;
  /* if received packet is not corrupted */
  if (!IsCorrupted(packet))
  {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    packets_received++;
    /*create sendpkt*/
    /* send an ACK for the received packet */
    sendpkt.acknum = packet.seqnum;
    sendpkt.seqnum = NOTINUSE;
    /* we don't have any data to send.  fill payload with 0's */
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = '0';
    /* computer checksum */
    sendpkt.checksum = ComputeChecksum(sendpkt);
    /*send ack*/
    tolayer3(B, sendpkt);
    /* need to check if new packet or duplicate */
    seqfirst = B_baseseqnum;
    seqlast = (B_baseseqnum + WINDOWSIZE - 1) % SEQSPACE;

    /*see if the packet received is inside the window*/
    if (((seqfirst <= seqlast) && (packet.seqnum >= seqfirst && packet.seqnum <= seqlast)) ||
        ((seqfirst > seqlast) && (packet.seqnum >= seqfirst || packet.seqnum <= seqlast)))
    {

      /*get index*/
      if (packet.seqnum >= seqfirst)
        index = packet.seqnum - seqfirst;
      else
        index = WINDOWSIZE - seqfirst + packet.seqnum;
      /*keep receivelast */
      receivelast = receivelast > index ? receivelast : index;

      /*if not duplicate, save to buffer*/

      if (strcmp(B_buffer[index].payload, packet.payload) != 0)
      {
        /*buffer it*/
        packet.acknum = packet.seqnum;
        B_buffer[index] = packet;
        /*if it is the base*/
        if (packet.seqnum == seqfirst)
        {
          for (i = 0; i < WINDOWSIZE; i++)
          {
            if (B_buffer[i].acknum >= 0 && strcmp(B_buffer[i].payload, "") != 0)
              pckcount++;
            else
              break;
          }
          /* update state variables */
          B_baseseqnum = (B_baseseqnum + pckcount) % SEQSPACE;
          /*update buffer*/
          for (i = 0; i < WINDOWSIZE; i++)
          {
            if ((i + pckcount) <= (receivelast + 1))
              B_buffer[i] = B_buffer[i + pckcount];
          }
        }
        /* deliver to receiving application */
        tolayer5(B, packet.payload);
      }
    }
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  /* initialise B's window, buffer and sequence number */
  B_baseseqnum = 0; /*record the first seq num of the window*/
  receivelast = -1;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
