/*
 *  common.c
 *  Common - Platform independance layer for TS7250, T75x0.
 *
 *  Created by Martin Robinson on 02/05/2010.
 *  Copyright 2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * $Revision$
 */

#include "common.h"
#include <stdio.h>      // for FILE
#include <stdlib.h>     // for exit()
#include <time.h>       // for timeval
#include <string.h>     // for strlen etc
#include <termios.h>    // for termios
#include <fcntl.h>      // for O_RDWR
#include <netdb.h>      // for sockaddr_in
#include <errno.h>      // For ETIMEDOUT
#include <sys/stat.h>	// for stat
#include <sys/mman.h>	// for PROT_READ
#include <netinet/ip.h>	// for TOSIP_LOWDELAY
#include <netinet/tcp.h>	// for TCP_NODELAY
#include <sys/socket.h>		// for SHUT_RDWR
#include <unistd.h>		// for write 
#include <assert.h>

#include "../Common/sbus.h"		// For the TS7550 stuff

#ifndef TRUE
#define TRUE (1)
#define FALSE (0)
#endif

extern int debug;
extern FILE * logfp;
extern int sockfd[];
extern int noserver;
extern int numretries;
extern int retrydelay;
extern int controllernum;
extern const char * progname;
#ifndef linux
extern
#endif
int errno;

char buffer[206];	// General messages

enum Platform platform = undefPlatform;
#define TS7500REDLEDMASK 0x4000
#define TS7500GREENLEDMASK 0x8000
#define TS7250REDLEDMASK 2
#define TS7250GREENLEDMASK 1

// Locals
void blinkLED_ts72x0(int state, int which);
void blinkLED_ts75x0(int state, int which);
void blinkLED(int state, int which);
int openSerialDevice(const char * name, int baud, int parity, int databits, int stopbits);
int openSerialSocket(const char * fullname);
int openXuart(const char * name, int baud, int parity, int databits, int stopbits);

/**********/
/* LOGMSG */
/**********/
void logmsg(int severity, char *msg) {
	// Write error message to logfile and socket if possible and abort program for FATAL
	// Truncate total message including timestamp and 'event ' to 206 bytes.
	
	// Globals used: sockfd logfp
	
	// Due to the risk of looping when you call this routine due to a problem writing on the socket,
	// set sockfd to 0 first.
	
	char buffer[206];	// This MUST be local!
	time_t now;
	if (strlen(msg) > 174) msg[174] = '\0';
	now = time(NULL);
	strcpy(buffer, ctime(&now));
	buffer[24] = ' ';       // replace newline with a space
	strcat(buffer, msg);
	strcat(buffer, "\n");
	DEBUG printf("LOGMSG ");
	if (logfp && severity > INFO) { // don't even log INFO to the file
		fputs(buffer, logfp);
		DEBUG printf("File ");
		fflush(logfp);
	}
	if (sockfd[0] > 0) {
		strcpy(buffer, "event ");
		strcat(buffer, msg);
		DEBUG printf("Socket ");
		sockSend(sockfd[0], buffer);
	}
	DEBUG puts(buffer);
	if (severity > ERROR) {              // If severity is FATAL terminate program
		if (logfp) fclose(logfp);
		DEBUG printf("LOGMSG Closing log file\n");
		exit(severity);
	}
}

/**************/
/* GETVERSION */
/**************/
char *getVersion(const char * revision) {
	// return pointer to version part of REVISION macro
	static char version[10] = "";	// Room for xxxx.yyyy
	if (!strlen(version)) {
		strcpy(version, revision+11);
		version[strlen(version)-2] = '\0';
	}
	return version;
}

/**********/
/* DECODE */
/**********/
void decode(char * msg) {
	// Algorithm - each byte X-ored' with a successively higher integer.
	char * cp;
	char i = 0;
	for (cp = msg; *cp; cp++) putchar(*cp ^ i++);
	putchar('\n');
}

/**************/
/* OPENSERIAL */
/**************/
int openSerial(const char * name, int baud, int parity, int databits, int stopbits) {
// Choose wich type of device to open: /dev/tty, hostname:socket or xuartX
	if (strchr(name, ';'))
		return openSerialSocket(name);
	
	if (name[0] == '/')
		return openSerialDevice(name, baud, parity, databits, stopbits);
	
	return openXuart(name, baud, parity, databits, stopbits);
}

/*************/
/* OPENXUART */
/*************/

