/*
 *  comms.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 18/09/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010, 2011 Wattsure Ltd. All rights reserved.
 *
 * $Id: comms.h,v 3.5 2013/04/08 20:20:55 martin Exp $
 */
 #include <map>
	using std::map;
#include <sstream>
	using std::ostringstream;	using std::endl;

int makeserversocket(int port); 
void send2fd(const int fd, const char * msg);		// Send a message to specific FD

const int SERVERPORT = 10010;
const int CONSOLEPORT = 10011;
const int SOCKBUFSIZE = 4200;		/* socket messages are generally short  - but not for Davis packets!  (4096 + "davis graph" + 2) */
const int NUMRETRIES = 3;		/* times to retry a socket */
const int RETRYDELAY = 100000;	/* uSecs to sleep */

struct sockconn_s {
// holds the buffer for a socket to completely assemble a packet
	int fd;		// the file descriptor or 0 if closed
	int remain;	// bytes remaining
//	enum DeviceType type;
//	int con;	// the device number or -1 if not connected
	char * cp;	// pointer into buffer
	char buf[SOCKBUFSIZE];
	void clear() {SOCKETDEBUG cerr << "Clearing FD " << fd << endl; fd = 0; remain = 0; cp = buf; /*con = -1; */}
	char * processRead();
};

class Sockconn {
public:
	Sockconn() {for (int i = 0; i < MAXSOCKETS; i++) sockconn[i].clear();
			SOCKETDEBUG fprintf(stderr,"Sockconn initalised\n");}
	void setfd(fd_set &fd);					// re-initialise the FD set
	void accept(int serverfd);
	char * processRead(int num);			// deal with a read on a socket.  When message is complete,
									// return pointer to buf; otherwise NULL;
	int getFd(int i) {return sockconn[i].fd;}
	int getContrFromFd(const int fd) {return fd2contr[fd];}
	int getFdFromContr(const int contr) {return contr2fd[contr];}
	int getUnitFromFd(const int fd) {return fd2unit[fd];}
	void logon(int fd, const char * msg);		// process a logon message - set con[fd]
	void send2contr(const int contr, const char * msg);		// Send a message to device 'con'
	void showConn(ostringstream & msg);			// Show connected devices
	bool hasConsole (void) const {return consoleFD;}
	int consoleFD;
	void clearFD(const int fd);
private:
	sockconn_s sockconn[MAXSOCKETS];
	map<int, int> fd2contr;	// map FD to device number once logged on 
	map<int, int> contr2fd;   // map device number to FD once logged on
	map<int, int> fd2unit;	// map fd to unit (subdevice)
};
