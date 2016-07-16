/* The Wattsure Master Control Program in C++
*/

/*
 *  mcp.cpp
 *  mcp1.0
 *
 *  Created by Martin Robinson on 22/08/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * $Id: mcp.cpp,v 3.5 2013/04/08 20:20:55 martin Exp $
 */
#include <getopt.h>

#include <netinet/in.h>	// for sockaddr_in
#include <sys/socket.h>  // for accept() LINUX
#include <errno.h>	// for ECHILD
#include <signal.h>		// for SIGCHLD linux
#include <fcntl.h>		// for F_SETFD
#include <stdlib.h>

#ifdef linux
	#include <wait.h>		// for WAIT_ANY linux
#endif
#ifdef __MACH__
#include <stub.h>
#endif

#include "mcp.h"	/* globals */
#include "expatpp.h"
#include "meter.h"
#include "journal.h"
#include "config.h"
#include "comms.h"
#include "action.h"
#include "response.h"
#include "command.h"
#include "modbus.h"
#include "common.h"

#define FIFO_COMMAND "/var/lock/command"
#define FIFO_RESULT "/var/lock/result"
#define LEDDAT "/tmp/led.dat"

// For common.c ...
int numretries, noserver, controllernum, sockfd, retrydelay;
FILE * logfp;
#define PROGNAME "MCP"
const char * progname = PROGNAME;
extern enum Platform platform;

static const char *id=MCPVERSION; // defined in mcp.h

/* GLOBALS */
Current current;		// Global for Current data
Data prev;				// Global for previous Data structure - used to fill in zero blips.
Journal journal;	
Config conf;
Queue queue;
Variables var;
Sockconn socks;
MeterData meter[MAXMETER];
struct sockconn_s consock;
int insecure;

int debug;

// LOCALS 
void usage();
void dumpfdset(fd_set &f) ;
void sigurg(int sig);
void dumphex(const char *msg, const int count);
void handle_davis(const char *msg);	// Handle Davis Vantage messages  // in davis.cpp
// void handle_meter(const int contr, const char * msg);		// in meter.cpp
void processLEDmessage(char * msg);
void decode(const char * msg);
void write2tmp(const int num, const int unit, const char * message);		// Write a /tmp/deviceXX.txt file for all incoming messages

#ifdef linux
const char * config_file = "/root/config.xml";
#else
const char * config_file = "../../config.xml";
#endif

