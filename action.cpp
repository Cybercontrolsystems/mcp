/*
 *  action.cpp
 *  mcp1.0
 *
 *  Created by Martin Robinson on 02/10/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * This looks after the timer queue and the actions that can be on it.
 *
 * Classes: Variables Queue et al
 */

#include <list>
	using std::list;
#include <algorithm>
	using std::for_each;
#include <functional>
	using std::mem_fun;
#include <signal.h>		// for SIGCHLD linux
#include <sys/stat.h>

#include "mcp.h"
#include "meter.h"
#include "journal.h"
#include "config.h"
#include "action.h"
#include "response.h"
#include "command.h"
#include "../Common/common.h"

extern void systemStatus(void);
extern Current current;
extern Config conf;
extern Journal journal;
extern Queue queue;
extern Variables var;
// extern int errno;
#include <errno.h>
extern char * meterStr[];
extern const char * deviceStr[];
extern char * systemStr[];
extern const char * modelStr[];
extern MeterData meter[];
extern int deviceVals[];
extern Platform platform;

// LOCALS 
bool str2bool(char * text);
enum MeterType str2meter(char * text);

Variables::Variables() : prevWsolar(0.0), prevWload(0.0), esolarhighwater(0), ewindhighwater(0) {
// As a constructor this is called so early that no logging or debugging can take place.
	state = initial;
	mainsstate = unknown;
	victronOn = true;
	lowestBatt = minBatt = 45.0;
	processLag = 1800;
	highwaterincmax = 5;		// The factor by which the solarhighhwatermark may rise.
	lastEqualisation = readCMOS(NVRAM_LastEq);
}

Queue::~Queue() {		// need to delete all the items still on the queue
// This should never happen, since Queue is a global, so it doesn't go out of scope
// Should really use foreach. 
	for (queue_type::iterator li = queue.begin(); li != queue.end(); li++) {
		MEMDEBUG cerr << "~Queue deleting " << *li << "\n"; 
		delete *li;
	}
}

void Queue::dump(ostream & msg) const {
	for (queue_type::const_iterator li = queue.begin(); li != queue.end(); li++) (*li)->dump(msg);
}

void Queue::add (Event * e) {		// This preserves the order when the times are identical
// and ensures events are not added earlier than now.
	if (e->getTime() < time(NULL))
		e->setTime(time(NULL));
	MEMDEBUG cerr << "Queue::add " <<  e << "\n";
	DEBUG cerr << "Queue:add "; DEBUG e->dump(cerr);
	queue_type::iterator it = queue.begin();
	for (; (it != queue.end()) && (*it)->getTime() <= e->getTime(); it++);
		queue.insert(it, e);
}


// UGLY this has to be written in action.h as otherwise replace<DataUpload> in command.cpp
// does not cause this to be instantiated for DataUload.

// The use of export does not work as advertised. 

/* template <class T>
void Queue::replace(T * e) {	// Delete any existing items of same type before adding
	T * tp;
	queue_type::iterator li = queue.begin();
	while (li != queue.end()) {
		if (tp = dynamic_cast<T *>(*li)) {
			DEBUG { cerr << "Queue::replace Deleting " << tp << std::endl;
				tp->dump(cerr);}
			li = queue.erase(li);
			delete tp ;
		}
		else ++li;
	}
	add(e);
}
 */

/*
void Queue::replace(GetResponse * e) {	// Delete any existing items of same type before adding
	GetResponse * tp;
	queue_type::iterator li = queue.begin();
	while (li != queue.end()) {
		if (tp = dynamic_cast<GetResponse *>(*li)) {
			DEBUG { cerr << "Queue::replace Deleting " << tp << std::endl;
				tp->dump(cerr);}
			li = queue.erase(li);
			delete tp ;
		}
		else ++li;
	}
	add(e);
}
 */

bool Queue::containsDataUpload() {		// Ensure there is a DataUpload in the queue
	queue_type::const_iterator li;
	for (li = queue.begin(); li != queue.end(); li++) {
		if (dynamic_cast<DataUpload *> (*li))
			return true;
	}
	return false;
}
		   
void Queue::pop_front() {
	Event * e = queue.front(); queue.pop_front(); 
	MEMDEBUG cerr << "Queue::delete " << e << "\n";
	delete e;
}

void StatusUpdate::action() const {
	systemStatus();	// do it
	// reschedule 
	queue.add(new StatusUpdate(timeMod(var.statusUpdate, 0)));
}

void Consolidate::action() const {
	current.consolidate(journal);
	queue.add(new Consolidate(timeMod(var.sampleFrequency, 0)));
}

