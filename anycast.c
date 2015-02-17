/**
 * \file
 *         Anycast header file
 * \author
 *         Wei Qiao Toh
 */

#include "anycast.h"
#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "dev/leds.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h> /* For offsetof */

#define PACKET_TIMEOUT (CLOCK_SECOND * 10)
#define ANYCAST_RES_FLAG 0
#define ANYCAST_DATA_FLAG 1
#define ANYCAST_DATA_LEN 50

#define DEBUG 1
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define FLASH_LED(l){leds_on(l);clock_delay_msec(50);leds_off(l);clock_delay_msec(50);}

/* stores anycast address nodes listens on */
struct anycast_bind_address {
  struct anycast_bind_address *next;
  anycast_addr_t address;
};

/* for responding to an anycast request */
struct anycast_res {
	uint8_t flag;
	uint8_t seq_number;
	anycast_addr_t address;
};

/* for sending data to an anycast server */ 
struct anycast_data {
	uint8_t flag;
	anycast_addr_t address;
  char data[ANYCAST_DATA_LEN];
};

/* data structure for each requests made by application */
struct anycast_send_buffer {
	struct anycast_send_buffer *next;
	anycast_addr_t address;
	uint8_t seq_number;
	char data[ANYCAST_DATA_LEN];
	struct anycast_conn *conn;
	struct ctimer ctimer; 
};

/* listen to maximum of 5 anycast address */
MEMB(anycast_mem, struct anycast_bind_address, 5);

/* store maximum of 5 anycast send request */
MEMB(send_buf_mem, struct anycast_send_buffer, 5);

/* linked-list buffer that stores requests made by application*/
LIST(send_buf);

//static list_t send_buf;
  
/* sequence number which is incremented for each request by application */
static uint8_t seq_no = 0;

/*---------------------------------------------------------------------------*/
PROCESS(status_process, "Print addresses/requests buffer periodically");
/*---------------------------------------------------------------------------*/
void *
get_send_buf(	const anycast_addr_t addr, 
							const uint8_t seq_no)
{
	struct anycast_send_buffer *s_buf;

  for(s_buf = list_head(send_buf); s_buf != NULL; s_buf = s_buf->next ) {
		if(s_buf->address == addr && s_buf->seq_number == seq_no) {
			list_remove(send_buf, s_buf);
			return s_buf;
		}
  }
	return NULL;
}
/*---------------------------------------------------------------------------*/
static void
expire_buf_element(void *n)
{
  struct anycast_send_buffer *s_buf = n;
	
	PRINTF("[LOG]\t\tBuffer entry expired -> %u:%u:'%s'\n", 
					s_buf->address, 
					s_buf->seq_number, 
					s_buf->data);
  
	s_buf->conn->cb->timedout(s_buf->conn, ERR_NO_SERVER_FOUND);
	list_remove(send_buf, s_buf);
  memb_free(&send_buf_mem, s_buf);
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
}
/*---------------------------------------------------------------------------*/
static void netflood_dropped(struct netflood_conn *c)
{
  //PRINTF("[ERROR]\t\tNetFlood packet dropped !\n");
}
/*---------------------------------------------------------------------------*/
static void mesh_sent(struct mesh_conn *c)
{
	struct anycast_data *a_data;
	uint8_t flag = (uint8_t) *((char *)packetbuf_dataptr());
	struct anycast_conn *a_conn = (struct anycast_conn *)
          ((char *)c - offsetof(struct anycast_conn, mesh_conn));
	
	if(flag == 1) {
		a_data = (struct anycast_data *)packetbuf_dataptr();
		if(a_conn->cb->sent) {
    	a_conn->cb->sent(a_conn, a_data->address, a_data->data);
  	}
	}
}
/*---------------------------------------------------------------------------*/
static void mesh_timedout(struct mesh_conn *c)
{
	struct anycast_conn *a_conn = (struct anycast_conn *)
  				((char *)c - offsetof(struct anycast_conn, mesh_conn));
	
  PRINTF("[LOG]\t\tMesh packet timedout.\n");

	if(a_conn->cb->timedout) {
		a_conn->cb->timedout(a_conn, ERR_NO_ROUTE);	
	}
}
/*---------------------------------------------------------------------------*/
static void mesh_recv(struct mesh_conn *c, 
											const rimeaddr_t *from, 
											uint8_t hops)
{
	//PRINTF("[LOG]\t\tMesh received (%02X:%02X).\n", from->u8[1], from->u8[0]);
	
	/* get first byte to determine whether it's a RES or DATA */
	uint8_t flag = (uint8_t) *((char *)packetbuf_dataptr());
	
	if(flag == ANYCAST_RES_FLAG){
		struct anycast_res *res = (struct anycast_res *)packetbuf_dataptr();	
		struct anycast_send_buffer *s_buf;
		struct anycast_data a_data;
		
		PRINTF("[LOG]\t\tAnycast server %u at %02X:%02X\n", 
						res->address, 
						from->u8[1], 
						from->u8[0]);
	
		s_buf = (struct anycast_send_buffer *)get_send_buf(res->address, res->seq_number);
		if(s_buf != NULL) {
			PRINTF("[LOG]\t\tSending data '%s'...\n", s_buf->data);
			
			a_data.flag = 1;
			a_data.address = s_buf->address;
			strcpy(a_data.data, s_buf->data);

			packetbuf_copyfrom((char *)&a_data, sizeof(a_data));
			mesh_send(c ,from);
			
			PRINTF("[BUF]\t\tRemoved %u:%u:'%s' from send buffer.\n", 
							s_buf->address, 
							s_buf->seq_number, 
							s_buf->data);
			
			/* stop call backtimer since data has already been sent */ 
			ctimer_stop(&s_buf->ctimer);

			/* free-up memory */
			memb_free(&send_buf_mem, s_buf);

		} else {
			PRINTF("[WARNING]\t\tRespond from Anycast Server %u(%02x:%02X) ignored.\n", 
							res->address, 
							from->u8[1], 
							from->u8[0]);
		}
	} else if (flag == ANYCAST_DATA_FLAG) {
		struct anycast_data	*a_data = (struct anycast_data *)packetbuf_dataptr();
		struct anycast_conn *a_conn = (struct anycast_conn *)
    				((char *)c - offsetof(struct anycast_conn, mesh_conn));
		
		PRINTF("[LOG]\t\tAnycast data '%s' received from %02X:%02X\n",
						a_data->data,
						from->u8[1], 
						from->u8[0]);

		/* callback to application to notify data received */
		a_conn->cb->recv(a_conn, from, a_data->address, a_data->data);
	}			
}
/*---------------------------------------------------------------------------*/
static const struct netflood_callbacks netflood_call = 
				{ netflood_recv, netflood_sent, netflood_dropped };