int main (int argc, char * const argv[]) {
	const char * startupMsg = "Undefined Startup";
	char * response_file = 0;
	int consoleserverfd;
	int fifofd = 0;
	bool usestderr = false;
	
	consock.clear();
	
	signal(SIGCHLD, catcher);	// catch SIGCHILD for termination
	signal(SIGPIPE, catcher);	// rare but you can get SIGPIPE if you close console too fast

// Command line args
	opterr = 0;
	debug = 0;
	int opts; 
	insecure = 0;
	while ((opts = getopt(argc, argv, "id:f:r:VZe")) != -1) {
		switch (opts) {
		case 'f': config_file = optarg; break;
		case 'r': response_file = optarg; break;
		case 'd': debug = atoi(optarg); break;
		case '?': usage(); exit(1);
		case 'e': usestderr = true; break;
		case 'i': insecure = 1; break;
		case 'V': cout << id << ' ' << getmac() << endl; exit(0);
		case 'Z': decode("(b+#Gjv~z`mcx-@ndd`rxbwcl9Vox=,/\x10\x17\x0e\x11\x14\x15\x11\x0b\x1a" 
						"\x19\x1a\x13\x0cx@NEEZ\\F\\ER\\\x19YTLDWQ'a-1d()#!/#(-9' >q\"!;=?51-??r"); exit(0);
		}
	}
	
	if (optind < argc) startupMsg = argv[optind];
	
	// Set working directory to /root
#ifdef linux
	chdir("/root");
#endif

	// Validate a response file
	if (response_file) {
		Response r;
		if (r.read(response_file)) 
			cerr << "XML ok ...\n";
		else
			cerr << "XML ERROR!\n";
		queue.dump(cout);
		return 0;
	}

// Setup socket
	int serverfd = makeserversocket(SERVERPORT);	// this may exit.  If you can't even create a socket
		// there's no point continuing
	fcntl(serverfd, F_SETFD, 1);		// set to close on exec so childrend od'nt have it open.
	consoleserverfd = makeserversocket(CONSOLEPORT);
	fcntl(consoleserverfd, F_SETFD, 1);

#ifdef linux
	if (!usestderr) freopen("/tmp/mcp.log", "a", stderr);		// set stderr to this file.
#endif
	
	determinePlatform();
	string startup("event INFO Starting MCP Version " REVISION);		// Very first message; before TZ is set!
	if (platform == ts72x0) {
		time_t lastactive = readCMOS(NVRAM_LastActivity);
		startup +=  " previously active GMT ";
		startup += ctime(&lastactive);
	}
	journal.add(new Message(startup));

	conf.read(config_file);
//	conf.write("../../config.xml.new");
//	conf.dump();
//	Config conf2; 
//	conf2.read("../../config.2");
//	conf = conf2;
//	conf.dump();
//	conf.write("../../config.2.new");
	
//  Initialise Queue
	if (var.sampleFrequency == 0) var.sampleFrequency=600;	// Prevent fatal error condition in case dataUpload is null
	if (var.statusUpdate == 0) var.statusUpdate=600;	// Prevent fatal error condition in case dataUpload is null
	if (var.dataUpload == 0) var.dataUpload=600;	// Prevent fatal error condition in case dataUpload is null
	queue.add(new Consolidate(timeMod(var.sampleFrequency, 0)));
	queue.add(new StatusUpdate(timeMod(var.statusUpdate, 0)));
	queue.add(new DataUpload(time(NULL) + 90));		// First upload in 90 seconds to avoid hanging around.
	string uptime("uptime");
	queue.add(new Command(time(NULL), uptime));
//	queue.dump(cerr);
	
//	return 0;

// close standard input
	freopen("/dev/null", "r", stdin);
	
// Check FIFO_COMMAND and FIFO_RESULT exist.  Creates them as pipes if not.
	setupFifo();
	
// Startup file processing.  If there is /root/startup.xml, treat it like a 
// response file.  Since no devices have logged in yet, make sure you use after=

	Response r;
	if (r.stat(STARTUPXML)) {
		DEBUG cerr << "Processing startup file " << STARTUPXML << endl;
		if (r.read(STARTUPXML)) {
			DEBUG cerr << "Startup file ok\n";
		}
		else
			journal.add(new Message("event ERROR Error in startup file"));
	}
	
// Setup Current data
	current.setDevices(conf);
	
// Start devices 

#ifdef linux
	for (int i = 0; i < conf.size(); i++) {
		if (conf[i].start) {
			start_device(i);
			sleep(2);
		}
	}
#endif
		
		
// Start Modbus
	if (conf.bModbus())
		initModbus();
		
// Setup CurrentData
// main loop
	bool done = false;
	fd_set readfd;
	fd_set exceptfd;
	try {
	while (!done) {
		FD_ZERO(&readfd);
		struct timeval timeout;
		timeout.tv_sec = 60;
		timeout.tv_usec = 0;
		// Is there an event due before the default timeout?
		if (queue.notempty() && queue.front()->getTime() < time(0) + timeout.tv_sec) {
			DETAILDEBUG fprintf(stderr, "Next event due is ");
			DETAILDEBUG queue.front()->dump(cerr);
			timeout.tv_sec = queue.front()->getTime() - time(0);
			if (timeout.tv_sec < 0) timeout.tv_sec = 0;
		}
		
		// If there is not a DataUpload in the queue, that's serious ...
		if (!queue.containsDataUpload()) {
			journal.add(new Message("ERROR No DataUpload in queue! Adding one .."));
			queue.add(new DataUpload(time(NULL) + 60));
		}			
		
		DETAILDEBUG fprintf(stderr, "Timeout adjust for %zu seconds from now\n", timeout.tv_sec);
		FD_SET(serverfd, &readfd);
		FD_SET(consoleserverfd, &readfd);
		
		// Open FIFO if possible
		DETAILDEBUG fprintf(stderr, "Fifofd = %d ", fifofd);
		if (!fifofd) {
			if ((fifofd = open(FIFO_COMMAND, O_RDONLY | O_NONBLOCK, 0)) == -1) {		// Need to do a non-blocking open
				perror(FIFO_COMMAND);
				fifofd = 0;
			}
			DETAILDEBUG fprintf(stderr, "Opened FIFO as fd %d\n", fifofd);
		}
		if (fifofd > 0) FD_SET(fifofd, &readfd);
		
/*		// TEMP
			if ((flags = fcntl(fifofd, F_GETFL, 0)) < 0) perror("Reading flags");
	flags &= ~O_NONBLOCK;
	if (fcntl(fifofd, F_SETFL, flags) < 0) perror("Writing flags");
	DETAILDEBUG fprintf(stderr,"Flags = %x O_NONBLOCK = %x\n", flags, O_NONBLOCK);
*/
		// END TEMP
		if (consock.fd) {
			FD_SET(consock.fd, &readfd);		// console FD
		}
		socks.setfd(readfd);		// set all open sockets into readfd
		// SOCKETDEBUG FD_SET(STDIN_FILENO, &readfd);
		if (conf.bModbus()) 
			setModbusFD(&readfd);
		SOCKETDEBUG {cerr << "Before: "; dumpfdset(readfd); cerr << endl;}
		exceptfd = readfd;
		// turn LED on when going into sleep ...
		blinkLED(1, GREENLED);
		int retval = select(consoleserverfd + MAXDEVICES + 3 + MODBUSSOCKETS, &readfd, 0, &exceptfd, &timeout);
		// .. and off while processing ...
		blinkLED(0, GREENLED);
		usleep(10000);	// Sleep for 10mSec to allow LED to actually blink.
		
		DEBUG {Timestamp t;
			cerr << t.getTime() << ' ';
		}
		// Save LastActivity time in NVRAM  V1.42
		if (platform == ts72x0)
			writeCMOS(NVRAM_LastActivity, time(0));	
		
		if (retval == 0) {
			DEBUG fprintf(stderr, "timeout\n");
				// process non IO related events.
				// Anything on queue ?
			Event * ep;
			while (queue.notempty() && (ep = queue.front()) && ep->getTime() <= time(0)) {
				DEBUG ep->dump(cerr);
				ep->action();
				DEBUG cerr << " .. done it\n";
				queue.pop_front();		// done that, now delete it
			}
		//	continue;
		}
		if (retval < 0) {
			fprintf(stderr, "Select returned %d ", retval);
			perror("");
			sleep(5);
			continue;
		}
		SOCKETDEBUG {cerr << "After: ";	
			dumpfdset(readfd); cerr << endl;
		}
		
		SOCKETDEBUG { cerr << "Exceptfd: "; dumpfdset(exceptfd); cerr << endl; }
// Update ChargeWindow boolean
		var.updateChargeWindow();
		
		if (FD_ISSET(serverfd, &readfd)) {		// read on main server socket - process accept()
			SOCKETDEBUG cerr << "Processing accept from serverfd ( " << serverfd << ")\n";
			socks.accept(serverfd);	
		};
		char * result;
		int i, fd;
		for (i = 0; i < MAXDEVICES; i++) {
			// DEBUG fprintf(stderr, "Device %d fd %d ", i, socks.getFdFromCon(i));
			if ((fd = socks.getFd(i)) && FD_ISSET(fd, &readfd)) {
				int cont, unit;
				cont = socks.getContrFromFd(fd);
				unit = socks.getUnitFromFd(fd);
				if (result = socks.processRead(i)) {
					DEBUG fprintf(stderr,"Cn%d FD%d Cont%d U:%d: '%s'\n", i, fd, 
								  cont, unit, result); 
				}
				write2tmp(cont, unit, result);		// Write a /tmp/deviceXX.txt file ...
				if (result == 0) continue;		// not got the entire message yet
				if (strncmp(result, "logon ", 6) == 0) {
					socks.logon(fd, result);
				}
				if (strncmp(result, "data ", 5) == 0) {	
					current.addData(cont, unit, result);
				} else if (strncmp(result, "meter ", 6) == 0) 
					handle_meter(cont, result);		// Create an ElsterVal object
				else if (strncmp(result, "davis ", 6) == 0) {
					handle_davis(result);	// Just puts the DavisRealtime object on the queue.
					current.davis(cont, result);
				}
				else if (strncmp(result,    "inverter ", 9) == 0 
						 || strncmp(result, "thermal ", 8) == 0
						 || strncmp(result, "sensor ", 7) == 0) 
					current.parse(cont, unit, result);		// process "inverter" or "thermal" row
				else if (strncmp(result,     "units ", 6) ==0)
					current.setUnits(cont, result);
				// Anything except data message, log it as is. Including logon messages
				else  if (strncmp(result, "version", 7) == 0) {
					string ver("mcp ");
					ver += getVersion(REVISION);
					send2fd(fd, ver.c_str());
					DEBUG fprintf(stderr, "Version response '%s' ", ver.c_str());
				} else
					journal.add(new Message(result));
				if (strncmp(result, "event INFO Victron LED Status is ", 33) == 0)
					processLEDmessage(result);		// Slight hack to write /tmp/leds.dat file.
			}
		}
		// Console connection
		if (consoleserverfd && FD_ISSET(consoleserverfd, &readfd)) {	// create console connection
			int fd;
			struct sockaddr_in clientAddr;
			int clientLen = sizeof(clientAddr);
			fd = accept(consoleserverfd,  (struct sockaddr *) &clientAddr, (socklen_t *)&clientLen);
			fcntl(fd, F_SETFD, 1);
			if (consock.fd) {	// problem - console already connected
				send2fd(fd, "ERROR Console already connected");
				int retval = close(fd);
				if (retval == -1) perror("Closing duplicate console");
			}
			else { 
				consock.fd = fd;
				DEBUG fprintf(stderr, "Console accepted on %d\n", consock.fd);
			}
		}
		// console handling
		if (consock.fd && FD_ISSET(consock.fd, &readfd)) {
			DEBUG cerr << "Console ... ";
				doConsole(consock);
		}
		// Modbus handling
		if (conf.bModbus())
			handle_modbus(&readfd);
			
		// FIFO handling
		if (fifofd && FD_ISSET(fifofd, &readfd)) {
			DEBUG cerr << "FIFO ... ";
			doFifo(fifofd);
			close(fifofd);
			fifofd = 0;
		}
		DETAILDEBUG sleep(1);	// to prevent fast loops when it runs away
	}
	} catch (char const * msg) {
	//Exception!  Exit program
		DEBUG cerr << "EXCEPTION " << msg << endl;
	}
	if (consock.fd) close(consock.fd);
}