void DataUpload::action() const {
// Make the xml files to upload
	string name("/tmp/data.");
	time_t now = time(0);
//	struct tm * mytm = localtime(&now);
//	char buf[20];
	
	// Implement purge_to:
	journal.purge(var.purgeTo);

	char buf[20];
	if (conf.getSiteId() < 0)		// Treat negative siteid as MAC address instead
		strcpy(buf, getmac());
	else {
		snprintf(buf, sizeof(buf), "%d", conf.getSiteId());
	}
	name += buf;
	name += ".xml";
	DEBUG cerr << "DataUpload::action Got name as " << name << "\n";
	journal.xml(name.c_str());
	
	queue.add(new DataUpload(timeMod(var.dataUpload, 1)));
	var.lastUpload = now;
	
	// Call send.sh
	string command(conf.serverComms);
	command += " ";
	command += buf;
	command +=" ";
	command += conf.serverUrl;
	command += " &";	// background it
	DEBUG cerr << "Invoking comms: '" << command;
	int retval = system(command.c_str());
	DEBUG cerr << "' Retval from system() is " << retval << endl;
	// Do a replace in case there is still a GetResponse in the queue
	queue.replace<GetResponse>(new GetResponse(now + 30));
}

void Command::action() const {
// Perform a command and write the result as Message
// Upper limit of 1024 bytes for response.  If it's longer, you should write it to
// a file and retrieve the file

// Note that stderr is lost unless you append 2>&1 to the command.

// If exit status is 0, taken as INFO cmd OUTPUT data, else treated as
// WARN cmd STATUS xx OUTPUT data

// Wierd stuff.  The pclose will fail with errno = no child processes as the catcher function
// has caught and discarded the signal.  Hence need to suspend the Catcher function during 
// this one.  Hope it does not have unintended consequences.

	string str(cmd);
	subst(str);
	DEBUG fprintf(stderr, "Command::action executing command '%s'\n", str.c_str());
	FILE * fp = popen(str.c_str(), "r");
	if (!fp) {
		string msg = "event WARN Failed to execute command: ";
		journal.add(new Message(msg));
	}
	char buf[1024];
	int retval;
	signal(SIGCHLD, SIG_DFL);
	// This can block
	if ((retval = fread(&buf[0], 1, 1023, fp)) == -1) {
		ostringstream omsg;
		omsg << "event WARN Command '" << cmd << "' failed to execute, errno " << errno;
		journal.add(new Message(omsg.str()));
		return;
	}
	buf[retval] = '\0';
	if (retval == 1023) {	// there's more to come - dump it 
		while (retval = fread(&buf[0], 1, 1023, fp)) DEBUG cerr << "Ditching another " << retval << " bytes\n";
	} 
	ostringstream response;
	retval = pclose(fp);
	signal(SIGCHLD, catcher);
	response << "event INFO Command '" << cmd << "' STATUS " << retval;
	response << " OUTPUT\n" << buf;
	DETAILDEBUG if (errno) perror("pclose");
	DEBUG cerr << "Command '" << cmd << ": " << response.str() << "'\n";
	journal.add(new Message(response.str()));
}

//////////////////////
/* Internal::Action */
//////////////////////
void Internal::action() const { //  Do an internal command
	ostringstream msg;
	doCommand(command.c_str(), msg);
	DEBUG cerr << "Internal::action for '" << command << "' returned '" << msg.str() << "''n";
	if (msg.str().size())
		journal.add(new Message(msg.str()));
}

