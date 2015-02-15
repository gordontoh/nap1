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


struct anycast_bind_address {
  struct anycast_bind_address *next;
  anycast_addr_t address;
};

struct anycast_res {
	uint8_t flag;
	uint8_t seq_number;
	anycast_addr_t address;
};

struct anycast_data {
	uint8_t flag;
	anycast_addr_t address;
  char data[ANYCAST_DATA_LEN];
};

struct anycast_send_buffer {
	struct anycast_send_buffer *next;
	anycast_addr_t address;
	uint8_t seq_number;
	char data[ANYCAST_DATA_LEN];
	struct ctimer ctimer; 
};

/* listen to maximum of 5 anycast address */
MEMB(anycast_mem, struct anycast_bind_address, 5);

MEMB(send_buf_mem, struct anycast_send_buffer, 5);

LIST(send_buf);

static list_t send_buf;
  
static uint8_t req_no = 1;

/*---------------------------------------------------------------------------*/
PROCESS(address_process, "Print address periodically");
/*---------------------------------------------------------------------------*/
void *
get_send_buf(const anycast_addr_t addr, const uint8_t seq_no)
{
	struct anycast_send_buffer *b;
  for(b = list_head(send_buf); b != NULL; b = b->next ) {
		if(b->address == addr && b->seq_number == seq_no) {
			//PRINTF("[BUF]\t\tRemoving %u:%u:'%s' from send buffer...\n", b->address, b->seq_number, b->data);
			list_remove(send_buf, b);
			return b;
		}
  }
	return NULL;
}
/*---------------------------------------------------------------------------*/
static void
expire_buf_element(void *n)
{
  struct anycast_send_buffer *b = n;
	PRINTF("[LOG]\t\tBuffer entry expired -> %u:%u:'%s'\n", b->address, b->seq_number, b->data);
  list_remove(send_buf, b);
  memb_free(&send_buf_mem, b);
}
/*---------------------------------------------------------------------------*/
static int 
netflood_recv(struct netflood_conn *netflood, 
				const rimeaddr_t * from, 
				const rimeaddr_t * originator, 
				uint8_t seqno,	
				uint8_t hops)
{
	struct anycast_bind_address *s;
	struct anycast_res res;	

  uint8_t anycast_addr = *((char *) packetbuf_dataptr());

	struct anycast_conn *c = (struct anycast_conn *)
  				((char *) netflood - offsetof(struct anycast_conn, netflood_conn));

	/* check and serve anycast request */
	for(s = list_head(c->bind_addrs);
      s != NULL;
      s = list_item_next(s)) {
		if(anycast_addr == (anycast_addr_t)s->address){
    	PRINTF("[LOG]\t\tService request on %u. From %02X:%02X, seq %u\n",
							anycast_addr,
							originator->u8[1],
          		originator->u8[0],
          		seqno);
			res.flag = 0;
			res.seq_number = seqno;
			res.address = anycast_addr;
			packetbuf_copyfrom((char *)&res, sizeof(res));
			mesh_send(&c->mesh_conn, originator);
			FLASH_LED(LEDS_ALL);
			return 0;
		}
  }

	/* forward anycast request message */
  PRINTF("[LOG]\t\tForward anycast request from %02X:%02X to anycast %u\n",
					originator->u8[1], 
					originator->u8[0], 
					anycast_addr);
  
	FLASH_LED(LEDS_BLUE);
  return 1;
}
/*---------------------------------------------------------------------------*/
static void netflood_sent(struct netflood_conn *c)
{
	//PRINTF("[LOG]\t\tNetflood message sent.\n");
  //PRINTF("Netflood SEQ: %d \n", packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
  //PRINTF("Message: %s %u\n", (char *)packetbuf_dataptr(), *((char *)packetbuf_dataptr()) );
}
/*---------------------------------------------------------------------------*/
static void netflood_dropped(struct netflood_conn *c)
{
  //PRINTF("NetFlood packet dropped !\n");
}
/*---------------------------------------------------------------------------*/
static void mesh_sent(struct mesh_conn *c)
{
	/*struct anycast_res *r;
	r = (struct anycast_res *)packetbuf_dataptr();
  PRINTF("[LOG]\t\tMesh Sent. FLAG:%uSEQ:%uANYCAST: %u\n", r->flag, r->seq_number, r->address);
	*/
}
/*---------------------------------------------------------------------------*/
static void mesh_timedout(struct mesh_conn *c)
{
  PRINTF("[LOG]\t\tMesh packet timedout.\n");
	struct anycast_conn *a = (struct anycast_conn *)
    ((char *)c - offsetof(struct anycast_conn, mesh_conn));
	if(a->cb->timedout) {
		a->cb->timedout(a, ERR_NO_ROUTE);	
	}
}
/*---------------------------------------------------------------------------*/
static void mesh_recv(struct mesh_conn *c, 
				const rimeaddr_t *from, uint8_t hops)
{
	PRINTF("[LOG]\t\tMesh received (%02X:%02X).\n", from->u8[1], from->u8[0]);
	struct anycast_send_buffer *b;

	/* get first byte to determine whether it's a RES or DATA */
	uint8_t flag = (uint8_t) *((char *)packetbuf_dataptr());
	
	if(flag == ANYCAST_RES_FLAG){
		struct anycast_res *res = (struct anycast_res *)packetbuf_dataptr();	
		PRINTF("[LOG]\t\tAnycast server %u at %02X:%02X\n",res->address);
	
		b = (struct anycast_send_buffer *)get_send_buf(res->address, res->seq_number);
		if( b != NULL) {
			PRINTF("[LOG]\t\tSending data '%s'...\n", b->data);
			PRINTF("[BUF]\t\tRemoved %u:%u:'%s' from send buffer.\n", b->address, b->seq_number, b->data);
			memb_free(&send_buf_mem, b);
			packetbuf_copyfrom(b->data, sizeof(b->data));
			mesh_send(c ,from);
		} else {
			PRINTF("[ERROR]\t\tAnycast addr and seq pair not in send buffer.\n");
		}
	} else if (flag == ANYCAST_DATA_FLAG) {
		struct anycast_conn *a = (struct anycast_conn *)
    				((char *)c - offsetof(struct anycast_conn, mesh_conn));
		struct anycast_data	*data = (struct anycast_data *)packetbuf_dataptr();
		PRINTF("[LOG]\t\tAnycast data received from %02X:%02X\n",
						from->u8[1], from->u8[0]);
		PRINTF("[LOG]\t\tData:'%s'\n", data->data);
		a->cb->recv(a, from, data->address);
		
	}	/*else {		
  	PRINTF("Data received from %02X:%02X: %.*s (%d)\n", 
						from->u8[0], from->u8[1], 
						(char *)packetbuf_dataptr(), 
						packetbuf_datalen());
  	mesh_send(c, from);
	}*/
}
/*---------------------------------------------------------------------------*/
static const struct netflood_callbacks netflood_call = 
				{ netflood_recv, netflood_sent, netflood_dropped };
