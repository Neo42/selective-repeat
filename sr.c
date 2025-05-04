#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
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
#define SEQSPACE 7    /* the min sequence space for GBN must be at least windowsize + 1 */
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
    return (false);
  else
    return (true);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE]; /* array for storing packets waiting for ACK */
static int windowcount;               /* the number of packets currently awaiting an ACK */
static int A_baseseqnum;              /* the first sequece number in sender's window */
static int A_nextseqnum;              /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
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
void A_input(struct pkt packet)
{
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
// Initialize sender A's state variables
void A_init(void)
{
  A_baseseqnum = 0;
  A_nextseqnum = 0; /* A starts with seq num 0, do not change this */
  windowcount = 0;
}

/********* Receiver (B)  variables and procedures ************/

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
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
