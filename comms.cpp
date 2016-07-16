/*
 *  comms.cpp
 *  mcp1.0
 *
 *  Created by Martin Robinson on 18/09/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010,2011 Wattsure Ltd. All rights reserved.
 *
 *	Socket handling 
 *
 */

#include <sys/socket.h>		// for socket
#include <netinet/in.h>	// for sockaddr_in
#include <netdb.h>		// for hostent & gethostbyname
#include <stdio.h>		// for perror
#include <stdlib.h>		// for exit()
#include <strings.h>	// for bzero
#include <sys/select.h>		// for select
#include <unistd.h>			// for close()
#include <fcntl.h>		// for F_SETFD

#include <iostream>
	using std::cerr; using std::endl;
#include <sstream>
	using std::ostringstream;

#include "mcp.h"
#include "comms.h"
#include "meter.h"
#include "journal.h"
#include "config.h"
	
/* NON MEMBER FUNCTION */

extern Config conf;
extern char * meterStr[];
extern const char * modelStr[];
extern Journal journal;
extern MeterData meter[];
extern Current current;
extern int insecure;
	
int makeserversocket(int port) {
// All errors are fatal, so we use exit() instead of return
	int serverfd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverfd < 0) {
		perror("Can't open socket");
		exit(1);
	}
	struct sockaddr_in serverAddr;
	// SECURITY listen on localhost only instead of INADDR_ANY unless -i option given
	if (insecure) 
		serverAddr.sin_addr.s_addr = INADDR_ANY;
	else	{
		bzero(&serverAddr, sizeof(serverAddr));
		struct hostent * serverHostent = gethostbyname("localhost");
		if (serverHostent == NULL) {
			perror("Couldn't resolve localhost");
			exit(1);
		}
		bcopy((char *)serverHostent->h_addr, &serverAddr.sin_addr.s_addr, serverHostent->h_length);
	}
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	// Set REUSE ADDR to speed up restart.
	// 2011/01/02 - this must be before bind()
	int reuse = 1;
	if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
		fprintf(stderr, "Socket %d" , port);
		perror("Couldn't set REUSEADDR");
		exit(1);
	}
	if (bind(serverfd, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
		fprintf(stderr, "Port %d: ", port);
		perror("Couldn't bind Server socket");
		exit(1);
	}
	if (listen(serverfd, 2)) {	// 1 = backlog parameter
		fprintf(stderr, "Port %d: ", port);
		perror("Couldn't listen");
		exit(1);
	}
	return serverfd;
}

/* SOCKCONN_S */

char * sockconn_s::processRead(void) {
// do a read on the file descriptor. Assemble message into correct buf. When 
// message is complete, return pointer to buf.  While message is incomplete
// return NULL. Also returns NULL on an error. Be careful!

	short int shortlen;
	int len;
	if (fd == 0) {	
		fprintf(stderr, "Sockconn::processRead[] - zero FD\n");
		return NULL;	// Should not be called on an empty sockconn
	}
	if (remain == 0) { // very first read - want 2-byte count
		if (read(fd, &shortlen, 2) < 0) {
			perror("Failed to read length");
			return NULL;
		}
		len = remain = ntohs(shortlen);
		
		cp = &buf[0]; // reset internal pointer to start of buffer.
		SOCKETDEBUG fprintf(stderr, "Read length as %d ", len); fflush(stdout);
		if (len >= SOCKBUFSIZE) {
			DEBUG fprintf(stderr, "Warning - message is %d, longer than SOCKBUFSIZE (%d)", len, SOCKBUFSIZE);
			len = read(fd, &buf, SOCKBUFSIZE);
			DEBUG fprintf(stderr, " Actually got %d\n", len);
			close(fd);
			clear();
			return NULL;
		}
	};  // Fall through once header read. in process of reading another chunk
	
	len = read(fd, cp, remain);
	if (len <= 0) {	// since we did a select, reading 0 bytes means sockets has been closed.
		perror("Error reading from socket");
		SOCKETDEBUG cerr << "Closed FD " << fd << " : " ;
		close(fd);
		clear();
		SOCKETDEBUG cerr << fd << endl;
		return NULL;	// try to accept() again
	}
	remain -= len;
	cp += len;
	SOCKETDEBUG fprintf(stderr, "Read %d bytes %d remaining ", len, remain);
	
	if (remain == 0) {	// message is complete
		*cp = '\0';	// terminate buffer
		SOCKETDEBUG fprintf(stderr, "Got %d string as '%s'\n", len, buf);
	}
	return buf;
}

