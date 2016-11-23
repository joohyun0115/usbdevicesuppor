/*
 * Agent proxy for android  
 *
 * This file is based in part on 
 * 		Agent-Proxy
 *
 * Copyright (C) 2011 Sevencore, Inc.
 * 	Author: Joohyun Kyong <joohyun0115@gmail.com>
 * Copyright (c) 2005-2010 Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifdef _WIN32
#include <winsock.h>
#else /* ! _WIN32 */
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif

#define FEATURE_PORT_USB

/* Port types */
#define PORT_TCP       0x1
#define PORT_UDP       0x2
#define PORT_RS232     0x4
#define STDINOUT       0x8
#define PORT_LISTEN    0x10
#define PORT_FIFO_CON  0x20

#ifdef FEATURE_PORT_USB
#define PORT_USB  0x40
#endif

/* constants */
#define IO_BUFSIZE 8192
#define NAMESIZE 256
#define IAC 255

struct port_st {
	int type;
	int cls;		/* define if it is the local port or not */
	int sock;		/* Socket handle */
	int isLocal;		/* Is this the local side or the remote side? */
	struct port_st *peer;	/* direct read and write local to remote */
	struct port_st *remote;	/* The remote clone to attach to attach a local to */
	int inIAC;		/* Processing an IAC sequence */

	/* Script specific variables */
	struct port_st *scriptRef;	/* A script port refrence */
	int mode;		/* Mode of operation of a script port */
	int lmode;		/* Mode of a local port */
	int rmode;		/* Mode of a remote port */
	int scriptInUse;	/* States whether or not the script connection is in use */
	struct port_st *lscript;	/* The local side of a script connection */
	struct port_st *rscript;	/* The remote side of a script connection */
	struct port_st *clients;	/* The series of clients connected to the
					 * script port 
					 */
	struct port_st *clientNext;
	int breakPort;		/* Send an alternate break sequence in place of a
				 * tcp break or ^C 
				 */
	/* End script specific variables */

	int port;		/* Port number of udp or tcp connection */
	char name[NAMESIZE];
	char buf[IO_BUFSIZE];	/* buffer for io operations */
	int (*readMessage) (struct port_st *);
	void (*portclose) (struct port_st *);
	int (*portread) (struct port_st *, char *, int, int);
	int (*portwrite) (struct port_st *, char *, int, int);
	struct sockaddr_in serv_addr;

	struct port_st *next;
};
extern fd_set master_rds;

void rs232_portclose(struct port_st *port);
int rs232_portread(struct port_st *port, char *buf, int size, int opts);
int rs232_portwrite(struct port_st *port, char *buf, int size, int opts);

#ifdef FEATURE_PORT_USB
int usb_portopen(struct port_st *port);
void usb_portclose(struct port_st *port);
int usb_portread(struct port_st *port, char *buf, int size, int opts);
int usb_portwrite(struct port_st *port, char *buf, int size, int opts);
#endif

#ifdef linux
#define HAVE_TERMIOS
#endif /* linux */
#ifdef solaris
#define HAVE_TERMIOS
#endif /* solaris */
#ifdef HAVE_TERMIOS
#include <termios.h>
#endif /* HAVE_TERMIOS */
#ifndef _WIN32
int setbaudrate(int sock, int baud);
int setstopbits(unsigned int sock, char *stopbits);
int setcondefaults(unsigned int sock);
#endif /* ! _WIN32 */