static const struct mesh_callbacks mesh_call = 
				{ mesh_recv, mesh_sent, mesh_timedout };
/*---------------------------------------------------------------------------*/
void 
anycast_open(struct anycast_conn *c, uint16_t channels, 
				const struct anycast_callbacks *callbacks)
{
	netflood_open(&c->netflood_conn, CLOCK_SECOND * 2, channels, &netflood_call);
  mesh_open(&c->mesh_conn, channels+1, &mesh_call);
  c->cb = callbacks;
	LIST_STRUCT_INIT(c, bind_addrs);
	memb_init(&anycast_mem);
	memb_init(&send_buf_mem);
	process_start(&address_process, (char *)c);
}
/*---------------------------------------------------------------------------*/
int 
anycast_listen_on(struct anycast_conn *c, const anycast_addr_t anycast_addr)
{
	static struct anycast_bind_address *addr;
  addr = memb_alloc(&anycast_mem);
  if(addr != NULL) {	
		addr->address = anycast_addr;
		list_add(c->bind_addrs, addr);
		PRINTF("[LOG]\t\tBinded anycast addr %u \n", addr->address);
		return 0;
	}

	return -1;
}
/*---------------------------------------------------------------------------*/
void 
anycast_send(struct anycast_conn *c, const anycast_addr_t dest)
{
	static struct anycast_send_buffer *sbuf;
	char addr_buf[2];

	sbuf = memb_alloc(&send_buf_mem);
	if(sbuf != NULL) {
		if(dest>1 && dest<255) {
			/* store data in buf first */
			if(sizeof((char *)packetbuf_dataptr()) > ANYCAST_DATA_LEN){
				PRINTF("[ERROR]\t\tData length out of range.");
				return;
			}
			sbuf->address = dest;
			sbuf->seq_number = req_no++;
			snprintf(sbuf->data, packetbuf_datalen(),"%s", (char *)packetbuf_dataptr());
			PRINTF("[LOG]\t\tSending data to %u, seq %u, data '%s'\n", 
							sbuf->address, sbuf->seq_number, sbuf->data);

			list_add(send_buf, sbuf);
			ctimer_set(&sbuf->ctimer, CLOCK_SECOND * 10, expire_buf_element, sbuf);
	
			snprintf(addr_buf, 2, "%c", (uint8_t) sbuf->address);
    	packetbuf_copyfrom(addr_buf, sizeof(addr_buf));
			netflood_send(&c->netflood_conn, (uint8_t) sbuf->seq_number);
		}else { 
			PRINTF("[ERROR]\t\tAnycast address out of range.\n");
		}
	} else {
		PRINTF("[ERROR]\t\tSend buffer full!\n");
	}	
}
/*---------------------------------------------------------------------------*/
void 
anycast_close(struct anycast_conn *c)
{
	struct anycast_bind_address *s;
	while(list_length(c->bind_addrs) > 0) {
		s = list_chop(c->bind_addrs);
		PRINTF("[LOG]\t\tUnbinded anycast address: %u\n", s->address);
		memb_free(&anycast_mem, s);
	}
	netflood_close(&c->netflood_conn);
	mesh_close(&c->mesh_conn);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(address_process, ev, data)
{
  static struct etimer et;
	static struct anycast_conn *con = NULL;
	struct anycast_bind_address *s = NULL;	
	struct anycast_send_buffer *b = NULL; 
	uint8_t i = 0;
	rimeaddr_t addr;
	
	/* store anycast connection only during the first time */
	if(con == NULL){
		con = (struct anycast_conn *)data;
	}

	/* check everytime */
  rimeaddr_copy(&addr, &rimeaddr_node_addr);

  PROCESS_BEGIN();

  while(1) {
		/* delay 30 seconds */
    etimer_set(&et, CLOCK_SECOND * 5);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    PRINTF("[ADDR]\t\tRIME:%02X:%02X", addr.u8[1], addr.u8[0]);
		
		for(s = list_head(con->bind_addrs); s != NULL; s = s->next ) {
			PRINTF("\tANYCAST%u:%u", ++i, s->address);
  	}
		PRINTF("\n");

		/*DEBUG - send buffer*/
		i=0;
		for(b = list_head(send_buf); b != NULL; b = b->next ) {
      PRINTF("[BUF]\t\t(%u) %u:%u:'%s'\n", ++i, b->address, b->seq_number, b->data);
    }
  }

  PROCESS_END();
}


