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
#include "button-sensors.h"
#include "dev/leds.h"
#include "anycast.h"
#include <stdio.h>

#define FLASH_LED(l) {leds_on(l);	clock_delay_msec(50);	leds_off(l);clock_delay_msec(50);}

#define ANYCAST_CHANNEL 129
#define S2_ANYCAST_SVC 101
#define S3_ANYCAST_SVC 102
#define ANYCAST_ADDR_1 103
#define ANYCAST_ADDR_2 104 

/*---------------------------------------------------------------------------*/
PROCESS(anycast_process, "Anycast");
AUTOSTART_PROCESSES(&anycast_process);
/*---------------------------------------------------------------------------*/
void anycast_recv(struct anycast_conn *c, const rimeaddr_t * originator,
    const anycast_addr_t anycast_addr)
{

	printf("Anycast recv.\n");
}
/*---------------------------------------------------------------------------*/
void anycast_sent(struct anycast_conn *c)
{

	printf("Anycast sent.\n");
}
/*---------------------------------------------------------------------------*/
void anycast_timedout(struct anycast_conn *c, const uint8_t err_code)
{
	printf("Anycast timedout.\n");
}
/*---------------------------------------------------------------------------*/
static const struct anycast_callbacks anycast_call = { anycast_recv, anycast_sent, anycast_timedout };
static struct anycast_conn anycast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(anycast_process, ev, data)
{
  PROCESS_EXITHANDLER(anycast_close(&anycast);)
  PROCESS_BEGIN();

	SENSORS_ACTIVATE(button_sensor);
	SENSORS_ACTIVATE(button2_sensor);

	/* Set RIME address according to OrisenPrime serial no. */
	rimeaddr_t addr;
	rimeaddr_copy(&addr, &rimeaddr_null);
  addr.u8[0] = 0x09;
  rimeaddr_set_node_addr(&addr);

  /* Set TX power - values range from 0x00 (-30dBm = 1uW) to 0x12 (+4.5dBm = 2.8mW) */
  /*set_power(0x01);*/

	anycast_open(&anycast, ANYCAST_CHANNEL, &anycast_call);

	anycast_listen_on(&anycast, ANYCAST_ADDR_1);
	
	anycast_listen_on(&anycast, ANYCAST_ADDR_2);

  while(1) {
		PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && (data == &button_sensor || data == &button2_sensor));
	
		char buf[20];
		snprintf(buf, 7, "Gordon");
    packetbuf_copyfrom(buf, sizeof(buf));
		
		if(data == &button_sensor){
    	//anycast_send(&anycast, (uint8_t)S3_ANYCAST_SVC);
			anycast_close(&anycast);
    }else{
    	//anycast_send(&anycast, (uint8_t)S2_ANYCAST_SVC);
			anycast_listen_on(&anycast, ANYCAST_ADDR_1);

		  anycast_listen_on(&anycast, ANYCAST_ADDR_2);

   	}
		FLASH_LED(LEDS_GREEN);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
