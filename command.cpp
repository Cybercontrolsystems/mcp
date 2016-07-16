/*
 *  command.cpp
 *  mcp1.0
 *
 *  Created by Martin Robinson on 22/03/2007.
 *  Copyright 2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 * 
 * Command processing routines, previously in mcp
 * 
 * $Id: command.cpp,v 3.5 2013/04/08 20:20:55 martin Exp $
 */

class Config;

#include <sys/select.h>
#include "mcp.h"
#include "command.h"
#include "meter.h"
#include "journal.h"
#include "config.h"
#include "action.h"
#include "comms.h"
#include "../Common/common.h"

#include <sys/ioctl.h>	// for ioctl
#include <sys/socket.h>

#include <sys/stat.h>
#ifdef linux
	#include <net/if.h>
	#include <wait.h>		// for WAIT_ANY linux
	#include <linux/nvram.h>	// for NVRAM_INIT
	#include <limits.h>		// for PIPE_BUF - but doesn't work
	#ifndef PIPE_BUF
	#define PIPE_BUF 2048
	#endif
	#include <stdlib.h>
#else
	#define NVRAM_INIT  0
#endif
#include <fcntl.h>		// for F_SETFD

/* GLOBALS */
extern Current current;		// Global for Current data
extern Journal journal;	
extern Config conf;
extern Queue queue;
extern Sockconn socks;
extern Variables var;
extern char * config_file;
// extern int errno;
#include <errno.h>
extern struct sockconn_s consock;
extern char * meterStr[];
// extern char * deviceStr[];
extern MeterData meter[];
extern Platform platform;

void doConsole(sockconn_s & s) {
// Read and process a command from a connected Console
	char * command = s.processRead();
	if (!command) {consock.fd = 0;  return;}
	DEBUG cerr << "DoConsole FD " << s.fd << " " << command << endl;
	ostringstream output;
	doCommand(command, output);
	if (output.str().length() >= CONSOLEBUFSIZE)
		send2fd(s.fd, "ERROR response too long");
	else
		send2fd(s.fd, output.str().c_str());
}

void doFifo(int fd) {
// Read a command from the FD and write result
	// 2.2 - this only handles one command per read.
	// Fix it to handle multiple lines of input
	int out;
	FILE * f;
	if (!fd) return;
	char command[128];
	int n;
	DEBUG cerr << "In doFifo with FD = " << fd << endl;
	if (!(f = fdopen(fd, "r"))) {
		perror("fdopen of fifo");
		return;
	}
	command[0] = 0;
	ostringstream output;
	while (fgets(command, 127, f)) {
		int numtowrite, position;
		if (strlen(command)) command[strlen(command)-1] = 0;
		DETAILDEBUG cerr << "FIFO Read " << strlen(command) << " bytes :'" << command << "' ";
		doCommand(command, output);
		DETAILDEBUG cerr << "Response to command is " << output.str();
		if ((out = open(FIFO_RESULT, O_WRONLY | O_NONBLOCK)) < 0) {
			// perror(FIFO_RESULT);	// If there is no reader process, this shows as No such device or address.
			continue;
		}
		numtowrite = output.str().length();
		position = 0;
		while(numtowrite) {
			if (output.str().length() >= PIPE_BUF)
				n = write(out, output.str().c_str() + position, PIPE_BUF);
			else
				n = write(out,  output.str().c_str() + position, numtowrite);
			DEBUG cerr << "Wrote " << n << " bytes of result to FD " << out << endl;
			position += n;
			numtowrite -= n;
		}
		if (n < 0)
			cerr << strerror(errno);
	}
	close(out);
}

int getHexInt(const char * cp1) {
	char * cp2;
	int val = strtol(cp1, &cp2, 0);
	return val;
}

int getInt(const string & str, const int prev) throw (const char *) { // helper: get an integer
	return getInt(str.c_str(), prev);
}

int getInt(const char * cp1, const int prev) throw (const char *) { // helper: get integer from char *
	char * cp2;
	int val = strtol(cp1, &cp2, 10);		// 2.8 permit 0x.. as debug level.
		// 2.9 - use of 0 introduced a bug parsing 09 as this is an invalid octal number
		// So from 9:00 to 10:00 every day ,the LastProcessed is reset to midnight.
	if (*cp1 == '-' || *cp1 == '+') val = prev + val;	// Handle relative settings, ie +/-
	if (cp1 == cp2) throw "Not a number";
	return val;
}