void GetResponse::action() const { // Process response file
/* This should be scheduled after an Upload and dshould be repeated every minute
  until a download.1.done file exists.
  It is not an error for there to be no download.1.xml file
  If it does exist it is renamed to download.1.xml.done, which will eventually be overwritten
  but does help with seeing what has been going on
  
  Also see if there is an ack.xml file. If so, read it an adjust lastupload if required.
*/
	Response r;
	string responsefile("/tmp/download.");
	char siteno[20];
	string buf;
	if (conf.getSiteId() < 0)		// Deal with unconfigured site by using MAC as siteno.
		strcpy(siteno, getmac());
	else
		sprintf(siteno, "%d", conf.getSiteId());
	responsefile += siteno;
	string donefile(responsefile);
	responsefile += ".xml";		// download.$site.xml
	donefile += ".done";		// download.$site.done
	DEBUG cerr << "GetResponse::action File " << responsefile << " Donefile " << donefile << endl;
	if (r.stat(donefile)) {
		DEBUG cerr << "Found .done file .. trying .xml";
		if (r.stat(responsefile)) {
			if (rename(responsefile.c_str(), donefile.c_str()) == -1) {
				buf = "event WARN failed to rename " + responsefile + " to " + donefile;
				cerr << buf;
				journal.add(new Message(buf));
			}
			else {
				r.read(donefile);	// this parses and adds to queue.
				DEBUG cerr << "Processing " << responsefile << endl;
			}
			// We do this AFTER renaming it in case it causes MCP to crash and restart on the same file
			
			DEBUG cerr << "GetResponse::action renamed " << responsefile.c_str() << " to " << donefile.c_str() << endl;
		}	
		else
			DEBUG cerr << " No .xml file. Not retrying\n";
			
		// Now look for ack.xml
		string ackfile("/tmp/ack.");
		ackfile += siteno;
		ackfile += ".xml";
		Ack ack;
		DEBUG cerr << "Looking for " << ackfile << " ";
		if (ack.stat(ackfile)) {
			DEBUG cerr << "Found " << ackfile;
			ack.read(ackfile);
			if (ack.lastProcessed) {
				if (ack.lastProcessed + var.processLag < var.lastUpload) {
					DEBUG { Timestamp t1(var.lastUpload);
						Timestamp t2(ack.lastProcessed);
						buf = "event INFO Ack: Resetting LastUpload from ";
						buf += t1.getTime();
						buf += " to ";
						buf += t2.getTime();
						journal.add(new Message(buf));
					}
					var.lastUpload = ack.lastProcessed;
					var.lastProcessed = ack.lastProcessed;
				}
				else
					DEBUG { Timestamp t1(var.lastUpload);
						Timestamp t2(ack.lastProcessed);
						DEBUG cerr << "Ack: NOT Resetting LastUpload from " << t1.getTime() << " to " << t2.getTime() << endl;
					}
				
			}
			// Finally rename it
			string ackto("/tmp/ack.");
			ackto += siteno;
			ackto += ".done";
			rename(ackfile.c_str(), ackto.c_str());
		}
		else DEBUG cerr << " not found. ";
	} else {
		DEBUG cerr << "GetResponse::action No " << donefile << " file .. trying again \n";
		// Delete any existing GetResponse on queue
		
		queue.add(new GetResponse(time(NULL) + var.responseCheck));
	} 
}

