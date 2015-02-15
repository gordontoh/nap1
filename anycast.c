/**
 * \file
 *         Anycast header file
 * \author
 *         Wei Qiao Toh
 */

#include "contiki.h"
#include "anycast.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "anycast.h"
#include "dev/leds.h"
#include <stddef.h> /* For offsetof */

#define PACKET_TIMEOUT (CLOCK_SECOND * 10)
#define ANYCAST_RES_FLAG 0
#define ANYCAST_DATA_FLAG 1
#define ANYCAST_DATA_LEN 126

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define FLASH_LED(l) {leds_on(l); clock_delay_msec(50); leds_off(l); clock_delay_msec(50);}


struct anycast_address {
  struct anycast_address *next;
  anycast_addr_t address;
};

struct anycast_res {
	uint8_t flag;
	uint8_t seq_number;
	anycast_addr_t address;
};

struct anycast_data {
	uint8_t flag;
  char data[ANYCAST_DATA_LEN];
};

struct anycast_send_buffer {
	struct anycast_send_buffer *next;
	anycast_addr_t address;
	uint8_t seq_number;
	char data[ANYCAST_DATA_LEN];
};

MEMB(anycast_mem, struct anycast_address, 10);

LIST(send_buf);

static list_t send_buf;
  
static uint8_t req_no = 1;

