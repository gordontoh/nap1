/**
 * \file
 *         Example for using the anycast code
 * \author
 *         Wei Qiao Toh
 */

#include "contiki.h"
#include "button-sensors.h"
#include "dev/leds.h"
#include "anycast.h"
#include <stdio.h>

#define FLASH_LED(l)            \
{                               \
        leds_on(l);             \
        clock_delay_msec(50);   \
        leds_off(l);            \
        clock_delay_msec(50);   \
}

#define ANYCAST_CHANNEL 129
#define S2_ANYCAST_SVC 101
#define S3_ANYCAST_SVC 102
#define ANYCAST_ADDR_1 103
#define ANYCAST_ADDR_2 104 

/*---------------------------------------------------------------------------*/
PROCESS(anycast_process, "Anycast");
AUTOSTART_PROCESSES(&anycast_process);
/*---------------------------------------------------------------------------*/
void 
anycast_recv(struct anycast_conn *c, const rimeaddr_t * originator,
	const anycast_addr_t anycast_addr, char *data)
{
	printf("---------------App layer------------------\n");
	printf("'%s' received on anycast service %u.\n", data, anycast_addr);
	printf("------------------------------------------\n");
}
/*---------------------------------------------------------------------------*/
void 
anycast_sent(struct anycast_conn *c, const anycast_addr_t anycast_addr, 
	char *data)
{
	printf("---------------App layer------------------\n");
	printf("'%s' sent to anycast server %u.\n", data, anycast_addr);
	printf("------------------------------------------\n");
}
/*---------------------------------------------------------------------------*/
void 
anycast_timedout(struct anycast_conn *c, const uint8_t err_code)
{
	printf("---------------App layer------------------\n");
	if(err_code == ERR_NO_SERVER_FOUND) {
		printf("Anycast server not found. (netflood failed)\n");
	} else if (err_code == ERR_NO_ROUTE) {
		printf("Sending data failed. (mesh timed out)\n");
	}
	printf("------------------------------------------\n");
}
/*---------------------------------------------------------------------------*/
static const struct anycast_callbacks anycast_call = 
			{ anycast_recv, anycast_sent, anycast_timedout };
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
	rimeaddr_copy(&addr, &rimeaddr_node_addr);
	if(!(addr.u8[1]==0 && addr.u8[0]==0x09)) {
		rimeaddr_copy(&addr, &rimeaddr_null);
  		addr.u8[0] = 0x09;
  		rimeaddr_set_node_addr(&addr);
	}

  	/* Set TX power - values range from 0x00 (-30dBm = 1uW) to 
	 * 0x12 (+4.5dBm = 2.8mW) 
	 */
  	set_power(0x04); 

	anycast_open(&anycast, ANYCAST_CHANNEL, &anycast_call);

	anycast_listen_on(&anycast, ANYCAST_ADDR_1);
	
	anycast_listen_on(&anycast, ANYCAST_ADDR_2);

  	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && 
			(data == &button_sensor || data == &button2_sensor));
	
		char buf[ANYCAST_DATA_LEN];
		snprintf(buf, ANYCAST_DATA_LEN, "Hello from Gordon (node 9)");
    		packetbuf_copyfrom(buf, sizeof(buf));
		
		if(data == &button_sensor) {
    			anycast_send(&anycast, (uint8_t)S3_ANYCAST_SVC);
    		} else {
    			anycast_send(&anycast, (uint8_t)S2_ANYCAST_SVC);
   		}
		FLASH_LED(LEDS_GREEN);
  	}

  	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