int openXuart(const char * name, int baud, int parity, int databits, int stopbits) {
	struct sockaddr_in sa;
	struct hostent *he;
	struct linger lx;
	int baudrate = 9600;
	char parity_c;
	char nbuf[32];
	int num_bits = 8;
	int retval, retval2, sk, tos, portnum, nodelay;
	
	if (strncmp(name, "xuart", 5) != 0) {
		snprintf(buffer, sizeof(buffer), "FATAL (Common) Device name is not xuart: '%s'", name);
		logmsg(FATAL, buffer);
		return -1;
	}
	portnum = strtol(name + 5, NULL, 0) + 7350;	// port number 7350 upwards.
	DEBUG fprintf(stderr, "Opening Xuart %d ", portnum);
	switch(baud) {
		case 0: baudrate = 0; break;			// Don't change it.
		case B300: baudrate = 300; break;		// Steca
		case B1200: baudrate = 1200; break;		// SMA
		case B2400: baudrate = 2400; break;		// Elster, Victron, Rico
		case B9600: baudrate = 9600; break;		// Resol, Owl, Soladin
		case B19200: baudrate = 19200; break;	// Davis, Fronius
		case B115200: baudrate = 115200; break;	// Console
		default: snprintf(buffer, sizeof(buffer), "ERROR (Common) Unsupported Baud rate %d", baud);
			logmsg(ERROR, buffer);
			baudrate = 0;
	}
	
	switch(databits) {
		case CS8: num_bits = 8; break;
			logmsg(ERROR, buffer);
		default: snprintf(buffer, sizeof(buffer), "ERROR (Common) Unsupported number of Stop Bits: %d", databits);
			logmsg(ERROR, buffer);
			num_bits = 8;
			
	}
	switch(parity) {
		case 0:	parity_c = 'n';	break;
		default: snprintf(buffer, sizeof(buffer), "ERROR (Common) Unsupported parity %d", parity);
			logmsg(ERROR, buffer);
			parity_c = 'n';
	}
	
	// Must be a xuart.
	sa.sin_family = AF_INET;
	if (!(he = gethostbyname("localhost"))) {
		logmsg(FATAL, "FATAL (Common) Can't resolve localhost");
		return -1;	// To satisfy static code checker
	}
	
	//	assert(he != NULL);
	memcpy(&sa.sin_addr, he->h_addr_list[0], he->h_length);
	sa.sin_port = htons(portnum);
	//	assert(sa.sin_port != 0);
	if (!(sk = socket(PF_INET, SOCK_STREAM, 0))) {
		logmsg(ERROR, "ERROR (Common) Can't open socket");
		return -1;
	}
	//	assert(sk != -1);
	lx.l_onoff = 1;
	lx.l_linger = 15;
	setsockopt(sk, SOL_SOCKET, SO_LINGER, &lx, sizeof(lx));
	retval = connect(sk, (struct sockaddr *)&sa, sizeof(sa));
	
	if (retval) {
		snprintf(buffer, sizeof(buffer), "ERROR (Common) Couldn't connect to Xuart port %d", portnum);
		logmsg(ERROR, buffer);
		return -1;
	}
	
	if(baudrate) {
		retval = snprintf(nbuf, sizeof(nbuf), 
						  "%d@%d%c%d", baudrate, num_bits, parity_c, stopbits);
		// DEBUG fprintf(stderr,"Initialising '%s' ", nbuf);
		// Send the first byte of the message as Out-of-band data;
		// Send the rest including the terminating NULL as in-band.
		retval2 = send(sk, nbuf, 1, MSG_OOB);
		assert(retval2 == 1);
		// DEBUG fprintf(stderr, "Wrote %d OOB ", retval2);
		retval2 = write(sk, &nbuf[1], retval);
		assert(retval2 == retval);
		// DEBUG fprintf(stderr, "Wrte rest: %d\n", retval2);
		//		assert (retval == retval2);
		shutdown(sk, SHUT_RDWR);
		close(sk);
		sk = socket(PF_INET, SOCK_STREAM, 0);
		retval = connect(sk, (struct sockaddr *)&sa, sizeof(sa));
		
		if (retval) {
			snprintf(buffer, sizeof(buffer), "ERROR (Common) Can't connect after setting baud rate '%s'", nbuf);
			logmsg(ERROR, buffer);
			return -1;
		}
	}
	tos = IPTOS_LOWDELAY;
	setsockopt(sk, IPPROTO_IP, IP_TOS, &tos, 4);
	nodelay = 1;
	setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, &nodelay, 4);
	retval = fcntl(sk, F_SETFL, O_NONBLOCK);
	assert(retval != -1);
	return sk;
}

