/*
 *  action.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 02/10/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * $Id: action.h,v 3.5 2013/04/08 20:20:54 martin Exp $
 */
 
#include <time.h>

enum State {initial, normal, charging, notcharging, inverteroff, off, recovery, recharge, contributing, stecaerror};

enum MainsState {unknown, mainson, mainsoff};

// void subst(char * input, int length);	// Make substitutions into a string.
void subst(string & str);
	
class Variables {
public:		// not going to use access functions; will trust people to play nice.
/* A lot of 'variables' are in Config as they need to be persistent */
	Variables();
	// These look like variables, but when updated, the config.xml is also updated.
	// So they need to be persistent across program invocations.
	time_t	sampleFrequency;		// how often to consolidate samples
	time_t	statusUpdate;			// how often to get a System Status
	time_t	dataUpload;				// how often to upload
	time_t	processLag;				// How far behind the server can be before we resend data
	time_t	purgeTo;				// May purge to this date/time
	int		inverterOff;			// SOC to turn off inverter AS LONG AS mains still on
	int		inverterOn;				// SOC to restore inverter operation
	int		emergencySOC;			// SOC to turn off inverter EVEN IF mains is off
	int		recoverySOC;			// SOC to exit recovery state
	float	minBatt;				// Voltage to turn off loads when in StecaError.
	int		highwaterincmax;		// How much to increase either of the highwatermark permitted (multiple)
	float	esolarhighwater;
	float	ewindhighwater;
// Charging
	int		chargetoSOC;				// Try not to charge above this
	int		chargeLimit;			// Local generation in watts
	int		maintain;				// Hybrid : NotCharging -> Charging
	time_t	chargeStartTime;
	time_t	chargeStopTime;
// Equalisation
	int		eqInterval;				// in days
	time_t  eqDuration;				//  maintain eqVoltage for this long (seconds)
	float   eqVolts;				// Voltage to be maintained
// G83
	time_t	G83StartTime;
	time_t	G83StopTime;
	float	G83KWh;
// Not held in config.xml
	time_t  lastUpload;				// date of last upload. Used to prevent incorrect purging
	time_t	lastProcessed;			// Date from server of last data processed
	time_t	lastEqualisation;		// Last one took place at ...
	bool	equalising;				// Within parameters for an equalisation
	time_t  equalisationStart;		// Set to when voltage first exceeds eqVolts;
	float	localGeneration;		// Sum of local generation - solar and wind. Done in Consolidation.
	bool	chargeWindow;			// within start and stop time?
	bool	chargerOn;				// track Victron state
	bool	useBattery;
	bool	victronOn;
	bool	lowBatt;				// TRUE when vBatt < minBatt
	float   vMains;
	enum	State state;			// state as determined by state machine
	enum	MainsState mainsstate;
	void showvar(ostream & msg) const;
	void showstate(ostream & msg) const;
	const char *  stateAsStr(void ) const;
	const char *  mainsStateAsStr(void ) const;
	static const 	time_t  responseCheck = 60;			// How often to check for Response file
	void updateChargeWindow(void);
	float lowestBatt;				// Lowest battery voltage seen this period
// Removal of spurios values
	float prevWload;
	float prevWsolar;
};

class Event {		// The basis of all items on the Timer Queue.  Initialise with a specific time
public:
	Event(time_t t) : timeval(t) {} 
	virtual void action() const = 0;	// Perform the event's action
	virtual void dump(ostream & msg) const {Timestamp t(timeval); msg << t.getTime();}
	time_t getTime() const {return timeval;}
	void setTime(time_t t) {timeval = t;}
private:
	time_t timeval;
};

class Consolidate : public Event {	// Perform consolidation at specified time
public:
	Consolidate(time_t t) : Event(t) {};
	virtual void action() const;				//
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " Consolidation\n";} 
};

class StatusUpdate : public Event {	// Get system status
public:
	StatusUpdate(time_t t) : Event(t) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " StatusUpdate\n";} 
};

class DataUpload : public Event { // perform upload
public:
	DataUpload(time_t t) : Event (t) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " DataUpload\n";} 
};

class Command : public Event {	// Unix command
public:
	Command(time_t t, string & str) : Event (t), cmd(str) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " Command " << cmd << "\n";} 
private:
	string cmd;
};

class except {	// Exception thrown when converting integers and time values
public:
	except(const char * str) : msg(str) {};
	string msg;
};

class SetCmd : public Event {	// Set A Variable or alter config
public:
	SetCmd(time_t t, string & str) : Event (t), cmd(str) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " SetCommand " << cmd << "\n";} 
private:
	string cmd;	// in format 'varname=value' or '0 path /new/path'
};

class Get : public Event {  // FTP Get request
public:
	Get(time_t t, char * Owner, char * Mode, char * Server, char * Getfrom, char *Getto) : 
		Event (t), owner(Owner), mode(Mode), server(Server), getfrom(Getfrom), getto(Getto) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " Get Owner " << 
		owner << " Mode " << mode << " Server " << server << " Getfrom " << getfrom <<
		" GetTo " << getto << "\n";} 
private:
	string owner, mode, server, getfrom, getto;	
};

class Put : public Event {  // FTP Put request
public:
	Put(time_t t, char * Server, char * Putfrom, char *Putto) : 
		Event (t), server(Server), putfrom(Putfrom), putto(Putto) {};
	virtual void action() const;
	void put7250() const;	// with defective busybox1.00 ftpput
	void put7500() const;	// With working ftpput
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " Put Server " 
	<< server << " Putfrom " << putfrom << " PutTo " << putto << "\n";} 
private:
	string server, putfrom, putto;	
};

class Internal : public Event { // Internal command
public:
	Internal(time_t t, string & c) : 
		Event (t), command(c) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " InternalCommand " << command << "\n";} 
private:
	string command;	
};

class GetResponse : public Event { // Process Response file
public:
	GetResponse(time_t t) : 
		Event (t) {};
	virtual void action() const;
	virtual void dump(ostream & msg) const {Event::dump(msg); msg << " GetResponse\n";} 
private:
};

class Queue {	// the actual queue itself
typedef list <Event *> queue_type;
public:
	Queue() {QUEUEDEBUG cerr << "Queue constructed\n";};
	~Queue();
	void dump(ostream & msg) const;
	void add(Event * e);
//	 void replace(DataUpload * e);
//	 void replace(GetResponse * e);
	 template < class T> void replace(T * e);
	Event * front() {return queue.front();}
	void pop_front()	;		// delete front entry
	bool notempty()      {return queue.size();}
	bool containsDataUpload();
private:
	queue_type queue;
};

template <class T> 
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