void usage() {
	cerr << "Usage: mcp [-f config.xml] [ii] [-d n] [-r responsefile] \"Startup Message\"\n";
	cerr << "Flags: -e use stderr not /tmp/mcp.log\n";
	cerr << "	-V for Version -i Insecure ports\n";
}

/***********/
/* CATCHER */
/***********/
void catcher(int sig) {
// Signal catcher
// Possibly, more than one process could have died.  Unlikely but possible
// In fact seems to be rather common - due to statusupdate.
// Improved thanks to APUE to discriminate between different types of exit 
// or indeed cause of SIGCHLD
Timestamp t;
int pid, status;
#ifndef WIFCONTINUED
#define WIFCONTINUED(x) (x == SIGCONT)
#endif
	errno = 0;
	while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0) {
		ostringstream response, process;
		// Find entry in conf[] if any for this PID
		int i;
		for (i = 0; i < conf.size(); i++) 
			if (conf[i].pid == pid) break;
		if (i < conf.size()) 
			process << " " << conf[i].getTypeStr() << " " << i;
		else
			process << " (unknown)";
		if(WIFEXITED(status)) {
			if(WEXITSTATUS(status))
				response << "event WARN Process " << pid << process.str() << " exited with status " << WEXITSTATUS(status);
			else
				response << "event INFO Process " << pid << process.str() << " exited with status " << WEXITSTATUS(status);
		}
		else if (WIFSIGNALED(status))
			response << "event ERROR Process " << pid << process.str() << " exited on signal " << WTERMSIG(status) << " (" << strsignal(WTERMSIG(status)) << ")";
		else if (WIFSTOPPED(status))
			response << "event WARN Process " << pid << process.str() << " stopped on signal " << WSTOPSIG(status) << " (" << strsignal(WTERMSIG(status)) << ")";
		else if (WIFCONTINUED(status))	
			response << "event INFO Process " << pid << process.str() << " continued";
		else
			response << "event ERROR Process " << pid << process.str() << " received unexpected signal " << sig << " (" << strsignal(WTERMSIG(status)) << ") Exit status " << status;
		journal.add(new Message(response.str()));
		if (i < conf.size()) {
			conf[i].pid = 0;
		}
		// If restart requested, queue a restart
		if ( i < conf.size() && conf[i].restart) {
			conf[i].restart = false;
			ostringstream start;
			start << "start " << i;
			string s ;
			s = start.str();
			queue.add(new Internal(time(NULL) + 10 + i, s)); 
			DEBUG fprintf(stderr,"Queueing '%s' in %d seconds .. ", start.str().c_str(), 10+i);
		}
	}
	if (errno == ECHILD || errno == 0) return;
	perror("Catcher::Waiting ");
}