/* SOCKCONN */

void Sockconn::accept(int serverfd) {
// 
/* Logic is faulty here.  After an accept(), this puts the fd into the first available
sockconn. 
But we access the sockconn as if it were indexed by device number. 
Then logon is called with the FD number a sets the correctly indexed sockconn.fd.
There seems to be a risk of overwriting.
It probably works because we use fd_set to set every bit of readfd.
*/
	struct sockaddr_in clientAddr;
	int clientLen = sizeof(clientAddr);
	int fd = ::accept(serverfd, (struct sockaddr *) &clientAddr, (socklen_t *)&clientLen);
	if (fd < 0) {
		perror("Accept error");
		return; // continue;	// hopefully a non-fatal error.
	}
	fcntl(fd, F_SETFD, 1);	// close on exec.
	// find an empty sockconn;
	int i;
	for (i = 0; i < MAXSOCKETS; i++)
		if (sockconn[i].fd == 0) {
			sockconn[i].clear();
			sockconn[i].fd = fd;
			break;
		}
	if (i == MAXSOCKETS) {
		::close(fd);
		fprintf(stderr, "Run out of sockconn structures\n");
		return;
	}
	DEBUG fprintf(stderr, "Accepted Connection %d on FD %d\n", i, sockconn[i].fd);
}

void Sockconn::setfd(fd_set &fd) {
// Would be better to iterate around a map, but this likely to be faster
	SOCKETDEBUG cerr << "Sockconn::setfd ";
	for (int i = 0; i < MAXSOCKETS; i++)
		if (sockconn[i].fd) {
			SOCKETDEBUG cerr<< sockconn[i].fd << ' ';
			FD_SET(sockconn[i].fd, &fd);
		}
}

char * Sockconn::processRead(int i) { return sockconn[i].processRead();}
// Do a read into specified member of Sockconn, ie by device number

/*********/
/* LOGON */
/*********/
void Sockconn::logon(int fd, const char * msg) {
// Process logon.  Set device number in map con[].
	// 2.10 handles unit logon where controllernum is 0.0, 2.1 or similar.
	char typeStr[10];
	enum DeviceType type;
	int pid, device, unit, numread, major, minor;
	unit = 0;
	numread = sscanf(msg, "logon %9s %d.%d %d %d.%d", typeStr, &major, &minor, &pid, &device, &unit);
	if (numread != 5 && numread != 6) {
		fprintf(stderr, "Logon: only got %d fields not 5 or 6 from '%s'\n", numread, msg);
		return;
	}
	if (device < 0 || device >= MAXDEVICES) {
		cerr << "Socks.login: invalid device number " << device << " in " << msg << endl;
		return;
	}
	type = noDevice;
	// ADD CONTROLLER
	if (strncmp(typeStr, "steca", 5) == 0) type = steca_t;
	else if (strncmp(typeStr, "victron", 7) == 0) type = victron_t;
	else if (strncmp(typeStr, "turbine", 7) == 0) type = turbine_t;
	else if (strncmp(typeStr, "fronius", 7) == 0) type = fronius_t;
	else if (strncmp(typeStr, "davis", 5) == 0) type = davis_t;
	else if (strncmp(typeStr, "pulse", 5) == 0) type = pulse_t;
	else if (strncmp(typeStr, "rico", 4) == 0) type = rico_t;
	else if (strncmp(typeStr, "generic", 7) == 0) type = generic_t;
	else if (strncmp(typeStr, "owl", 3) == 0) type = owl_t;
	else if (strncmp(typeStr, "soladin", 7) == 0) type = soladin_t;
	else if (strncmp(typeStr, "sevenseg", 8) == 0) type = sevenseg_t;
	else if (strncmp(typeStr, "meter", 5) == 0) type = meter_t;
	else if (strncmp(typeStr, "resol", 5) == 0) type = resol_t;
	else if (strncmp(typeStr, "sma", 3) == 0) type = sma_t;
	else if (strncmp(typeStr, "inverter", 8) == 0) type = inverter_t;
	else if (strncmp(typeStr, "thermal", 7) == 0) type = thermal_t;
	else if (strncmp(typeStr, "sensor", 6) == 0) type = sensor_t;
	else fprintf(stderr, "ERROR logon name %s not recognised", typeStr);
	
	DEBUG fprintf(stderr, "Logon: Device %d.%d (%d.%d) %s on FD %d, type %d\n", device, unit, major, minor, typeStr, fd, type);
	// sockconn[device].type = type;
	ostringstream error;
	// If the config is Generic, permit the logon to be different and set the conf[] appropriately
	if (type != conf.getType(device)) {
		if (conf.getType(device) == generic_t) {
			conf[device].type = type;
			// Doesn't bounds or sanity check unit index.
			// Units cannot have different types.
			current.setDevice(device, unit, type);
		}
		else {
			error << "event WARN logon: mismatch. Logon " << deviceStr[type] << " (" << type << ") Config " 
				<< deviceStr[conf.getType(device)] << " (" << conf.getType(device) << ")";
			journal.add(new Message(error.str()));
		}
	}
	if (conf[device].units < unit + 1) {
		error << "event WARN logon: Device " << device << " (" << deviceStr[type] << ") Unit " << unit << " Only " << conf[device].units << " configured";
		journal.add(new Message(error.str()));
	}
	fd2contr[fd] = device;
	fd2unit[fd] = unit;		// Usually 0.
	if (unit == 0) contr2fd[device] = fd;
	conf[device].major = major;
	conf[device].minor = minor;
	conf[device].pid = pid;
}