void SetCmd::action() const { // Set a variable or change the Config
	string v;
	int num, contr, rest;
	char field[40];
	const char * cp1;
	ostringstream msg;
	num = sscanf(cp1 = cmd.c_str(), "%d %39s %n", &contr, field, &rest);
	// Look for string of form number name value or number name=value
	// 1.46 A bit tricky. sscanf %s means all non-whitespace.
	if (num == 2) {	// Success - command starts with a number so it's a config file change
		char value[40];	
		char * equals;
		if (equals = strchr(field, '=')) {
			*equals = 0;		// Null terminate the field part
			strncpy(value, equals+1, 39);	// Copy the value part into value[]
		}
		else
			strncpy(value, cp1 + rest, 39);
		for (char * cp2 = value + strlen(value) - 1; *cp2 == ' '; cp2--) *cp2 = 0;	// Strip whitespace from end
		DEBUG fprintf(stderr, "SetCmd::Action '%s' = '%s' for device %d\n", field, value, contr);
		msg << "event INFO Device " << contr << " " << field << " = " << value;
		if (contr < 0 || contr > conf.size()) {
			journal.add(new Message("event WARN Invalid controller number"));
			return;
		}
		if (strcasecmp(field, "type") == 0) {
			int found = findString(value, deviceStr);
			if (found == -1) {
				string msg = "event WARN ";
				msg += value;
				msg += " is not a valid type in 'set ";
				msg += cp1;
				msg += "'";
				journal.add(new Message(msg));
			}
			else {
				DEBUG  fprintf(stderr, "Set type %d (%s) with vals %d\n", found, deviceStr[found], deviceVals[found]);
				conf[contr].type = (DeviceType) found;
				conf[contr].numVals = deviceVals[found];
				for(int j = 0; j < conf[contr].units; j++)
					current.setDevice(contr, j, (DeviceType) found);
			}
		}
		else if (strcasecmp(field, "model") == 0) {			// Set model
			int found = findString(value, modelStr);
			if (found == -1) {
				string msg = "event WARN ";
				msg += value;
				msg += " is not a valid type in 'set ";
				msg += cp1;
				msg += "'";
				journal.add(new Message(msg));
			}
			else {
				DEBUG  fprintf(stderr, "Set model %d (%s)\n", found, modelStr[found]);
				conf[contr].model = (ModelType) found;
			}
		}
		else if (strcasecmp(field, "port") == 0) conf[contr].serialPort = value;
		else if (strcasecmp(field, "path") ==0) conf[contr].path = value;
		else if (strcasecmp(field, "options")== 0) conf[contr].options = value;
		else if (strcasecmp(field, "energy") == 0) {
			// TODO This isn't clever enough to cope with additions and deletions
			// Slightly improved in 2.8
			enum MeterType v = str2meter(value);
			enum MeterType prev = conf[contr].meterType;
			if (v != noMeter) {
				conf[contr].meterType = v;
				if (prev != noMeter)
					meter[prev].derivePower = meter[prev].inUse = false;		// Added 26-05-2011
				meter[v].inUse = true;
				msg << " (Actual " << meterStr[v] << ")";
			}
		}
		else if (strcasecmp(field, "power") ==0) {
			if (strcmp(value, "yes") == 0) meter[conf[contr].meterType].derivePower = true;
			else if (strcmp(value, "no") == 0) meter[conf[contr].meterType].derivePower = false;
		}
		else if (strcasecmp(field, "capacity") ==0) {
			conf[contr].capacity = getInt(value);
		}			
		else if (strcasecmp(field, "units") == 0) {			// 2.10
			try {
				int val = getNonZeroInt(value, conf[contr].units);
				int prev = conf[contr].units;
				if (val > MAXUNITS) {
					journal.add(new Message("event ERROR Value too large in set units command"));
					val = conf[contr].units = MAXUNITS;
				}		// This duplicates the code in Current::setUnits
						// Assuming new value is larger than old value .. 
				conf[contr].units = val;
				for (int i = prev; i < val; i++)
					current.setDevice(contr, i, conf.getType(contr));
			}
			catch (const char *) {
				journal.add(new Message("event ERROR Can't have 0 in set units command"));
			}
		}
		else journal.add(new Message("event WARN Unknown Field name"));
		journal.add(new Message(msg.str()));
		return;
	}
	// Otherwise process as var=name.
	v = cmd.substr(0, cmd.find('='));
	const char * varname = v.c_str();
	string value = cmd.substr(cmd.find('=') + 1, string::npos);
	// In principle, each variable can have knock-on effects or require a transmission to a device
	// so must be handled individually
	DEBUG cerr << "SetVar::action: " << varname << " := " << value << endl;
	msg << "event INFO Set " <<  varname <<  "=";
	try {	// these functions throw except()
		if (strcasecmp(varname, "sampleFrequency")==0)		msg << (var.sampleFrequency = constrain(getNonZeroInt(value, var.sampleFrequency), 10, 7200));
		else if (strcasecmp(varname, "statusUpdate")==0)	msg << (var.statusUpdate = constrain(getNonZeroInt(value, var.statusUpdate), 10, 7200));
		else if (strcasecmp(varname, "dataUpload")==0)		msg << (var.dataUpload = constrain(getNonZeroInt(value, var.dataUpload), 10, 7200));
// TODO		else if (strcasecmp(varname, "nextUpload")==0) 	var.nextUpload = getDateTime(value);
		// TODO - some subtlety about deleting/rescheduling nextUpload
		// Need to handle both cases where nextupload < already scheduled upload and >
		else if (strcasecmp(varname, "lastUpload")==0)		msg << (var.lastUpload = getDateTime(value));	// should not be settable
		else if (strcasecmp(varname, "purgeTo")==0)			msg << (var.purgeTo = getInt(value, var.purgeTo / 3600) * 3600);
		else if (strcasecmp(varname, "inverterOff")==0) 	msg << (var.inverterOff = constrain(getNonZeroInt(value, var.inverterOff), 40, 95));
		else if (strcasecmp(varname, "inverterOn")==0)   	msg << (var.inverterOn = constrain(getNonZeroInt(value, var.inverterOn), 40, 99));
		else if (strcasecmp(varname, "emergencySOC")==0)	msg << (var.emergencySOC = constrain(getNonZeroInt(value, var.emergencySOC), 35, 90));
		else if (strcasecmp(varname, "recoverySOC")==0)		msg << (var.recoverySOC = constrain(getNonZeroInt(value, var.recoverySOC), 40, 90));
		else if (strcasecmp(varname, "optimalSOC")==0)		msg << (var.chargetoSOC = constrain(getNonZeroInt(value, var.chargetoSOC), 35, 95));		// Compatability
		else if (strcasecmp(varname, "chargetoSOC")==0)		msg << (var.chargetoSOC = constrain(getNonZeroInt(value, var.chargetoSOC), 35, 99));
		else if (strcasecmp(varname, "minBatt")==0) 		msg << (var.lowestBatt = var.minBatt = constrain(getFloat(value, var.minBatt), 42, 48));
		else if (strcasecmp(varname, "chargeLimit")==0)		msg << (var.chargeLimit = getInt(value, var.chargeLimit));
		else if (strcasecmp(varname, "chargestarttime")==0) { msg << value; var.chargeStartTime = ::getTime(value);}
		else if (strcasecmp(varname, "chargestoptime")==0)  { msg << value ; var.chargeStopTime = ::getTime(value);}
		else if (strcasecmp(varname, "eqInterval")==0)		msg << (var.eqInterval = getNonZeroInt(value, var.eqInterval));
		else if (strcasecmp(varname, "eqDuration")==0)		msg << (var.eqDuration = ::getTime(value));
		else if (strcasecmp(varname, "eqVolts")==0)			msg << (var.eqVolts  = getFloat(value, var.eqVolts));
		else if (strcasecmp(varname, "lastEq")==0)			{  writeCMOS(NVRAM_LastEq, var.lastEqualisation = getDateTime(value));
															msg << (var.lastEqualisation); }
		else if (strcasecmp(varname, "g83starttime")==0)	{msg <<  value; var.G83StartTime = ::getTime(value);}
		else if (strcasecmp(varname, "g83stoptime")==0)		{ msg << value; var.G83StopTime = ::getTime(value);}
		else if (strcasecmp(varname, "g83kwh")==0)			msg << (var.G83KWh = getFloat(value, var.G83KWh));
		else if (strcasecmp(varname, "processlag") ==0)     msg << (var.processLag = getNonZeroInt(value, var.processLag));
		else if (strcasecmp(varname, "lastprocessed") ==0)  msg << (var.lastProcessed = getDateTime(value));
		else if (strcasecmp(varname, "highwaterincmax")==0) msg << (var.highwaterincmax = getNonZeroInt(value, var.highwaterincmax));
		else if (strcasecmp(varname, "maintain")==0)        msg << (var.maintain = constrain(getInt(value, var.maintain), 0, var.chargetoSOC));
		else if (strcasecmp(varname, "siteid") == 0)		msg << conf.setSiteId(getInt(value, -1)); 
		else if (strcasecmp(varname, "sitename") ==0)		msg << conf.setSiteName(value);
		else if (strcasecmp(varname, "capabilities")==0)	msg << (conf.capabilities = getInt(value, conf.capabilities));
		else if (strcasecmp(varname, "username")==0)		msg << (conf.username = value);
		else if (strcasecmp(varname, "password")==0)		msg << (conf.password = value);
		else if (strcasecmp(varname, "model") == 0)			msg << (conf.hwModel = value);
		else if (strcasecmp(varname, "comms") == 0)			msg << (conf.serverComms = value);
		else if (strcasecmp(varname, "server") == 0)		msg << (conf.serverUrl = value);
		else if (strcasecmp(varname, "capacity") ==0)       msg << (conf.capacity = getInt(value, conf.capacity));
		else throw "Unknown variable name";
	}
	catch (const char *cp) {
		cerr << "Setvariable: " << cp << " in " << cmd;
		msg.str("event WARN Set ");
		msg << cp << " in " << cmd;
	}	
	journal.add(new Message(msg.str()));
}