/*---------------------------------------------------------------------------*/
PROCESS(address_process, "Print address periodically");
/*---------------------------------------------------------------------------*/
static int 
netflood_recv(struct netflood_conn *netflood, 
				const rimeaddr_t * from, 
				const rimeaddr_t * originator, 
				uint8_t seqno,	
				uint8_t hops)
{
  uint8_t anycast_addr = *((char *) packetbuf_dataptr());

	struct anycast_conn *c = (struct anycast_conn *)
  				((char *) netflood - offsetof(struct anycast_conn, netflood_conn));

	struct anycast_address *s;	
	/* check and serve anycast request */
	for(s = list_head(c->bind_addrs);
      s != NULL;
      s = list_item_next(s)) {
		if(anycast_addr == (anycast_addr_t)s->address){
    	PRINTF("Service request on %u\tFrom %02X:%02X\tSeq %u\n",
							anycast_addr,
							originator->u8[1],
          		originator->u8[0],
          		seqno);
			mesh_send(&c->mesh_conn, originator);
			FLASH_LED(LEDS_ALL);
			return 0;
		}
  }

	/* forward anycast request message */
  PRINTF("NetFlood packet received from %02X:%02X through %02X:%02X, \
					seqno=%u, hops=%u, msg='%u'\n", 
					originator->u8[1], 
					originator->u8[0], 
					from->u8[1], 
					from->u8[0], 
					seqno, 
					hops, 
					anycast_addr);
  
	FLASH_LED(LEDS_BLUE);
  return 1;
}
/*---------------------------------------------------------------------------*/
static void netflood_sent(struct netflood_conn *c)
{
  PRINTF("NetFlood packet sent ! \n");
  //PRINTF("Netflood SEQ: %d \n", packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
  //PRINTF("Message: %s %u\n", (char *)packetbuf_dataptr(), *((char *)packetbuf_dataptr()) );
}
/*---------------------------------------------------------------------------*/
static void netflood_dropped(struct netflood_conn *c)
{
  PRINTF("NetFlood packet dropped !\n");
}
/*---------------------------------------------------------------------------*/
static void mesh_sent(struct mesh_conn *c)
{
  PRINTF("packet sent\n");
  //PRINTF("Message: %s\n", (char *)packetbuf_dataptr());
}
/*---------------------------------------------------------------------------*/
static void mesh_timedout(struct mesh_conn *c)
{
  PRINTF("packet timedout\n");
}
/*---------------------------------------------------------------------------*/
static void mesh_recv(struct mesh_conn *c, const rimeaddr_t *from, uint8_t hops)
{
	/* get first byte to determine whether it's a RES or DATA */
	uint8_t flag = (uint8_t) *((char *)packetbuf_dataptr());
	
	if(flag == ANYCAST_RES_FLAG){
		struct anycast_res *res = (struct anycast_res *)(char *)packetbuf_dataptr();	
		mesh_send(c ,from);
	} else if (flag == ANYCAST_DATA_FLAG) {
		struct anycast_data	*data = (struct anycast_data *)(char *)packetbuf_dataptr();
	}	/*else {		
  	PRINTF("Data received from %02X:%02X: %.*s (%d)\n", 
						from->u8[0], from->u8[1], 
						(char *)packetbuf_dataptr(), 
						packetbuf_datalen());
  	mesh_send(c, from);
	}*/
}
/*---------------------------------------------------------------------------*/
static const struct netflood_callbacks netflood_call = { netflood_recv, netflood_sent, netflood_dropped };
static const struct mesh_callbacks mesh_call = {mesh_recv, mesh_sent, mesh_timedout};
/*---------------------------------------------------------------------------*/
void 
anycast_open(struct anycast_conn *c, uint16_t channels, 
				const struct anycast_callbacks *callbacks)
{
	netflood_open(&c->netflood_conn, CLOCK_SECOND * 2, channels, 
				&netflood_call);
  mesh_open(&c->mesh_conn, channels+1, &mesh_call);
  c->cb = callbacks;
	LIST_STRUCT_INIT(c, bind_addrs);
	memb_init(&anycast_mem);

	process_start(&address_process, (char *)c);
}
/*---------------------------------------------------------------------------*/
int 
anycast_listen_on(struct anycast_conn *c, const anycast_addr_t anycast_addr)
{
	static struct anycast_address *addr;
  addr = memb_alloc(&anycast_mem);
  if(addr != NULL) {	
		addr->address = anycast_addr;
		list_add(c->bind_addrs, addr);
		PRINTF("[LOG]\t\tBinded anycast addr %u \n", addr->address);
	}
}
/*---------------------------------------------------------------------------*/
void 
anycast_send(struct anycast_conn *c, const anycast_addr_t dest)
{
	static struct anycast_send_buffer sbuf;
	char addr_buf[2];

	if(dest>1 && dest<255) {
		/* store data in buf first */
		if(sizeof((char *)packetbuf_dataptr()) > ANYCAST_DATA_LEN){
			PRINTF("[ERROR]\t\tData length out of range.");
			return;
		}
		sbuf.address = dest;
		sbuf.seq_number = req_no++;
		snprintf(sbuf.data, packetbuf_datalen(),"%s", (char *)packetbuf_dataptr());
		PRINTF("[LOG]\t\tSending data to %u, seq %u, data '%s'\n", sbuf.address, sbuf.seq_number, sbuf.data);
		list_add(send_buf, &sbuf);
	
		snprintf(addr_buf, 2, "%c", (uint8_t) sbuf.address);
    packetbuf_copyfrom(addr_buf, sizeof(addr_buf));
		netflood_send(&c->netflood_conn, (uint8_t) sbuf.seq_number);
	}else { 
		PRINTF("[ERROR]\t\tAnycast address out of range.\n");
	}
	
}
/*---------------------------------------------------------------------------*/
void 
anycast_close(struct anycast_conn *c)
{
	struct anycast_address *s;
	while(list_length(c->bind_addrs) > 0) {
		s = list_chop(c->bind_addrs);
		PRINTF("Closing anycast listen address: %u\n", s->address);
		memb_free(&anycast_mem, s);
	}
	netflood_close(&c->netflood_conn);
	mesh_close(&c->mesh_conn);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(address_process, ev, data)
{
  static struct etimer et;
	static struct anycast_conn *a = NULL;
	struct anycast_address *s;	
	uint8_t i = 0;
	rimeaddr_t addr;
	
	/* set only once */
	if(a == NULL){
		a = (struct anycast_conn *)data;
	}

	/* check everytime */
  rimeaddr_copy(&addr, &rimeaddr_node_addr);

  PROCESS_BEGIN();

  while(1) {
		/* Delay 20 seconds */
    etimer_set(&et, CLOCK_SECOND * 20);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    PRINTF("[LOG]\t\tAddresses{ RIME:%02X:%02X", addr.u8[1], addr.u8[0]);
		
		for(s = list_head(a->bind_addrs); s != NULL; s = s->next ) {
			PRINTF("\tANYCAST%u:%u", ++i, s->address);
  	}
		PRINTF(" }\n");
  }

  PROCESS_END();
}