int getNonZeroInt(const string & str, const int prev) throw (const char *) { // helper: get non-zero int
	int val = getInt(str, prev);
	if (val == 0) throw "May not be 0";
	return val;
}

time_t getTime(const string & str) throw (const char *) { // hh:mm:ss or hh:mm
	const char * cp = str.c_str();
	try {
		if (strlen(cp) == 8)
			return ((getInt(cp) * 60) + getInt(cp+3)) * 60 + getInt(cp+6);
		else if (strlen(cp) == 5)
			return ((getInt(cp) * 60) + getInt(cp+3)) * 60;
		else throw 1;		
	} catch ( ... ) {
		throw "Error in time - must be hh:mm:ss";
	}
}

float getFloat(const string & str, const float prev) throw (const char *) { // float
	char * cp2;
	const char * cp1 = str.c_str();
	float val = strtof(cp1, &cp2);
	if (*cp1 == '-' || *cp1 == '+') val = prev + val;	// Handle relative settings, ie +/-
	if (cp1 == cp2) throw "Not a number";
	return val;
}

time_t getDateTime(const string & str) throw (const char *) { // yyyy-mm-ddThh:mm:ss+01:00
// Convert a XML date to a time_t or throw except().
	struct tm mytm;
	const char * cp1 = str.c_str();
	if (str.length() != 25) throw "Not long enough for a date";
	mytm.tm_year = getNonZeroInt(cp1) - 1900;
	mytm.tm_mon  = getNonZeroInt(cp1 + 5) - 1;	// map from 1 .. 12 -> 0 .. 11
	mytm.tm_mday = getNonZeroInt(cp1 + 8);
	mytm.tm_hour = getInt(cp1 + 11);
	mytm.tm_min  = getInt(cp1 + 14);
	mytm.tm_sec  = getInt(cp1 + 17);
	mytm.tm_gmtoff = getInt(cp1 + 20) * 3600 + getInt(cp1 + 23) * 60;
	if (cp1[19] == '-') mytm.tm_gmtoff = -mytm.tm_gmtoff;
	DETAILDEBUG cerr << " gmtoff = " << mytm.tm_gmtoff << endl;
	DETAILDEBUG fprintf(stderr, "Date '%s' Year %d Month %d Day %d Hour %d Min %d Sec %d ",
						cp1, mytm.tm_year, mytm.tm_mon, mytm.tm_mday, mytm.tm_hour, mytm.tm_min, mytm.tm_sec);
	mytm.tm_isdst = -1;	// please work it out for me
	// Interesting - the gmtoff field is overwritten and not taken into account
	time_t t = mktime(&mytm);
	if (t == -1) throw "Invalid Date";
	DETAILDEBUG cerr << " getTime returns " << t << " and gmtoff = " << mytm.tm_gmtoff << endl;
	return t;
}

char * asTime(const time_t t) {
	static char c[10];
	int h = t / 3600;
	snprintf(c, sizeof(c), "%02d:%02zd:%02zd", h, (t - h *3600) / 60, t % 60);
	return c;
}

time_t getDuration(const char * str) throw (const char *) { //XML Duration
// P[nnY][nnM][mmD][T[nnH][nnM][nnS]]
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	int v;
	bool T = false;
	char * cp1;
	while (isspace(*str)) str++;
	if (toupper(*str++) != 'P') throw "Duration must start with P";
	while (*str) {
	if (isspace(*str)) {str++; continue;}
	v = strtol(str, &cp1, 10);
	if (str == cp1 && *str != 'T') throw "No number found";
	str = cp1;
	if (toupper(*str) == 'Y') year = v;
	else if (toupper(*str) == 'M' && T == false) month = v;
	else if (toupper(*str) == 'M' && T == true)  minute = v;
	else if (toupper(*str) == 'D') day = v;
	else if (toupper(*str) == 'T') T = true;
	else if (toupper(*str) == 'H') hour = v;
	else if (toupper(*str) == 'S') second = v;
	else throw "Unknown component of Duration";
	str ++;
	}
	time_t duration = second + (minute * 60)  + (hour * 60 * 60) + (day * 60 * 60 * 24) + 
		(month * 30 * 60 * 60 * 24) + (year * 365 * 60 * 60 * 24);
	return duration;
}