// Local utility function
void dumpfdset(fd_set &f) {
	for (int i = 0; i < 24; i++) {
		cerr << (FD_ISSET(i, &f) ? '1' : '0');
		if (i % 8 == 7) cerr << ' ';
	}
	cerr << ' ';
	cerr.flush();
}

/***********/
/* TIMEMOD */
/***********/
time_t timeMod(time_t interval, int offset) {
	// Return a time in the future at modulus t;
	// ie, if t = 3600 (1 hour) the time returned
	// will be the next time on the hour.
	//	time_t now = time(NULL);
	char buffer[20];
	if (interval == 0) interval = 600;
	time_t t = time(NULL);
	time_t newt =  (t / interval) * interval + interval + offset;
	DEBUG {
		struct tm * tm;
		tm = localtime(&t);
		strftime(buffer, sizeof(buffer), "%F %T", tm);
		fprintf(stderr,"TimeMod now = %s delta = %zu ", buffer, interval);
		tm = localtime(&newt);
		strftime(buffer, sizeof(buffer), "%F %T", tm);
		fprintf(stderr, "result %s\n", buffer);
	} 
	return newt;
}

void processLEDmessage(char * msg) {
	// Handle 'event INFO Victron LED Status is XXXX'
	// Needs to differentiate Victron versions.  Prior to 2.0, the meaning is different
	DEBUG fprintf(stderr,"LED Status ");
	int val, num;
	FILE * f;
	const char * on = "on", *off = "off";
	num = sscanf(msg, "event INFO Victron LED Status is %x", &val);
	if (num != 1) {
		DEBUG fprintf(stderr, "ERROR failed to read a value from '%s'\n", msg);
		return;
	}
	DEBUG fprintf(stderr, " Got value as %x\n", val);
	if (!(f = fopen(LEDDAT, "w"))) {
		fprintf(stderr, "Failed to open " LEDDAT " for writing\n");
		return;
	}
	if (conf.theVictron == -1) {
		DEBUG fprintf(stderr, "Can't process a LED message - there is no Victron!");
		return;
	}
	if (conf[conf.theVictron].major > 1) {	// Use VE.bus LED bit meanings
		fprintf(f, "templed=%s\n", val & 0x80 ? on : off);
		fprintf(f, "lowbattled=%s\n", val & 0x40 ? on : off);
		fprintf(f, "overloadled=%s\n", val & 0x20 ? on : off);
		fprintf(f, "inverterled=%s\n", val & 0x10 ? on : off);
		fprintf(f, "mainsled=%s\n", val & 1 ? on : off);
		fprintf(f, "bulkled=%s\n", val & 4 ? on : off);
		fprintf(f, "absorptionled=%s\n", val & 2 ? on : off);
		fprintf(f, "floatled=%s\n", val & 8 ? on : off);
	} else { // Use 9bit meanings
		fprintf(f, "templed=%s\n", val & 1 ? on : off);
		fprintf(f, "lowbattled=%s\n", val & 2 ? on : off);
		fprintf(f, "overloadled=%s\n", val & 4 ? on : off);
		fprintf(f, "unknownled=%s\n", val & 8 ? on : off);
		fprintf(f, "inverterled=%s\n", val & 0x10 ? on : off);
		fprintf(f, "mainsled=%s\n", val & 0x20 ? on : off);
		fprintf(f, "bulkled=%s\n", val & 0x40 ? on : off);
		fprintf(f, "absorptionled=%s\n", val & 0x80 ? on : off);
		fprintf(f, "floatled=%s\n", val & 0x100 ? on : off);
	}
	fclose(f);
};

void decode(const char * msg) {
	// Algorithm - each byte X-ored' with a successively higher integer.
	const char * cp;
	char i = 0;
	for (cp = msg; *cp; cp++) putchar(*cp ^ i++);
	putchar('\n');
}

/*************/
/* WRITE2TMP */
/*************/
// 2.5 - write a /tmp/deviceXX-YY.txt file for every device.
void write2tmp(const int num, const int unit, const char * message){
	FILE * fp;
	char filename[24], timestr[20];
	time_t now;
	struct tm *mytm;
	snprintf(filename, 24, "/tmp/device%02d-%02d.txt", num, unit);
	if (!(fp = fopen(filename, "w"))) {
		fprintf(stderr, "Can't open '%s' for writing: %s\n", filename, strerror(errno));
		return;
	}
	now = time(NULL);
	mytm = localtime(&now);
	strftime(timestr, sizeof(timestr), "%T", mytm);
	fprintf(fp, "%02d-%02d %s: %s\n", num, unit, timestr, message);
	DETAILDEBUG fprintf(stderr,"Wrote '%02d-%02d %s: %s' to '%s'\n", num, unit, timestr, message, filename);
	fclose(fp);
	
}