void Sockconn::send2contr(const int contr, const char * msg) {
	int sockfd = getFdFromContr(contr);
	if (sockfd < 3) {
		fprintf(stderr, "ERROR - sockfd = %d when device = %d\n", sockfd, contr);
		return;
	}
	send2fd(sockfd, msg);
}
 
void send2fd(const int sockfd, const char * msg) {
// Send a message to filedescriptor
// Send the string to the socket.  May terminate the program if necessary
	short int msglen, written;
	int retries = NUMRETRIES;
	if (sockfd < 3) {
		fprintf(stderr, "ERROR - sockfd = %d which seems unlikely\n", sockfd);
		return;
	}
	msglen = strlen(msg);
	written = htons(msglen);
	if (write(sockfd, &written, 2) != 2) { // Can't even send length ??
			fprintf(stderr, "SockSend ERROR Can't write a length to socket\n");
	}
	while ((written = write(sockfd, msg, msglen)) < msglen) {
		if (written == -1) return;		// should really throw an exception or something
		// not all written at first go
		msg += written; msglen =- written;
		printf("Only wrote %d; %d left \n", written, msglen);
		if (--retries == 0) {
						fprintf(stderr, "SockSend WARN Timed out writing to socket\n"); 
				return;
		}
		usleep(RETRYDELAY);
	}
}

void Sockconn::clearFD(const int fd) {
	sockconn[fd].clear();
}

/************/
/* SHOWCONN */
/************/
void Sockconn::showConn(ostringstream & msg) {
	int i;
	// msg << "Connection\tFD\tDevice \n";
	msg << "\rRun?  Device  Type  Cxn/PID      Model              Path            Port         Options    Energy Power/Capacity\n";
	//        Yes   0  victron:1  6 12356        30A     /root/victron     /dev/ttyAM0                
	//        No    1    steca:1  7 12357          -       /root/steca     /dev/ttyAM1                
	//        No    2 inverter:1  8 12358       wind         /root/sim                     -n inverter  Ethermal Yes
	//        Yes   3  thermal:1 10 12359          -         /root/sim                      -n thermal
	//        No    4    davis:1 --     0          -       /root/davis      /dev/tts/2 
	for (i = 0; i < conf.size(); i++) {
		// msg << i << "\t\t" << sockconn[i].fd << "\t\t" << conf[i].typeStr << endl;
		if (conf[i].start)
			msg << "Yes ";
		else
			msg << "No  ";
		msg.width(3);
		msg << i;				// Device
		msg.width(9);
		msg << conf[i].getTypeStr() << ":";		// Type
		msg << conf[i].units << " ";			// Units
		msg.width(2);
		if (getFdFromContr(i)) msg << getFdFromContr(i);
		else msg << " -";
		msg.width(6);
		msg << conf[i].pid;
		msg.width(11);
		msg << modelStr[conf[i].model];
		msg.width(18);
		msg << conf[i].path << " ";
		msg.width(15);
		msg << conf[i].serialPort << " ";
		msg.width(15);
		msg << conf[i].options;
		if (conf[i].meterType != noMeter) {
			msg.width(10);
			msg << meterStr[conf[i].meterType];
			if (meter[conf[i].meterType].derivePower) {
				msg << " Yes";
			}
		}
		if (conf[i].capacity) {
			msg.width(10);
			msg << conf[i].capacity;
		}
		msg << endl;
	}
}