static const struct mesh_callbacks mesh_call = 
				{ mesh_recv, mesh_sent, mesh_timedout };
/*---------------------------------------------------------------------------*/
void 
anycast_open(	struct anycast_conn *c, 
							uint16_t channels, 
							const struct anycast_callbacks *callbacks)
{
	/* opens netflood connection for anycast server lookup */
	netflood_open(&c->netflood_conn, CLOCK_SECOND * 2, channels, &netflood_call);
	
	/* opens mesh connection for sending respond or data */
  mesh_open(&c->mesh_conn, channels+1, &mesh_call);
  
	c->cb = callbacks;
	
	/* initialize and allocate memory for lists */
	LIST_STRUCT_INIT(c, bind_addrs);
	memb_init(&anycast_mem);
	memb_init(&send_buf_mem);
	
	/* process for printing rime address, anycast address and send buffer */
	if(DEBUG) {
		process_start(&status_process, (char *)c);
	}
}
/*---------------------------------------------------------------------------*/
int 
anycast_listen_on(struct anycast_conn *c, 
									const anycast_addr_t anycast_addr)
{
	static struct anycast_bind_address *bind_addr;

  bind_addr = memb_alloc(&anycast_mem);
  if(bind_addr != NULL) {	
		bind_addr->address = anycast_addr;
		list_add(c->bind_addrs, bind_addr);
		
		PRINTF("[LOG]\t\tBinded anycast addr %u \n", 
						bind_addr->address);
		
		return 0;
	}

	return -1;
}
/*---------------------------------------------------------------------------*/
void 
anycast_send(	struct anycast_conn *c, 
							const anycast_addr_t dest)
{
	static struct anycast_send_buffer *s_buf;
	char addr_buf[2];

	s_buf = memb_alloc(&send_buf_mem);
	if(s_buf != NULL) {
		if(dest>0 && dest<256) {
			/* store data in buf first */
			if(sizeof((char *)packetbuf_dataptr()) > ANYCAST_DATA_LEN){
				PRINTF("[ERROR]\t\tData length out of range.");
				return;
			}

			s_buf->address = dest;
			s_buf->seq_number = seq_no++;
			s_buf->conn = c;
			snprintf(s_buf->data, packetbuf_datalen(), "%s", (char *)packetbuf_dataptr());
			
			PRINTF("[LOG]\t\tReceived anycast send. server:%u|seq:%u|data:'%s'\n",
							s_buf->address, 
							s_buf->seq_number, 
							s_buf->data);

			list_add(send_buf, s_buf);
			ctimer_set(&s_buf->ctimer, CLOCK_SECOND * 10, expire_buf_element, s_buf);
			
			snprintf(addr_buf, 2, "%c", (uint8_t) s_buf->address);
    	packetbuf_copyfrom(addr_buf, sizeof(addr_buf));
			netflood_send(&c->netflood_conn, (uint8_t) s_buf->seq_number);
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
		
		PRINTF("[LOG]\t\tUnbinded anycast address: %u\n", 
						s->address);
		
		memb_free(&anycast_mem, s);
	}
	
	netflood_close(&c->netflood_conn);
	mesh_close(&c->mesh_conn);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(status_process, ev, data)
{
  static struct etimer et;
	static struct anycast_conn *a_conn = NULL;
	struct anycast_bind_address *s = NULL;	
	struct anycast_send_buffer *b = NULL; 
	uint8_t i = 0;
	char buf[100];
	rimeaddr_t addr;
	
	/* store anycast connection only during the first time */
	if(a_conn == NULL){
		a_conn = (struct anycast_conn *)data;
	}
	
	/* check everytime */
  rimeaddr_copy(&addr, &rimeaddr_node_addr);

  PROCESS_BEGIN();

  while(1) {
		/* delay 30 seconds */
    etimer_set(&et, CLOCK_SECOND * 10);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

		snprintf(buf, 100, "[ADDR]\t\tRIME:%02X:%02X", addr.u8[1], addr.u8[0]);	
	
		for(s = list_head(a_conn->bind_addrs); s != NULL; s = s->next ) {
			snprintf(buf, 100, "%s | ANYCAST%u:%u", buf, ++i, s->address);	
  	}
		
		PRINTF("%s\n", 
						buf);

		i=0;
		for(b = list_head(send_buf); b != NULL; b = b->next ) {
      PRINTF("[BUF]\t\t(%u) %u|%u|'%s'\n", 
							++i, 
							b->address, 
							b->seq_number, 
							b->data);
    }
  }

  PROCESS_END();
}