/********************/
/* OPENSERIALDEVICE */
/********************/
	
int openSerialDevice(const char * name, int baud, int parity, int databits, int stopbits) {
	/* open serial device; return file descriptor or -1 for error (see errno) */
	int fd, res;
	struct termios newSettings;
	
	if (strchr(name, ':')) 
		return openSerialSocket(name);
	
	if ((fd = open(name, O_RDWR | O_NOCTTY)) < 0) return fd;        // an error code
	
	bzero(&newSettings, sizeof(newSettings));
	// Control Modes
	newSettings.c_cflag = databits | CLOCAL | CREAD; // CRTSCTS stops it working on TS-SER1 & TS-SER4
	if (stopbits == 2)
		newSettings.c_cflag |= CSTOPB;
	// input modes
	cfsetspeed(&newSettings, baud);
	newSettings.c_iflag = IGNPAR;   //input modes
	newSettings.c_oflag = 0;                // output modes
	newSettings.c_lflag = 0;                // local flag
	newSettings.c_cc[VTIME] = 0; // intercharacter timer */
    newSettings.c_cc[VMIN] = 0;     // non-blocking read */
	tcflush(fd, TCIFLUSH);          // discard pending data
	//      cfsetospeed(&newSettings, baud);
	if((res = tcsetattr(fd, TCSANOW, &newSettings)) < 0) {
		close(fd);      // if there's an error setting values, return the error code
		return res;
	}
	return fd;
}

/********************/
/* OPENSERIALSOCKET */
/********************/
// Return an open fd or -1 for error
// Expects name to be hostname:portname where either can be a name or numeric.
// Need to avoid overwriting/alterng input string in case of re-use
int openSerialSocket(const char * fullname) {
	const char * portname;
	char name[64];
	int fd;
	struct sockaddr_in serv_addr;
    struct hostent *server;
	struct servent * portent;
	int port;
	int firstime = 1;
	
	portname = strchr(fullname, ':');
	if (!portname) return -1;	// No colon in hostname:portname
	if (portname - fullname > 64) {
		snprintf(buffer, sizeof(buffer), "ERROR %s port name is too long: '%s'\n", progname, fullname);
		logmsg(ERROR, buffer);
		return -1;
	}
	strncpy(name, fullname, portname - fullname);
	name[portname - fullname] = 0;
	portname++;		// Now portname point to port part and name is just host part.
	
	server = gethostbyname(name);
	if (!server) {
		snprintf(buffer, sizeof(buffer),"ERROR %s Cannot resolve hostname %s\n", progname, name);
		logmsg(ERROR, buffer);	// Won't return
		return -1;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, 
		  (char *)&serv_addr.sin_addr.s_addr,
		  server->h_length);
	port = atoi(portname);		// Try it as a number first
	if (!port) {
		portent = getservbyname(portname, "tcp");
		if (portent == NULL) {
			snprintf(buffer, sizeof(buffer),"ERROR %s Can't resolve port: %s\n", progname, portname);
			logmsg(ERROR, buffer);	// Won't return
			return -1;
		}
		serv_addr.sin_port = portent->s_port;
	}
	else
		serv_addr.sin_port = htons(port);
	
	DEBUG fprintf(stderr, "Connect to %s:%d ", name, ntohs(serv_addr.sin_port));
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		snprintf(buffer, sizeof(buffer), "FATAL %s Can't create socket: %s", progname, strerror(errno));
		logmsg(FATAL, buffer);		// Won't return
		return -1;
	}
	DEBUG fprintf(stderr, "About to connect on %d ..", fd);
	// If we can't connect due to timeout (far end not ready) keep trying forever
	while (connect(fd, (struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
		DEBUG fprintf(stderr, "Connect fd%d ... sin_family %d sin_port %d sin_addr 0x%x ", fd, 
					  serv_addr.sin_family, serv_addr.sin_port, serv_addr.sin_addr.s_addr);
		if (firstime && errno == ETIMEDOUT) {
			firstime = 0;
			snprintf(buffer, sizeof(buffer),"INFO %s Error connecting to remote serial %s - will keep trying %d (%s)\n", 
					progname, fullname, errno, strerror(errno));
			logmsg(WARN, buffer);
		}
		
		if (errno != ETIMEDOUT) {
			snprintf(buffer, sizeof(buffer),"WARN %s Error other than time out connecting to remote serial: %s", progname, strerror(errno));
			logmsg(WARN, buffer);
			sleep(60);
		}
		if (errno == ETIMEDOUT) {
			fd = close(fd);		// necessary in order to reuse file desciptors.
			if (fd < 0){
				snprintf(buffer, sizeof(buffer), "WARN %s Can't close socket: %s", progname, strerror(errno));
				logmsg(WARN, buffer);		// Won't return
				return -1;
			}
			fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd < 0) {
				snprintf(buffer, sizeof(buffer), "WARN %s Can't create socket after closing: %s", progname, strerror(errno));
				logmsg(WARN, buffer);		// Won't return
				return -1;
			}
		}
	}
	DEBUG fprintf(stderr, "Connected on FD%d\n", fd);
	return fd;
}