char * asDuration(const time_t t) {	// to XML Duration format
// P[nnY][nnM][mmD][T[nnH][nnM][nnS]]
	static char c[24];
	char * cp = &c[0];
	struct tm * mytm;
	bool T = false;
	mytm = gmtime(&t);
	*cp++ = 'P';
	if (mytm->tm_year > 70)  {snprintf(cp, sizeof(c), "%dY", mytm->tm_year - 70); cp += strlen(cp); }
	if (mytm->tm_mon > 0) {snprintf(cp, sizeof(c), "%dM", mytm->tm_mon); cp += strlen(cp); }
	if (mytm->tm_mday > 1)    {snprintf(cp, sizeof(c), "%dD", mytm->tm_mday - 1); cp += strlen(cp); }
	if (mytm->tm_hour > 0) {
		snprintf(cp, sizeof(c), "T%dH", mytm->tm_hour); cp += strlen(cp); T = true;}
	if (mytm->tm_min > 0) {
		if (!T) *cp++ = 'T';
		snprintf(cp, sizeof(c), "%dM", mytm->tm_min); cp += strlen(cp); T = true;}
	if (mytm->tm_sec > 0) {
		if (!T) *cp++ = 'T';
		snprintf(cp, sizeof(c), "%dS", mytm->tm_sec); cp += strlen(cp);}
	return &c[0];
}

time_t readCMOS(int loc) {
// Return the four bytes of NVRAM at loc
	// V2.0 - if TS7500, return 0. This is only used now for the LastEqualisation variable.
	if (platform == undefPlatform)
		determinePlatform();
	if (platform != ts72x0)
		return 0;
	int fd, ret;
	time_t t;
	if ((fd = open("/dev/misc/nvram", O_RDONLY)) == -1) {	// failed to open device
		perror("/dev/misc/nvram");
		journal.add(new Message("event WARN ReadCMOS: Failed to open /dev/misc/nvram. nvram.o probably not loaded"));
		ret = system("/sbin/insmod nvram");
		DEBUG fprintf(stderr, "ReadCMOS attemping to insmod nvram - return %d ", ret);
		if (ret) {
			journal.add(new Message("event WARN ReadCMOS: failed to insmod nvram"));
		return 0;
		}
		// Try again 
		if ((fd = open("/dev/misc/nvram", O_RDONLY)) == -1) { // second failure after successful insmod
			journal.add(new Message("event WARN ReadCMOS: failed to open NVRAM even after successful insmod"));
			return 0;
		}
	}
	if (lseek(fd, loc, SEEK_SET) == -1) {
		journal.add(new Message("event WARN ReadCMOS: failed to seek"));
		close(fd);
		return 0;
	}

	if (read(fd, &t, 4) != 4) {			// possibly not initialised
		journal.add(new Message("event INFO ReadCMOS: Initialising /dev/misc/nvram"));
		if (ioctl(fd, NVRAM_INIT, 0) == -1) {
			journal.add(new Message("event WARN ReadCMOS: Error initialising NVRAM"));
			close(fd);
			return 0;
		}
		// try again
		if (lseek(fd, loc, SEEK_SET) == -1) {
			journal.add(new Message("event WARN ReadCMOS: failed to seek(2)"));
			close(fd);
			return 0;
		}
		
		if (read(fd, &t, 4) != 4) {	// still can't read it
			journal.add(new Message("event WARN ReadCMOS: Error reading NVRAM after successful initialisation"));
			close(fd);
			return 0;
		}
	}
	
	DEBUG cerr << "Got NVRAM at " << loc << " as " << t << " - " << ctime(&t);
	close(fd);
	return t;
}
	