void Get::action() const { // get a file
// Using ftpget.  Intermediate name is /tmp/xfer. Rename it to correct name afterwards
	ostringstream cmd1, cmd2, cmd3;
	string username, password;
	username = (conf.username.length() == 0) ? "naturalwatt" : conf.username;
	password = (conf.password.length() == 0) ? "natural1" : conf.password;
	int ret;
	string str(getfrom);
	subst(str);
	cmd1 << "ftpget -u " << username << " -p " << password << " " << server << " /tmp/xfer " << str;
	DEBUG cerr << "Get::action command is " << cmd1.str() << endl;
	ret = system(cmd1.str().c_str());
	if (ret) {
		DEBUG cerr << "Get::action ftpget failed with status " << ret << endl;
		journal.add(new Message("event WARN ftpget failed"));
	}

	// Need to see if /tmp/xfer exists, otherwise target will be removed
	struct stat s;
	if (stat("/tmp/xfer", &s) == -1) return;	// No more logging - we've already noted the failure
	
	str = getto;
	subst(str);
	cmd2 << "mv -f /tmp/xfer " << str;
	DEBUG cerr << "Get::action rename is " << cmd2.str() << endl;
	if (ret = system(cmd2.str().c_str())) {
		DEBUG cerr << "Get::action ftpget failed with status " << ret << endl;
		journal.add(new Message("event WARN mv failed"));
	}

	cmd3 << "chmod " << mode << " " << str << "; chown " << owner << " " << str;
	DEBUG cerr << "Get::action chmod/chown is " << cmd3.str() << endl;
	if (ret = system(cmd3.str().c_str())) {
		DEBUG cerr << "Get::action ftpget failed with status " << ret << endl;
		journal.add(new Message("event WARN chmod failed"));
	}
}

