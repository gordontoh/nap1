/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * $Id: example-trickle.c,v 1.5 2010/01/15 10:24:37 nifi Exp $
 */

/**
 * \file
 *         Example for using the trickle code in Rime
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "net/rime/trickle.h"
#include "dev/leds.h"
#include "button-sensors.h"

#include <stdio.h>

#define FLASH_LED(l) {leds_on(l); clock_delay_msec(50); leds_off(l); clock_delay_msec(50);}
#define SEQSIZE 20
#define ANYCAST_SVC1 777
#define ANYCAST_SVC2 888

/*---------------------------------------------------------------------------*/
PROCESS(example_trickle_process, "Trickle example");
AUTOSTART_PROCESSES(&example_trickle_process);
/*---------------------------------------------------------------------------*/
static void
trickle_recv(struct trickle_conn *c)
{ 
  printf("Trickle message received: %s\r\n", (char *)packetbuf_dataptr());
  FLASH_LED(LEDS_BLUE);
}

const static struct trickle_callbacks trickle_call = {trickle_recv};
static struct trickle_conn trickle;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_trickle_process, ev, data)
{
  PROCESS_EXITHANDLER(trickle_close(&trickle);)
  PROCESS_BEGIN();

  trickle_open(&trickle, CLOCK_SECOND, 130, &trickle_call);
  SENSORS_ACTIVATE(button_sensor);
  SENSORS_ACTIVATE(button2_sensor);

  // Set TX power - values range from 0x00 (-30dBm = 1uW) to 0x12 (+4.5dBm = 2.8mW)
  //
  set_power(0x01);

	static int s3_req_no=1;
	static int s2_req_no=1;

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && (data == &button_sensor || data == &button2_sensor));

		char buf[SEQSIZE];
		if(data == &button_sensor){
			snprintf(buf, SEQSIZE, "ANYCAST:%d;SEQ:%d", ANYCAST_SVC1, s3_req_no++);
		}else{
      snprintf(buf, SEQSIZE, "ANYCAST:%d;SEQ:%d", ANYCAST_SVC2, s2_req_no++);
		}
    packetbuf_copyfrom(buf, SEQSIZE);
    printf("Sending trickle message: %s\n", (char *)packetbuf_dataptr());
    trickle_send(&trickle);
    FLASH_LED(LEDS_GREEN);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