void writeCMOS(int loc, time_t t) {
// Write value t at loc
// For TS7550, ignore write.
#if __GNUC__ == 3
	int fd, ret;
	if (platform == undefPlatform)
		determinePlatform();
	if (platform != ts72x0)
		return;
	if ((fd = open("/dev/misc/nvram", O_RDWR)) == -1) {	// failed to open device
		journal.add(new Message("event WARN WriteCMOS: Failed to open /dev/misc/nvram. nvram.o probably not loaded"));
		ret = system("/sbin/insmod nvram");
		DEBUG fprintf(stderr, "WriteCMOS attemping to insmod nvram - return %d ", ret);
		if (ret) {
			journal.add(new Message("event WARN WriteCMOS: failed to insmod nvram"));
			return;
		}
		// Try again 
		if ((fd = open("/dev/misc/nvram", O_RDWR)) == -1) { // second failure after successful insmod
			journal.add(new Message("event WARN WriteCMOS: failed to open NVRAM even after successful insmod"));
			return;
		}
	}
	
	if (lseek(fd, loc, SEEK_SET) == -1) {
		journal.add(new Message("event WARN WriteCMOS: failed to seek"));
		close(fd);
		return;
	}

	if (write(fd, &t, 4) != 4) {			// possibly not initialised
		cerr << "WriteCMOS: Initialising /dev/misc/nvram\n";
		if (ioctl(fd, NVRAM_INIT, 0) == -1) {
			perror("Error initialising NVRAM: ");
			close(fd);
			return;
		}
		// try again
		if (lseek(fd, loc, SEEK_SET) == -1) {
			journal.add(new Message("event WARN ReadCMOS: failed to seek"));
			close(fd);
			return;
		}

		if (write(fd, &t, 4) != 4) {	// still can't write it
			perror("Error writing NVRAM after successful initialisation");
			close(fd);
			return;
		}
	}
	
	DETAILDEBUG cerr << "Wrote NVRAM at " << loc << " as " << t << " - " << ctime(&t);
	close(fd);
#endif
}