void Put::action() const { // Put a file
	// Need to be two version as the ftput on the TS7250 is broken.
	if (platform == undefPlatform)
		determinePlatform();
	if (platform == ts72x0)
		put7250();
	else
		put7500();
}

void Put::put7250() const{ 
/* This is much worse, as ftpput is limited.  Firstly the remotefile must actually be a path,
so the remotefile name will be the same as the localfile. Secondly the localfile cannot be qualified 
as it tries to do a PUT localfile.
Algorithm:
Separate putto int putto.dir and putto.file, also putfrom into putfrom.dir and putfrom.file
1) cd putfrom.dir
2) If putfrom.name is different than putto.name, make a copy locally
3) Put the file
4) If we made a local copy, delete it.
*/
	string username, password;
	username = (conf.username.length() == 0) ? "naturalwatt" : conf.username;
	password = (conf.password.length() == 0) ? "natural1" : conf.password;

	int slash = putto.rfind('/');
	int ret;
	if (slash == string::npos) {
		journal.add(new Message("event WARN Putto: absolute path required"));
		return;
	}
	string puttodir = putto.substr(0, slash);
	subst(puttodir);
	string puttofile = putto.substr(slash+1, string::npos);
	subst(puttofile);
	DEBUG cerr << "Put::put72x0action separated " << putto << " into " << puttodir << " and " << puttofile << endl;
	if ((slash = putfrom.rfind('/')) == string::npos) {
		journal.add(new Message("event WARN Putfrom: absolute path required"));
		return;
	}
	string putfromdir = putfrom.substr(0, slash);
	subst(putfromdir);
	string putfromfile = putfrom.substr(slash + 1, string::npos);
	subst(putfromfile);
	DEBUG cerr << "Put::action separated " << putfrom << " into " << putfromdir << " and " << putfromfile << endl;
	
	ostringstream cmd;
	cmd << "cd " << putfromdir;
	bool tempfile = false;
	if (putfromfile != puttofile) {	// need to rename first
		tempfile = true;
		cmd << "; cp "<< putfromfile << " " << puttofile;
	}
	cmd <<  "; ftpput -u " << username << " -p " << password << " " << server << " " << puttodir << " " << puttofile;
	if (tempfile) {	// delete what we created
		cmd <<  "; rm " << puttofile;
	}
	DEBUG cerr << "Complete command: " << cmd.str() << endl;
	if (ret = system(cmd.str().c_str())) {
		DEBUG cerr << "Put::72x0 rm failed with status " << ret << endl;
		journal.add(new Message("event WARN Putfrom delete of temporary failed"));
		return;
	}
}

