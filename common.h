/*
 *  common.h
 *  Common
 *
 *  Created by Martin Robinson on 02/05/2010.
 *  Copyright 2010 Naturalwatt. All rights reserved.
 *
 *  To include common serial I/O and socket operations.
 *
 * $Revision: 3.5 $
 */

#define PORTNO 10010
#define HOSTNAME "localhost"

// Severity levels.  FATAL terminates program
#define INFO    0
#define WARN    1
#define ERROR   2
#define FATAL   3
// Set to if(0) to disable debugging
#undef DEBUG
#define DEBUG if(debug)
#define DEBUG2 if(debug > 1)
#define DEBUG3 if(debug > 2)
#define DEBUG4 if(debug > 3)
#define DEBUGFP stderr   /* set to stderr or logfp as required */

enum Platform {undefPlatform = 0, ts72x0, ts75x0, sheeva, guru};

#define REDLED 1
#define GREENLED 2
#define CONNECTRETRY 10		/* Interval to retry if device not found (USB disconnect) */

void logmsg(int severity, char *msg);   // Log a message to server and file
char * getVersion(const char * revision);			// Convert $REVISION$ macro
void decode(char * msg);
int openSerial(const char * name, int baud, int parity, int databits, int stopbits);  // return fd
int reopenSerial(const int fd, const char * name, int baud, int parity, int databits, int stopbits);  // return fd
void closeSerial(int fd);  // restore terminal settings
void sockSend(const int fd, const char * msg);        // send a string
void openSockets(int servers, int max, char * logon, char * revision, char * extra); // Open server socket
void blinkLED(int state, int which);
void determinePlatform(void);	// Establish platform