void doCommand(const char * command, ostringstream & result) {
// Process an internal command which may have come from a Console or Response file.

if (strncasecmp(command, "help", 4) == 0 ||
	strncmp(command, "?", 1) == 0) {
	result << "victronOn, victronoff - Turn Victron on or off\n";
	result << "ChargerOn, chargerOff - Turn Charger on or off\n";
	result << "UseBattery, Passthrough - as it says\n";
	result << "show c[onnections] - show connected devices\n";
	result << "show t[imer] - Show Timer events\n";
	result << "show b[uild] - Show build and version\n";
	result << "show v[ariables] - Show all variables\n";
	result << "show s[tates] - Show system state\n";
	result << "show m[eter] - Show eter configuration and values\n";
	result << "set variable=value - set a variable (see 'show var')\n";
	result << "set [n] type [inverter|meter|thermal|...]\n";
	result << "set [n] path /path/to/program\n";
	result << "set [n] port [/path/to/serial|xuartn|hostname:port]\n";
	result << "set [n] options option-string\n";
	result << "set [n] energy [esolar|eload|ethermal|none]\n";
	result << "set [n] power [yes|no] - derive power from change in energy?\n";
	result << "set [n] capacity xxx - set power limit for inverter\n";
	result << "set [n] units [x] - set device n with x units\n";
	result << "stop [n] - Stop Device n\n";
	result << "start [n] - Start device n\n";
	result << "restart [n] - Restart device n\n";
	result << "restart inverter|fronius|meter - Restart all that match\n";
	result << "restart  - Shutdown MCP\n";
	result << "reload - re-read config.xml\n";
	result << "commit - rewrite config.xml\n";
	result << "backup - write journal to /root/journal.xml\n";
	result << "restore -  read journal from /root/journal.xml\n";
	result << "trunc[ate] - wipe out /tmp/mcp.log\n";
	result << "upload - do an upload now\n";
	result << "add - an an empty device slot\n";
	result << "log - write message to the journal\n";
	result << "test - whatever the current test command is\n";
	result << "resethighwater - reset the Solar and Wind High Water Mark\n";
	result << "enable|disable modbus\n";
	result << "! UNIX command - run it\n";
	result << "[n] command - sends command to Device n\n";
	result << "debug 1=DEBUG 2=CONFIG 4=SOCKET 8=MEM 16=QUEUE 32=STATUS 64=DETAIL 128=STATE 256=DAVIS 512=MODBUS. 0 .. 1023\n";
	result << "Time format: hh:mm:ss, Date format yyyy-mm-ddThh:mm:ss+00:00\n";
	
}
else if (strncasecmp(command, "victronon", 9) == 0) {
	Victron::victronOn();
}
else if (strncasecmp(command, "victronoff", 10) == 0) {
	Victron::victronOff();
}
else if (strncasecmp(command, "chargeron", 9) == 0) {
	Victron::chargerOn();
}
else if (strncasecmp(command, "chargeroff", 10) == 0) {
	Victron::chargerOff();
}
else if (strncasecmp(command, "usebattery", 10) == 0) {
	Victron::useBattery();
}
else if (strncasecmp(command, "passthrough", 11) == 0) {
	Victron::passthrough();
}
else if (strncasecmp(command, "show c", 6) == 0) {
	socks.showConn(result);
}
else if (strncasecmp(command, "show t", 6) == 0) {
	queue.dump(result);
}
else if (strncasecmp(command, "show v", 6) == 0) {
	var.showvar(result);
}
else if (strncasecmp(command, "show s", 6) == 0) {
	var.showstate(result);
}
else if (strncasecmp(command, "show b", 6) == 0) {
	result << MCPVERSION << ' ';
	result << getmac();
}
else if (strncasecmp(command, "show m", 6) == 0) {
	int i;
	for (i = 0; i < MAXMETER; i++) {
		// fprintf(stderr, "%d %20s ", i, meterStr[i]);
		meter[i].text(i);
	}
}
else if (strncasecmp(command, "set ", 4) == 0) {
	string s(command+4);
	queue.add(new SetCmd(time(0), s));
}
else if (strncasecmp(command, "stop ", 5) == 0) {	 // stop a device
	try {
		int contr = getInt(command + 5, 0);
		DEBUG cerr << "Stop device: Sending 'exit' to FD " << socks.getFdFromContr(contr);
		socks.send2contr(contr, "exit");
		conf[contr].start = false;		// 1.27: make state persistent
		if (conf[contr].meterType != noMeter) {
			DETAILDEBUG fprintf(stderr, " Disabling meter[%d] ", conf[contr].meterType);
			meter[conf[contr].meterType].inUse = false;		// 2.8 - disble meter readings when stopped
		}
	} catch (char *) {
		result << "No number in " << command;
	}
}
else if (strncasecmp(command, "restart ", 8) == 0) {	 // stop and restart a device
	try {
		int contr = getInt(command + 8, 0);
		DEBUG cerr << "Restart device: Sending 'exit' to FD " << socks.getFdFromContr(contr);
		socks.send2contr(contr, "exit");
		conf[contr].restart = true;
		if (conf[contr].meterType != noMeter) {
			DETAILDEBUG fprintf(stderr, " Disabling meter[%d] ", conf[contr].meterType);
			meter[conf[contr].meterType].inUse = false;		// 2.8 - disble meter readings when stopped
		}
	} catch (const char *) {
		int i;
		// Search device type an path for string
		const char * param = command + 8;
		for (i = 0; i< conf.size(); i++)
			if ((strstr(conf[i].getTypeStr(), param) || strstr(conf[i].path.c_str(), param)) && 
					conf[i].restart == false && 
					conf[i].start) {
				DEBUG cerr << "Restart device: Sending 'exit' to FD " << socks.getFdFromContr(i);
				socks.send2contr(i, "exit");
				conf[i].restart = true;
				if (conf[i].meterType != noMeter) {
					DETAILDEBUG fprintf(stderr, " Disabling meter[%d] ", conf[i].meterType);
					meter[conf[i].meterType].inUse = false;		// 2.8 - disble meter readings when stopped
				}
			}
	}
}
else if (strcasecmp(command, "restart") == 0) {	// Shutdown MCP
	throw "Shutdown requested";
}
else if (strncasecmp(command, "resethighwater", 14) == 0) {	// Reset eSolarHighWatermark
	var.esolarhighwater = 0.0;
	var.ewindhighwater = 0.0;
}
else if (strncasecmp(command, "start ", 6) == 0) {		// start a device
	try {
		int contr = getInt(command + 6, 0);
		DEBUG cerr << "Starting device " << contr << endl;
		start_device(contr);
		conf[contr].start = true;		// 1.27: make state persistent
		if (conf[contr].meterType != noMeter) {
			DETAILDEBUG fprintf(stderr, " Reenabling meter[%d] ", conf[contr].meterType);
			meter[conf[contr].meterType].inUse = true;		// 2.8 - enable meter readings when started
		}
	} catch (const char *) {
		result << "No number in " << command;
	}
} 
else if (strncasecmp(command, "debug ", 6) == 0) {		// setdebug level
	try {
		debug = getHexInt(command + 6);		// This 8does* permit se of 0x.. without the cockup caused in 2.8
		cerr << "Debug level now " << debug << endl;
		result << "Debug level now " << debug;
	} catch (const char *) {
		result << "No number in " << command;
	}
} 
else if (strncasecmp(command, "debug", 5) == 0) {		// show debug level
	result << "Debug level " << debug;
}
else if (strncasecmp(command, "reload", 6) ==0) {		// reload config.xml
	Config conf2;
	if (conf2.read(config_file)) {
		if (conf.getSiteId() != conf2.getSiteId()) {
			result << "event ERROR Attempt to change siteid from " << conf.getSiteId() << " to " << conf2.getSiteId();
			// conf2.setSiteId(conf.getSiteId());
		}			
		conf = conf2;
		current.setDevices(conf);
		result << "Config reloaded";
	}
	else result << "Failed to read " << config_file;
}
else if (strncasecmp(command, "load", 4) == 0) {		// Load an alternate config file
	char filename[30];
	char * cp;
	Config conf2;
	int n;
	if ((n = sscanf(command, "%*s %30s", filename)) == 1) 
		cp = filename;
	else
		cp = config_file;
	DETAILDEBUG fprintf(stderr, "Got %d params ", n);
	
	if (conf2.read(cp)) {
		if (conf.getSiteId() != conf2.getSiteId()) {
			result << "event ERROR Attempt to change siteid from " << conf.getSiteId() << " to " << conf2.getSiteId();
		}
		conf = conf2;
		current.setDevices(conf);
		result << "Config " << cp << " loaded";
	}
	else result << "Failed to read " << cp;
}
else if (strncasecmp(command, "backup", 6) == 0) {		// Save journmal to /root/journal.xml or specified name
	char filename[30];
	int n;
	if ((n = sscanf(command, "%*s %30s", filename)) != 1) 
		strcpy(filename, "/root/journal.xml");
	
	DETAILDEBUG fprintf(stderr, "Got %d params ", n);
	
	journal.backup(filename);
	
}
else if (strncasecmp(command, "restore", 7) == 0) {		// Read journmal from /root/journal.xml or specified name
	char filename[30];
	int n;
	if ((n = sscanf(command, "%*s %30s", filename)) != 1) 
		strcpy(filename, "/root/journal.xml");
	
	DETAILDEBUG fprintf(stderr, "Got %d params ", n);
	
	journal.restore(filename);
	journal.sort();
	DEBUG cerr << "Journal now " << journal.size() << " entries\n";
}
else if (strncasecmp(command, "commit", 6) ==0 || strncasecmp(command, "save", 4) == 0) {		// rewrite config.xml
	char filename[30];
	char *cp;
	if (sscanf(command, "%*s %30s", filename) == 1) 
		cp = filename;
	else
		cp = config_file;
	if (conf.write(cp))
		result << cp << " rewritten OK";
	else
		result << "Error writing " << cp;
}
	
else if (strncasecmp(command, "trunc", 5) ==0) {		// truncate /tmp/mcp.log
	if (freopen("/tmp/mcp.log", "w", stderr))
		result << "Ok";
	else 
		result << "Failed to reopen /tmp/mcp.log";
}
else if (strncasecmp(command, "upload", 6) ==0) {		// Do an uplaod now
	queue.replace<DataUpload>(new DataUpload(time(0)));
	result << "Ok";
}
else if (strncasecmp(command, "add", 3) == 0) {			// Add empty device slot
	current.addDevice();
}
else if (command[0] == '!') {				// invoke local command
	command++;		// skip the !
	// Suspend and restore catcher()
	signal(SIGCHLD, SIG_DFL);
	FILE * fp = popen(command, "r");
	if (!fp) {
		result << "Failed to execute command: ";
	}
	char buf[1024];
	int retval;
	// This can block
	if ((retval = fread(&buf[0], 1, 1023, fp)) == -1) {
		result << "event WARN Command '" << command << "' failed to execute, errno " << errno;
	}
	buf[retval] = '\0';
	if (retval = pclose(fp)) result << "event INFO " << command << " STATUS " << retval;
	else result << "event INFO " << command;
	result << " OUTPUT " << buf;
	signal(SIGCHLD, catcher);
}
else if (isdigit(command[0]) && strlen(command) > 2) { // send command to device
	int num;
	try { num = getInt(command, 0);}
	catch (const char *c) { result << c; }
	if (num < conf.size()) {
		if (num < 10)
			command += 2;	// Single digit - skip it and space
		else
			command += 3;	// double digits.
		// assume Device 0
		DEBUG cerr << "Writing '" << command << "' to FD " << socks.getFdFromContr(num) << endl;
		socks.send2contr(num, command);
	}
	else 
		result << "invalid or unlikely device number";
}
else if (strncasecmp(command, "log ", 4) == 0) {	// Log a message. Should start with INFO, WARN or ERROR
	journal.add(new Message(command + 4));
}
else if (strncasecmp(command, "test", 4) ==0) {		// current TEST command
	int i;
	conf.dumpModbus();
	for (i = 0; i < MAXMETER; i++) {
		fprintf(stderr, "%d %20s ", i, meterStr[i]);
		meter[i].dump(stderr);
	}
	
}
else if (strncasecmp(command, "enable modbus", 13) == 0)
	conf.capabilities |= CAPMODBUS;
else if (strncasecmp(command, "disable modbus", 13) == 0)
	conf.capabilities &= ~CAPMODBUS;
else if (strcmp(command, "\n") ==0);	// ignore blank line.
	else result << "Unknown command " << command;
}

