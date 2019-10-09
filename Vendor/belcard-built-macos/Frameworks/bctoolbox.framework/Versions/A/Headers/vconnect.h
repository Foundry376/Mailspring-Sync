/*
vconnect.h
Copyright (C) 2017 Belledonne Communications SARL

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef BCTBX_VCONNECT
#define BCTBX_VCONNECT

#include <fcntl.h>

#include <bctoolbox/port.h>


#if !defined(_WIN32_WCE)
#include <sys/types.h>
#include <sys/stat.h>
#if _MSC_VER
#include <io.h>
#endif
#endif /*_WIN32_WCE*/


#ifndef _WIN32
#include <unistd.h>
#endif


#define BCTBX_VCONNECT_OK         0   /* Successful result */

#define BCTBX_VCONNECT_ERROR       -255   /* Some kind of socket error occurred */


#ifdef __cplusplus
extern "C"{
#endif


/**
 * Methods associated with the bctbx_vsocket_api_t.
 */
typedef struct bctbx_vsocket_methods_t bctbx_vsocket_methods_t;

/**
 * Methods pointer prototypes for the socket functions.
 */
struct bctbx_vsocket_methods_t {

	bctbx_socket_t (*pFuncSocket)(int socket_family, int socket_type, int protocol);
	int (*pFuncConnect)(bctbx_socket_t sock, const struct sockaddr *address, socklen_t address_len);
	int (*pFuncBind)(bctbx_socket_t sock, const struct sockaddr *address, socklen_t address_len);

	int (*pFuncGetSockName)(bctbx_socket_t sockfd, struct sockaddr *addr, socklen_t *addrlen);
	int (*pFuncGetSockOpt)(bctbx_socket_t sockfd, int level, int optname, 
							void *optval, socklen_t *optlen);
	int (*pFuncSetSockOpt)(bctbx_socket_t sockfd, int level, int optname, 
							const void *optval, socklen_t optlen);
	int (*pFuncClose)(bctbx_socket_t sock);
	char* (*pFuncGetError)(int err);
	int (*pFuncShutdown)(bctbx_socket_t sock, int how);



};


/**
 * Socket API structure definition
 */
typedef struct bctbx_vsocket_api_t bctbx_vsocket_api_t;
struct bctbx_vsocket_api_t {
	const char *vSockName;      					 /* Socket API name */
	const bctbx_vsocket_methods_t *pSocketMethods;   /* Pointer to the socket methods structure */
};



/**===================================================
 * Socket API methods prototypes.
 * The default implementation relies on libc methods.
 *====================================================*/


/**
 * Creates a socket.
 * @param  socket_family type of address structure
 * @param  socket_type   socket type i.e SOCK_STREAM
 * @param  protocol      protocol family i.e AF_INET
 * @return 				 Returns a socket file descriptor.          
 */
BCTBX_PUBLIC bctbx_socket_t bctbx_socket(int socket_family, int socket_type, int protocol);

/**
 * Get the local socket address.
 * Calls the function pointer pFuncGetSockName. The default method associated with this pointer 
 * is getsockname. 
 * @param  sockfd  socket descriptor 
 * @param  addr    buffer holding  the current address to which the socket sockfd is bound
 * @param  addrlen amount of space (in bytes) pointed to by addr
 * @return         On success, zero is returned.  On error, -1 is returned, and errno is
 *                 set appropriately.
 */
BCTBX_PUBLIC int bctbx_getsockname(bctbx_socket_t sockfd, struct sockaddr *addr, socklen_t *addrlen);
/**
 * Get socket options. 
 * @param  sockfd  socket file descriptor
 * @param  level   level of the socket option
 * @param  optname name of the option
 * @param  optval  buffer in which the value for the requested option(s) are to be returned
 * @param  optlen  contains the size of the buffer pointed to by optval, and on return 
 *                 contains the actual size of the value returned. 
 * @return         On success, zero is returned.  On error, -1 is returned, and errno is
 *                 set appropriately.
 */
BCTBX_PUBLIC int bctbx_getsockopt(bctbx_socket_t sockfd, int level, int optname, 
							void *optval, socklen_t *optlen);
/**
 * Set socket options.
 * @param  sockfd  socket file descriptor
 * @param  level   level of the socket option
 * @param  optname name of the option
 * @param  optval  buffer holding the value for the requested option
 * @param  optlen  contains the size of the buffer pointed to by optval
 * @return         On success, zero is returned.  On error, -1 is returned, and errno is
 *                 set appropriately.
 */
BCTBX_PUBLIC int bctbx_setsockopt(bctbx_socket_t sockfd, int level, int optname, 
							const void *optval, socklen_t optlen);

/**
 * Shut down part of a full duplex connection.
 * @param  sockfd socket file descriptor
 * @param  how    specifies what is shut down in the connection.
 * @return        On success, zero is returned.  On error, -1 is returned, and errno is
 *                set appropriately.
 */
BCTBX_PUBLIC int bctbx_shutdown(bctbx_socket_t sockfd, int how);

/**
 * Close a socket file descriptor.
 * @param  sockfd socket file descriptor
 * @return        0 on success , -1 on error and errno is set.
 */
BCTBX_PUBLIC int bctbx_socket_close(bctbx_socket_t sockfd);

/**
 * Assign a name to a socket. 
 * @param  sockfd      socket file descriptor to assign the name to.
 * @param  address     address of the socket
 * @param  address_len size of the address structure pointed to by address (bytes)
 * @return             On success, zero is returned.  On error, -1 is returned, and errno is
 *                     set appropriately.
 */
BCTBX_PUBLIC int bctbx_bind(bctbx_socket_t sockfd, const struct sockaddr *address, socklen_t address_len);

/**
 * Initialize a connection to a socket.
 * @param  sockfd      socket file descriptor
 * @param  address     address of the socket
 * @param  address_len size of the address structure pointed to by address (bytes)
 * @return             On success, zero is returned.  On error, -1 is returned, and errno is
 *                     set appropriately.
 */
BCTBX_PUBLIC int bctbx_connect(bctbx_socket_t sockfd, const struct sockaddr *address, socklen_t address_len);

/**
 * strerror equivalent.
 * When an error is returned on a socket operations, returns
 * the error description based on err (errno) value.
 * @param  err should be set to errno
 * @return     Error description
 */
BCTBX_PUBLIC char* bctbx_socket_error(int err);


/**
 * Set default bctbx_vsocket_api_t pointer pDefaultvSocket to my_vsocket_api
 * if it is not NULL, sets it to the standard API implementation otherwise.
 * By default, the global pointer is set to use bcvSocket implemented in vconnect.c 
 * @param my_vsocket_api Pointer to a bctbx_vsocket_api_t structure. 
 */
BCTBX_PUBLIC void bctbx_vsocket_api_set_default(bctbx_vsocket_api_t *my_vsocket_api);


/**
 * Returns the value of the global variable pDefaultvSocket,
 * pointing to the default bctbx_vsocket_api_t used.
 * @return Pointer to bctbx_vsocket_api_t set to operate as default.
 */
BCTBX_PUBLIC bctbx_vsocket_api_t* bctbx_vsocket_api_get_default(void);
	
/**
 * Return pointer to standard bctbx_vsocket_api_t implementation based on libc
 * functions.
 * @return  pointer to bcSocketAPI
 */
BCTBX_PUBLIC bctbx_vsocket_api_t* bctbx_vsocket_api_get_standard(void);


#ifdef __cplusplus
}
#endif


#endif /* BCTBX_VCONNECT */