void Put::put7500() const{ 
	/* The busybox 1.2 and later works properly in that ftpput $options $host /path/to/remotefile /path/to/localfile
	 works.
	 */
	string username, password;
	username = (conf.username.length() == 0) ? "naturalwatt" : conf.username;
	password = (conf.password.length() == 0) ? "natural1" : conf.password;
	
	int slash = putto.rfind('/');
	int ret;
	if (slash == string::npos) {
		journal.add(new Message("event WARN Put7500: absolute path required"));
		return;
	}
	string puttofile(putto);
	string putfromfile(putfrom);
	subst(puttofile);
	subst(putfromfile);
	DEBUG cerr << "Put::put7500 From " << putfromfile << " To " << puttofile;
	subst(putfromfile);
	
	ostringstream cmd;
	cmd <<  "ftpput -u " << username << " -p " << password << " " << server << " " << puttofile << " " << putfromfile;
	DEBUG cerr << "Complete command: " << cmd.str() << endl;
	if (ret = system(cmd.str().c_str())) {
		DEBUG cerr << "Put::put7500 failed with status " << ret << endl;
		journal.add(new Message("event WARN Put7500 command failed"));
		return;
	}
}
void Variables::showvar(ostream & msg) const {
// Nicely ? format the variables
	msg.precision(4);
	msg << "Site " << conf.getSiteId() << " " << conf.siteName << "\n";
	msg << "Sample Frequency        (SampleFrequency) " << sampleFrequency << endl;
	msg << "Status Update           (StatusUpdate)    " << statusUpdate << endl;
	msg << "Data Upload Frequency   (DataUpload)      " << dataUpload << endl; 
	msg << "Last Upload             (LastUpload)      " << ctime(&lastUpload);
	msg << "Last Processed          (LastProcessed    " << ctime(&lastProcessed);
	msg << "Process Lag             (ProcessLag)      " << processLag << " sec\n";
	msg << "High Water Inc. Limit   (HighWaterIncMax) " << highwaterincmax << endl;
	msg << "Data Purge To           (PurgeTo)         " << purgeTo / 3600 << " hours\n";
	msg << "Capabilities            (Capabilities)    " << conf.capabilities << "\n";
	msg << "Model                   (Model)           " << conf.hwModel << "\n";
	msg << "Capacity (total watts)  (Capacity)        " << conf.capacity << "\n";
	msg << "Comms Script            (Comms)           " << conf.serverComms << "\n";
	msg << "Comms Server            (Server)          " << conf.serverUrl << "\n";
	if (conf.username.length() > 0)
		msg << "Username                (Username)        " << conf.username << "\n";
	if (conf.password.length() > 0)
		msg << "Password                (Password)        " << conf.password << "\n";

	if (conf.bCharging) {
	msg << "Inverter Off SOC        (InverterOff)     " << inverterOff << endl;
	msg << "Inverter On SOC         (InverterOn)      " << inverterOn << endl;
	msg << "Emergency SOC           (EmergencySOC)    " << emergencySOC << endl;
	msg << "Recovery SOC            (RecoverySOC)     " << recoverySOC << endl;
	msg << "Charge to SOC           (ChargetoSOC)     " << chargetoSOC << endl;
	msg << "Maintain Above SOC      (Maintain)        " << maintain << endl;
	msg << "Minimum Battery Voltage (MinBatt)         " << minBatt << endl;
	msg << "Local Generation Limit  (ChargeLimit)     " << chargeLimit << endl;
	msg << "Equalisation Interval   (EqInterval)      " << eqInterval / 86400 << " days\n";
	msg << "Equalisation Duration   (EqDuration)      " << asTime(eqDuration) << endl;
	msg << "Equalisation Voltage    (EqVolts)         " << eqVolts << endl;
	msg << "Last Equalisation at    (LastEq)          " << ctime(&lastEqualisation);
	msg << "Charge Start Time       (ChargeStartTime) " << asTime(chargeStartTime) << endl;
	msg << "Charge Stop Time        (ChargeStopTime)  " << asTime(chargeStopTime) << endl;
	msg << "G83 Start Time          (G83StartTime)    " << asTime(G83StartTime) << endl;
	msg << "G83 Stop Time           (G83StopTime)     " << asTime(G83StopTime) << endl;
	msg << "G83 KWH                 (G83KWh)          " << G83KWh << endl;
	}
}

void Variables::showstate(ostream & msg) const {
	msg << "\rLocal Generation       " << localGeneration << endl;
	msg << "Within charge window?  " << (chargeWindow ? 'Y' : 'N') << endl;
	msg << "Equalisation voltage?  " << (equalising ? 'Y' : 'N') << endl;
	msg << "Victron On?            " << (victronOn ? 'Y' : 'N') << endl;
	msg << "Use Battery?           " << (useBattery ? 'Y' : 'N') << endl;
	msg << "Charger On?            " << (chargerOn ? 'Y' : 'N') << endl;
	msg << "System Mode            " << stateAsStr() << endl;
	msg << "System Type            " << systemStr[conf.systemType] << endl;
}

const char * Variables::stateAsStr(void) const {
	switch (state) {
	case initial:	return "Initial";
	case normal: return "Normal";
	case charging:	return "Charging";
	case notcharging: return "Not Charging";
	case inverteroff:	return "InverterOff";
	case recovery:	return "Recovery";
	case off:	return "Off";
	case contributing:	return "Contributing";
	case recharge: return "Recharge";
	case stecaerror: return "StecaError";
	default :   return "*** Unknown ***";
	}
}

const char * Variables::mainsStateAsStr(void) const {
	switch (mainsstate) {
	case unknown: return "Initial";
	case mainson: return "Mains ON";
	case mainsoff: return "Mains OFF";
	default : return "*** Unknown ***";
	}
}

void Variables::updateChargeWindow(void) { //update chargeWindow boolean
// Call this every time any event takes place
	time_t my_time = time(0);
	struct tm * my_tm = localtime(&my_time);
	time_t now = my_tm->tm_sec + my_tm->tm_min * 60 + my_tm->tm_hour * 3600;
	bool newChargeWindow;
	
	DETAILDEBUG cerr << "Updatechargewindow now=" << now;
	
	// You need a truth table to fully get this. It all depends on whether the period
	// straddles midnight.
	// Special cases: start = 00:00 stop = 00:00 - never true
	// start = 00:00 stop = 24:00 - always true
	if (chargeStartTime <= chargeStopTime) 
		newChargeWindow = (now >= chargeStartTime && now < chargeStopTime);
	else
		newChargeWindow = (now >= chargeStartTime || now < chargeStopTime);
		
	DETAILDEBUG cerr << " Oldchargewindow = " << (chargeWindow ? 'T' : 'F') << " NewChargeWindow " << (newChargeWindow ? 'T' : 'F') << endl;
		
	DEBUG if (chargeWindow != newChargeWindow) cerr << "Variables::updateChargeWindow changing from "
		<< (chargeWindow ? 'T' : 'F') << " to " << (newChargeWindow ? 'T' : 'F') << endl;
	
	chargeWindow = newChargeWindow;
}