void start_device(int num) {
	if (num < 0 || num >= MAXDEVICES) {
		cerr << "Start_device called wih value outside range 0..MAXDEVICES: " << num << endl;
		return;
	}
	char * args[15];	// set up args array for exec.
	char buf[1024];
	Device & cont = conf[num];
	int i = 0;
	DEBUG cerr << "Starting Device Path " << cont.path;
	char * t = (char *)malloc(30);	// Assumed max length of a DeviceStr
	if (t == NULL) {
		snprintf(buf, sizeof(buf), "event WARN failed to malloc in start_device");
		journal.add(new Message(buf));

		return;
	}
	strcpy(t, cont.getTypeStr());
	args[i++] = t;
	DETAILDEBUG cerr << " arg-type " << i-1 << " '" << args[i-1] << "' "; 
	if (cont.options.length() != 0)  {
		/* Processing options is awkward. Getopts expects every word to be a distinct arg,
		so if we pass in '-s 123 -o 456' getopts sees -s with value "123 -o 456".
		Hence need to break it winto words. Dealing with quoted strings doesn't help.
		*/
		char * cp = buf;
		strcpy(buf, cont.options.c_str());
		// Skip leading whitespace
		while (*cp && isspace(*cp)) cp++;
		// main loop ..
		while (*cp) {
			if (*cp == '"') {		// process quoted string
				args[i++] = ++cp;
				while(*cp && *cp != '"') cp++;
			}
			else {	// unquoted	 word
				args[i++] = cp;
				while (*cp && !isspace(*cp)) cp++;
			}
			if (!*cp) 
				break;
			*cp++ = '\0';
			DETAILDEBUG cerr << " arg-opt " << i-1 << " '" << args[i-1] << "' "; 
			// skip whitespace to next argument
			while (*cp && isspace(*cp)) cp++;
		}
	}
	DETAILDEBUG cerr << " arg-last " << i-1 << " '" << args[i-1] << "' "; 
	if (cont.serialPort.length()) {
		args[i++] = const_cast<char *>(cont.serialPort.c_str());
		DETAILDEBUG cerr << " arg " << i-1 << " '" << args[i-1] << "' "; 
	}
	char contr_num[2];	// 2 bytes - enough for 9 devices!
	sprintf(contr_num, "%d", num);
	args[i++] = contr_num;
	DETAILDEBUG cerr << " arg-num " << i-1 << " '" << args[i-1] << "'\n"; 
	args[i] = NULL;
	DEBUG {int j =0;
		for (; args[j]; j++) cerr << " '" << args[j] << "'";
		cerr << endl;
	}
	
	int pid;
	if (!(pid = fork())) {
		if (pid == -1) { perror("fork failed");
			return;
		}
		
		execv(cont.path.c_str(), &args[0]);
		// If we get here, the exec failed
		snprintf(buf, sizeof(buf), "event WARN Start Device exec of '%s' failed: %s", cont.path.c_str(), strerror(errno));
		journal.add(new Message(buf));
		exit(2);	// seen as exit code 512.
	}	
	free(t);	// malloced above. Memory leak - the copy in the child process won't be freed.
	// WARNING risk of memory loos??
}

