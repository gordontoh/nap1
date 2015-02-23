/**
 * \defgroup anycast Anycast Protocol
 * @{
 *
 * The anycast module send a data packet to a anycast server in the network.
 *
 * \sections channels Channels
 *
 * The anycast modules uses 4 channels; 1 for the netflood and 3 for the mesh.
 */

/**
 * \file
 *         Anycast header file
 * \author
 *         Wei Qiao Toh
 */

#ifndef __ANYCAST_H__
#define __ANYCAST_H__

#include "net/rime/netflood.h"
#include "net/rime/mesh.h"
#include "lib/list.h"


#define ANYCAST_TIMEOUT (CLOCK_SECOND * 10)
#define ANYCAST_RES_FLAG 0
#define ANYCAST_DATA_FLAG 1

/*
 * \brief	maximum length of data application is allowed to send
 */
#define ANYCAST_DATA_LEN 103

/* no anycast server replied to the request */
#define ERR_NO_SERVER_FOUND 0

/* mesh layer could not found a route for the data packet to the responded 
 * anycast server 
 */
#define ERR_NO_ROUTE 1

struct anycast_conn;

/* 1 byte anycast address */
typedef uint8_t anycast_addr_t;

/**
 * \brief     Anycast callbacks
 */
struct anycast_callbacks {  
 /**
 * \brief      Callback for received anycast data message
 * \param c    A pointer to a struct anycast_conn
 * \param originator The link-layer address of the sender
 * \param anycast_addr The anycast address of the server
 * \param data A pointer to the data received
 *
 * This function is called when the server receives an anycast data message.
 *
 */
  void (* recv)(struct anycast_conn *c, const rimeaddr_t * originator,
    const anycast_addr_t anycast_addr, char *data);
 /**
 * \brief      Callback for sent anycast data message
 * \param c    A pointer to a struct anycast_conn
 * \param anycast_addr The anycast address of the server
 * \param data A pointer to the data received
 *
 * This function is called when the actual data packet is sent after the nearest
 * server has been figured out.
 *
 */
  void (* sent)(struct anycast_conn *c,	const anycast_addr_t anycast_addr,
		 char *data);
 /**
 * \brief      Timeout callback
 * \param c    A pointer to a struct anycast_conn
 * \param err_code ERR_NO_SERVER_FOUND or ERR_NO_ROUTE
 *
 * This function is called when a timeout occurred. When no server supporting the anycast
 * destination address could be found, the error code is set to ERR_NO_SERVER_FOUND. When the mesh
 * layer could not found a route for the data message, the error code is set to ERR_NO_ROUTE.
 *
 */
  void (* timedout)(struct anycast_conn *c, const uint8_t err_code);
};

/**
 * \brief	Stores variables for an opened anycast connection
 */
struct anycast_conn {
  struct mesh_conn mesh_conn;
  struct netflood_conn netflood_conn;
  /* list of anycast addresses this server is listening on */
  LIST_STRUCT(bind_addrs);
  const struct anycast_callbacks *cb;
};

/**
 * \brief      Open an anycast connection
 * \param c    A pointer to a struct anycast_conn
 * \param channels The channel on which the netflood connection will operate on. (The channel
                    number passed to the mesh connection is channels + 1)
 * \param callbacks Pointer to callback structure
 *
 *             This function sets up an anycast connection on the
 *             specified channel. The caller must have allocated the
 *             memory for the struct anycast_conn, usually by declaring it
 *             as a static variable.
 *
 *             The struct anycast_callbacks pointer must point to a structure
 *             containing function pointers to functions that will be called
 *             when a packet arrives on the channel.
 *
 */
void anycast_open(struct anycast_conn *c, uint16_t channels,
	       const struct anycast_callbacks *callbacks);

/**
 * \brief      Add an anycast address to listen on
 * \param c    A pointer to a struct anycast_conn
 * \param anycast_addr The anycast address on which to listen
 * \retval 0 if anycast_addr could be successfully added to the list bind_addrs, -1 otherwise
 *
 *             This function adds an anycast address to the list of anycast addresses
 *             this server is listening on. This function must be called after anycast_open()
 *             and must be called separately for every anycast address the server supports.
 *
 */
int anycast_listen_on(struct anycast_conn *c, const anycast_addr_t anycast_addr);

/**
 * \brief      Send an anycast packet
 * \param c    The anycast connection on which the packet should be sent
 * \param dest The anycast address of the virtual host this packet should be sent to
 *
 *             This function sends an anycast packet. The packet must be
 *             present in the packetbuf before this function is called.
 *
 *             The parameter c must point to an anycast connection that
 *             must have previously been set up with anycast_open().
 *
 */
void anycast_send(struct anycast_conn *c, const anycast_addr_t dest);

/**
 * \brief      Close an anycast connection
 * \param c    A pointer to a struct anycast_conn
 *
 *             This function closes an anycast connection that has
 *             previously been opened with anycast_open().
 *
 *             This function typically is called as an exit handler.
 *
 */
void anycast_close(struct anycast_conn *c);

#endif /* __ANYCAST_H__ */
/** @} */