bool str2bool(char * text) { // return true or false for "yes", "no", 0 ,1 and so on.
	if (strcasecmp(text, "yes")==0) return true;
	if (strcasecmp(text, "no") == 0) return false;
	if (strcmp(text, "1") == 0) return true;
	return false;
}

enum MeterType str2meter(char * text) {	// return index of meter type
	int i;
	for (i = 0; meterStr[i] && (strcasecmp(meterStr[i], text) != 0); i++);
	if (meterStr[i]) return (MeterType) i;
	return noMeter;
}

char * find(char * varname) {
// Return the value of a variable if it can be found
static char buf[20];
buf[0] = '\0';
struct tm  *my_tmp;
time_t t;
time(&t);
my_tmp = localtime(&t);

if (strcasecmp(varname, "site") == 0) {
	if (conf.getSiteId() < 0) 
		strcpy(buf, getmac());
	else
		snprintf(buf, sizeof(buf), "%d", conf.getSiteId());
}
else if (strcasecmp(varname, "time") == 0)
	strftime(buf, 20, "%T", my_tmp);
else if (strcasecmp(varname, "date") == 0)
	strftime(buf, 20, "%F", my_tmp);
else if (strcasecmp(varname, "timestamp") == 0)
	strftime(buf, 20, "%F-%T", my_tmp);
else
	strcpy(buf,"UNKNOWN");
DETAILDEBUG fprintf(stderr,"Find: '%s'", buf);
return buf;
}
	
void subst(char * input, int length) {
// Read from input array f, return TRUE unless an error occured.
// Length is the max size of the input array; return FALSE if this would be exceeded
// and return unaltered input; or FALSE if a variable cannot be found.
// The input array gets overwritten.
	char *cp, *output, *outputcp, *inputcp;
	char varname[10];
	int i;
	// Initial quick check to see if it contains a $
	if (strchr(input, '$') == 0) 
		return;
	if ((output = (char *)malloc(length)) == 0) return;
	// Can't malloc - no great biggie.
	outputcp = output;
	inputcp = input;
	do {
		if (*inputcp == '$') {
			// found start of var
			inputcp++;
			if (*inputcp == '$')	{// escaped $
				*outputcp++ = *inputcp;
				continue;
			}
			else if (*inputcp == '{')	{// start of ${name}
				DETAILDEBUG fprintf(stderr, "++${name}++");
				inputcp++;
				for (i = 0; *inputcp != '}'; i++) varname[i] = *inputcp++;
			}
			else {		// start of $name
				DETAILDEBUG fprintf(stderr, "++$name++");
				for (i = 0; isalnum(*inputcp); i++) varname[i] = *inputcp++;
				inputcp--;
			}
			varname[i] = '\0';
			*outputcp = '\0';
			if (cp = find(varname)) {
				strcat(outputcp, cp);
			}
			else 
				strcat(outputcp, varname);
		// If name not found, output it as a hint as to what wasn't found
			outputcp = strchr(outputcp, '\0');
			DETAILDEBUG fprintf(stderr, "Found variable '%s' in input\n", varname);
		} 
		else
		{
			*outputcp++ = *inputcp;
		}
		
	} while (*inputcp++);
	strcpy(input, output);
	free(output);
	return;	// all ok
}

void subst(string & instr) {
	string outstr;
	string::iterator it;
	char varname[10];	// longest var is 'timestamp'
	int i;
	char * cp;
	if (instr.find_first_of('$') == string::npos) return;	// Shortcut if no variable is present
	for(it = instr.begin(); it < instr.end(); it++) {
		if (*it == '$') {	// found a $
			if (*++it == '$')  { // escaped $
				outstr+= '$';
				continue;
			}
			if (*it == '{') {	 // ${name}
				for (i = 0; *it != '}'; i++) varname[i] = *++it;
				i--;
			}
			else	{	// $name
				for (i = 0; isalnum(*it); i++) varname[i] = *it++;
				it--;
			}
			varname[i] = '\0';
			if(cp = find(varname)) {
				outstr += cp;
			}
		}
		else 
			outstr += *it;
	}
	instr = outstr;
}