/****************/
/* SYSTEMSTATUS */
/****************/
void systemStatus(void) {
// get the sysem status and write it as an event record.
// status <load> <memused> <memfree> <diskused> <diskfree>
	FILE * fp;
	int num;
	float load;
	int diskused, diskfree, memused, memfree;
	char buf[100], *cp;
	if ((fp = popen("uptime", "r")) == 0){
		fprintf(stderr, "SystemStatus can't exec 'uptime'");
		return;
	}
	// the uptime format is horrid to parse as it has a variable number of fields, and the format
	// differs wildly between BSD and Linux.  The only safe way to get the first load value is to
	// find the last colon and work forwards from there.
	
	// It would be nice to raise a wobbly if the uptime is very small, but this is not the place to do it.
	// BSD: '15:29  up 5 days, 18 mins, 9 users, load averages: 1.50 1.17 1.15'
	// ARM Linux: ' 14:23:24 up 1 day, 22:21, load average: 0.00, 0.00, 0.00'
	fgets(buf, 99, fp);
	if ((cp = strrchr(buf, ':')) == NULL) {
		fprintf(stderr," SystemStatus: invalid response from 'uptime': %s\n", buf);
		return;
	}
	STATUSDEBUG fprintf(stderr, "Got %s and '%s'\n", buf, cp);
	if ((num = sscanf(cp, ": %f", &load)) == 0) {
		fprintf(stderr, "SystemStatus -got 0 conversions\n");
	}
	else 
		STATUSDEBUG fprintf(stderr, "Got %d values: Load as %f\n", num, load);
	pclose(fp);
	// Changed from Total to Mem to work on TS7500 - 1.47
	if ((fp = popen("free | grep Mem", "r")) == 0){
		fprintf(stderr, "SystemStatus can't exec 'free'");
		return;
	}
	// Again, look for the last colon and use the second and third values.
/*
              total         used         free       shared      buffers
  Mem:        61076         6952        54124            0            0
 Swap:            0            0            0
Total:        61076         6952        54124
*/	
	fgets(buf, 99, fp);
	if ((cp = strrchr(buf, ':')) == NULL) {
		fprintf(stderr," SystemStatus: invalid response from 'free': %s\n", buf);
		return;
	}
	if ((num = sscanf(cp, ": %*d %d %d", &memused, &memfree)) == 0) {
		fprintf(stderr, "SystemStatus - got 0 conversions for memory in %s\n", buf);
	}
	else 
	STATUSDEBUG fprintf(stderr, "Got %d values: memused as %d memfree as %d\n", num, memused, memfree);
	pclose(fp);

	if ((fp = popen("df | grep /$", "r")) == 0){
		fprintf(stderr, "SystemStatus can't exec 'df'");
		return;
	}
	// Just get root file system.
/*
Filesystem           1k-blocks      Used Available Use% Mounted on
/dev/root               128000     10680    117320   8% /
*/	
	fgets(buf, 99, fp);
	if ((num = sscanf(buf, "%*s %*d %d %d", &diskused, &diskfree)) == 0) {
		fprintf(stderr, "SystemStatus - got 0 conversions in %s\n", buf);
	}
	else 
	STATUSDEBUG fprintf(stderr, "Got %d values: diskused as %d diskfree as %d\n", num, diskused, diskfree);
	pclose(fp);

	journal.add(new Status(load, diskused, diskfree, memused, memfree));
}