/***************/
/* CLOSESERIAL */
/***************/
void closeSerial(int fd) {
	close(fd);
}

/************/
/* SOCKSEND */
/************/
void sockSend(const int fd, const char * msg) {
	// Send the string to the server.  May terminate the program if necessary
	short int msglen, written;
	int retries = numretries;
	
	if (noserver) {
		puts(msg);
		return;
	}
	
	msglen = strlen(msg);
	written = htons(msglen);
	if (write(fd, &written, 2) != 2) { // Can't even send length ??
		sockfd[0] = 0;             // prevent logmsg trying to write to socket!
		snprintf(buffer, sizeof(buffer), "ERROR %s Can't write a length to socket", progname);
		logmsg(ERROR, buffer);
	}
	while ((written = write(fd, msg, msglen)) < msglen) {
		// not all written at first go
		msg += written; msglen =- written;
		DEBUG printf("Socksend: Only wrote %d; %d left \n", written, msglen);
		if (--retries == 0) {
			char buffer[50];
			snprintf(buffer, sizeof(buffer), "WARN %s %d Timed out writing to server", progname, controllernum);
			logmsg(WARN, buffer);
			return;
		}
		usleep(retrydelay);
	}
}

/***************/
/* OPENSOCKETS */
/***************/
void openSockets(int servers, int max, char * logon, char * revision, char * extra) {
	struct sockaddr_in serv_addr;
    struct hostent *server;
	int i;

	if (!noserver) {
		if (servers == 0) return;		// It's a slave so exit immediately.
		server = gethostbyname(HOSTNAME);
		if (server == NULL) {
			snprintf(buffer, sizeof(buffer), "FATAL %s %d Cannot resolve localhost", progname, controllernum);
			logmsg(FATAL, buffer);
			return;		// To satisfy static code checker
		}
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		bcopy((char *)server->h_addr,
			  (char *)&serv_addr.sin_addr.s_addr,
			  server->h_length);
		serv_addr.sin_port = htons(PORTNO);
		
		for(i = 0; i < servers; i++) {
			sockfd[i] = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd[i] < 0) {
				snprintf(buffer, sizeof(buffer), "FATAL %s %d Creating socket", progname, controllernum);
				logmsg(FATAL, buffer);
			}
			if (connect(sockfd[i],(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
				sockfd[i] = 0;
				snprintf(buffer, sizeof(buffer), "FATAL %s %d Connecting to socket %d", progname, controllernum, i);
				logmsg(FATAL, buffer);
				
			}
			// Logon to server
			snprintf(buffer, sizeof(buffer), "logon %s %s %d %d %s", logon, getVersion(revision), getpid(), controllernum + i, extra);
			sockSend(sockfd[i], buffer);
			
		}
	} else sockfd[0] = 1;              // noserver - use stdout
}


/************/
/* BLINKLED */
/************/
void blinkLED(int state, int which) {
// This platform-independant version calls the right one.
// It blinks the RED led to indicate serial traffic
	// or the green LED to indicate MCP activity.
	if (platform == undefPlatform) 
		determinePlatform();
	switch(platform) {
		case ts72x0: blinkLED_ts72x0(state, which);
			break;
		case ts75x0:	blinkLED_ts75x0(state, which);
			break;
		default:
			snprintf(buffer, sizeof(buffer), "INFO %s (BlinkLED) Platform not yet determined", progname);
	}
}

void blinkLED_ts72x0(int state, int which) {
#ifdef linux
#define DATA_PAGE 0x80840000
#define LED 0x0020
	static volatile unsigned char *dr_page;
	static int devmem=0;
	int mask;
	if (devmem == 0) {
		devmem = open("/dev/mem", O_RDWR|O_SYNC);
		if (devmem == -1) return;
#ifdef linux	
		dr_page = (unsigned char *)mmap(0, getpagesize(), PROT_READ|PROT_WRITE
										, MAP_SHARED, devmem, DATA_PAGE);
#endif
		if (dr_page == MAP_FAILED) { 
			// close(devmem); 
			return;
		}
	}
	if (which == REDLED) mask = TS7250REDLEDMASK;
	if (which == GREENLED) mask = TS7250GREENLEDMASK;
	if (state) {	// Light LED
		*(dr_page + LED) |= mask;
	}
	else *(dr_page + LED) &= ~mask;
	DEBUG3 fprintf(stderr, "TS7250 Blinked %d %s ", which, state?"On":"Off");
	// We don't ever unmap() or close(). 
	// munmap(dr_page, getpagesize());
	// close(devmem);
#endif
}

void blinkLED_ts75x0(int state, int which) {
#ifdef linux
	int mask;
	sbuslock();
	if (which == REDLED) mask = TS7500REDLEDMASK;
	if (which == GREENLED) mask = TS7500GREENLEDMASK;
	if (state)
		sbus_poke16(0x62, sbus_peek16(0x62) | mask);
	else
		sbus_poke16(0x62, sbus_peek16(0x62) & ~mask);
	sbusunlock();
	DEBUG3 fprintf(stderr, "TS7550 Blinked %d %s ", which, state?"On":"Off");
#endif
}

#define MODEL "/bin/model"

/**********************/
/* DETERMINE PLATFORM */
/**********************/
void determinePlatform(void) {
	// Establish platform
	// Firstly from /bin/model if it exists and is executable
	// or if not executable, its contents (assumed text)
	FILE * fp;
	int popened = FALSE;
	struct stat s_stat;
	char modelstr[20];
	modelstr[0] = 0;
	if (stat(MODEL, &s_stat) == 0) {		// it exists
		if (s_stat.st_mode & S_IXUSR) {		// .. and is executable
			if (!(fp = popen(MODEL, "r"))) {	// open succeeded
				perror("Failed to read from execution of " MODEL);
				return;
			}
			DEBUG2 fprintf(stderr, "Executing " MODEL " ");
			popened = TRUE;
		}
		else 
		{	// It's not executable so read its contents
			if (!(fp = fopen(MODEL, "r"))) {
				perror("Failed to read from contents of " MODEL);
				return;
			}
			DEBUG2 fprintf(stderr, "Reading " MODEL " ");
		}
		fgets(modelstr, sizeof(buffer), fp);
		if (strncasecmp(modelstr, "ts7250", 6) == 0){
			platform = ts72x0; 
			DEBUG fprintf(stderr, "Platform set to ts72x0\n");
			goto cleanup;
		} else if (strncasecmp(modelstr, "ts7550", 6) == 0) {
			platform = ts75x0; 
			DEBUG fprintf(stderr, "Platform set to ts75x0\n");
			goto cleanup;
		} else {
			snprintf(buffer, sizeof(buffer), "WARN %s Unrecognised model type '%s'", progname, modelstr);
			logmsg(WARN, buffer);
			goto cleanup;
		}
	}	// doesn't exist.  Try uname -r
	if (!(fp = popen("uname -r", "r"))) {
		perror("Can't open uname -r");
		return;
	}
	popened = TRUE;
	fgets(modelstr, sizeof(modelstr), fp);
	if (strncmp(modelstr, "2.4.26", 6) == 0) {
		platform = ts72x0; 
		DEBUG fprintf(stderr, "Platform set to ts72x0\n");
		goto cleanup;
	} else if (strncmp(modelstr, "2.6.24", 6) == 0) {
		platform = ts75x0; 
		DEBUG fprintf(stderr, "Platform set to ts75x0\n");
		goto cleanup;
	}
	snprintf(buffer, sizeof(buffer), "WARN %s Can't determine platform - guessing TS7250", progname);
	platform = ts72x0;
cleanup:
	if (popened) // Nasty - although the FP can be read, it must be closed with pclose if popen was used
				// and fclose if fopen was used.
		pclose(fp);
	else
		fclose(fp);
}