void setupFifo(void) {
// Creates the Fifos if required
// If it does exist but it's a regular file, this won't do what you want.
// 2.5 - make it world writeable so CGI scripts can write to it.
	struct stat s;
	int prev;
	prev = umask(0);
	if (stat(FIFO_COMMAND, &s) < 0) {		// Doesn't exist
		if (mknod(FIFO_COMMAND, S_IFIFO | 0666, 0) < 0)
			perror(FIFO_COMMAND);		
	}
	if (stat(FIFO_RESULT, &s) < 0) {		// Doesn't exist
		if (mknod(FIFO_RESULT, S_IFIFO | 0666, 0) < 0)
			perror(FIFO_RESULT);		
	}
	umask(prev);
}

char * getmac(void) {
	// Return MAC address of eth0
	static char mac[18]="Unknown";
	
#ifdef linux
	struct ifreq ifr;       /* 32 bytes in size */
    int s;
 	
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s==-1) {
        return mac;
    }
	strcpy(ifr.ifr_name, "eth0");
	if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
		sprintf(mac, "%02x-%02x-%02x-%02x-%02x-%02x", ifr.ifr_hwaddr.sa_data[0],
				ifr.ifr_hwaddr.sa_data[1],ifr.ifr_hwaddr.sa_data[2],ifr.ifr_hwaddr.sa_data[3],
				ifr.ifr_hwaddr.sa_data[4],ifr.ifr_hwaddr.sa_data[5]);
	}
	close(s);
#endif
    return mac;
}
