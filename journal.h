/*
 *  journal.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 17/09/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 *	This is the repository for eveything to do with the Journal and Current data structure
 *
 */
 
#include <list> 
	using std::list;
#include <string>
	using std::string;
#include <map>
	
#include <string.h>			// for memcpy

#include "expatpp.h" 
	
string & htmlentities(string & str);

// Values less than this get ignored as invalid, and the previous value is used instead
#define INVALIDSOC 10.0

#ifndef DAVIS
#include "davis.h"
#endif

int name(const char * n, const char ** names) throw (const char *);		// Find a name in a namelist. Not part of an object

class Timestamp {
// Just a convenient way of handling timestamps. Every record has one, and they're always
// initialised to now.  Plus convenient XML representation.
public:
	Timestamp() {timeval = time(0L);}		// always initialise to current time
	Timestamp(time_t t) {timeval = t;}		// Can explicitly set a time.
	void xml(FILE * f) const;				// The XML representation
	char * getTime() const; 
	time_t getTimeval() const {return timeval;}
	void  set(time_t t) {timeval = t;}		// Introduced for reloading journal
	bool operator< (Timestamp & other) const {return timeval < other.timeval; }
	bool operator== (Timestamp & other) const {return timeval == other.timeval; }
private:
	time_t timeval;
};

class Entry {	// Abstract base for entries in Journal
public:
	Entry() : timeval() {}
	virtual void xml(FILE * f) const = 0;			// The XML representation
	virtual void dump() const { cerr << "ENTRY ";}
	char * getTime() const {return timeval.getTime();}
	time_t getTimeval() const {return timeval.getTimeval();}
	void setTime(time_t t) {timeval.set(t);}
	// an Entry is < another if its timestamp is less
	bool operator< (Entry & other) const { return timeval < other.timeval;}
	// An entry is == another if the timestamp and the object types match
	// (You can have a Data and a Rawdata at the same time but not two Data)
	bool operator== (Entry & other) const { return timeval == other.timeval && typeid(*this) == typeid(other); }
private:
	Timestamp timeval;
};

class Message : public Entry {
public:
	Message(string s) : Entry()			{msg = htmlentities(s); DEBUG cerr << "Message::Message: " << msg << endl;}
	virtual void xml(FILE * f) const;			// The XML representation
	virtual void dump() const			{cerr << "MESSAGE " << msg;}
	const char * getmsg() const { return msg.c_str();}
private:
	string msg;
};
	 
class Data : public Entry {
public:
	Data() : Entry() {period = time(0); 
		wmains = wload = wsolar = wexport = wwind = wthermal = vbatt = ibatt = soc = vdc = 0.0;
		emains = eload = esolar = eexport = ewind = ethermal = 0.0;
		irradiation = 0.0; 
		ambient = paneltemp = -273.0;
		pulsein = pulseout = 0;
		DETAILDEBUG fprintf(stderr, "Data::Data initialised ");}	// create it empty
	virtual void xml(FILE * f) const;
	virtual void dump() const;
	void vars(const bool) const;	// update the /tmp/vards.dat file
	int period;
	float wmains, wload, wsolar, wexport, wwind, wthermal, vbatt, ibatt, soc, vdc;
	float emains, eload, esolar, eexport, ewind, ethermal;
	float irradiation, paneltemp, ambient;
	int pulsein, pulseout;
};

class Status : public Entry {
public:
	Status(float Load, float Diskused, float Diskfree, float Memused, float Memfree) : load(Load), diskused(Diskused), 
		diskfree(Diskfree), memused(Memused), memfree(Memfree), Entry() {};
	virtual void xml(FILE *f) const;
private:
	float load, diskused, diskfree, memused, memfree;
};

// DAVIS VANTAGE CLASSES -- routines are in Davis.cpp 
class DavisRealtime : public Entry {
public:
	DavisRealtime(unsigned char * init) : Entry()	{DETAILDEBUG cerr << "Creating DavisRealtime" << endl; memcpy(data, init, DREALTIMELEN);}
	virtual void xml(FILE * f) const;			// The XML representation 
	virtual void dump() const			{cerr << "DAVISREALTIME";}
	void modbus() const;
private:
	unsigned char data[DREALTIMELEN];
};

class DavisHilow : public Entry {
public:
	DavisHilow(unsigned char * init) : Entry()	{DETAILDEBUG cerr << "Creating DavisHilow" << endl; memcpy(data, init, DHILOWLEN);}
	virtual void xml(FILE * f) const {};			// The XML representation - commented out in davis.cpp
	virtual void dump() const			{cerr << "DAVISHILOW";}
private:
	unsigned char data[DHILOWLEN];
};

class DavisGraph : public Entry {
public:
	DavisGraph(unsigned char * init) : Entry()	{DETAILDEBUG cerr << "Creating DavisGraph" << endl; memcpy(data, init, DGRAPHLEN);}
	virtual void xml(FILE * f) const {};			// The XML representation  - commented out in davis.cpp
	virtual void dump() const			{cerr << "DAVISGRAPH";}
private:
	unsigned char data[DGRAPHLEN];
};

class MeterEntry : public Entry {
public:
	MeterEntry(float f1, float f2, int i) : reading1(f1), reading2(f2), device(i), Entry() {DETAILDEBUG cerr << "Creating MeterEntry\n";}
	virtual void xml(FILE * f) const;
	virtual void dump() const		{ cerr << "MeterEntry " << device << " " << reading1 << reading2;}
private:
	float reading1;
	float reading2;
	int device;
};

class CurrentData {	// pure virtual - for use in Current only
public:
	CurrentData(int c, int n, const char ** namelist = NULL) : device(c), numvals(n), count(0), names(namelist) {clear();};
	float * getVals() { return vals;}
	virtual void dump() const = 0;
	const int getnum() const		{ return numvals;}
	void addval(const float val[]) 	{for (int i = 0; i < numvals; i++) vals[i] += val[i]; count++;}	// add a value 
	void clear()	 	{for (int i = 0; i < numvals; i++) vals[i] = 0.0; count = 0;}
	void average()		{if (count) for (int i = 0; i < numvals; i++) vals[i] /= count;}
	int getCount()	const {return count;}
	void parse(const char * data) throw (const char *);
	void davis(const char * data) throw (const char *);
	virtual bool hasWmains() const   {return false;}
	virtual bool hasWload()  const   {return false;}
	virtual bool hasWexport() const   {return false;}
	virtual bool hasWsolar() const   {return false;}
	virtual bool hasWwind()  const   {return false;} 
	virtual bool hasIwind()  const   {return false;} 
	virtual bool hasVbatt()  const   {return false;}
	virtual bool hasIbatt()  const   {return false;}
	virtual bool hasSOC()    const   {return false;}
	virtual bool hasPaneltemp() const {return false;}
	virtual bool hasWindspeed() const {return false;}
	virtual bool hasILoadRMS()  const {return false;}
	virtual void stateChange(float vals[])  const {DETAILDEBUG cerr << "CurrentData::stateChange() NULL\n";}
	virtual bool hasPulses() const {return false;}
	virtual bool hasVdc() const {return false;}
	virtual bool hasEsolar() const   {return false;}
	virtual bool hasEwind() const   {return false;}
	virtual void modbus() const {fprintf(stderr, "\n****ERROR MODBUS ****\n");}
		virtual bool isSensor() const     {return false;}
//	virtual void stateChange(float vals[]) const =0;
#define ERRORVAL -10000000
	virtual float getWmains()    const {return ERRORVAL;}		// Override these where appropriate, ie
	virtual float getWload()     const {return ERRORVAL;}
	virtual float getWsolar()    const {return ERRORVAL;}
	virtual float getWexport()	 const {return ERRORVAL;}
	virtual float getWwind()     const {return ERRORVAL;}	
	virtual float getIwind()     const {return ERRORVAL;}		// where hasXXXXX is true.
	virtual float getVbatt()     const {return ERRORVAL;}
	virtual float getIbatt()     const {return ERRORVAL;}
	virtual float getSOC()       const {return ERRORVAL;}
	virtual float getWindspeed() const {return ERRORVAL;}
	virtual float getILoadRMS()  const {return ERRORVAL;}
	virtual int getPulsesIn()	 const {return ERRORVAL;}
	virtual int getPulsesOut()	 const {return ERRORVAL;}
	virtual float getVdc()	 	 const {return ERRORVAL;}
	virtual float getEsolar()    const {return ERRORVAL;}
	virtual float getEwind()     const {return ERRORVAL;}
	virtual float getIrradiation () const {return ERRORVAL;}
	virtual float getAmbient ()  const {return ERRORVAL;}
	virtual float getPaneltemp () const {return ERRORVAL;}
	float & name(const char * n) throw (const char *);
	int index(const char * name) const throw (const char *);

protected:
	int device;		// which device this is - declaration order from config.xml
	int numvals;
	float vals[MAXVALS];
	int count;
	const char ** names;	// list of names
};

class Victron : public CurrentData {
public:
	Victron(int devicenum) : CurrentData(devicenum, VICTRONVALS) {}
	virtual void dump() const;
	virtual bool hasWmains() const {return true;} 	virtual float getWmains() const;
	virtual bool hasWload() const {return true;}	virtual float getWload() const;
	virtual bool hasVbatt() const {return true;}	virtual float getVbatt() const {return vals[4];}
	virtual bool hasIbatt() const {return true;}	virtual float getIbatt() const {return vals[5];}
	virtual bool hasILoadRMS()  const {return true;}	virtual float getILoadRMS() const {return vals[7];}
	virtual void stateChange(float vals[])  const;
	static void victronOff(void);
	static void victronOn(void);
	static void useBattery(void);
	static void passthrough(void);
	static void chargerOn(void);		// only works in conjunction with passthrough
	static void chargerOff(void);
	virtual void modbus() const;
	/*  Vals are
	0 - V Mains RMS
	1 - I Mains RMS
	2 - V Inv RMS
	3 - I Inv RMS
	4 - V Batt
	5 - I Batt
	6 - V Batt Ripple
	7 - I Load signed */
};

class Steca : public CurrentData {
public:
	Steca(int devicenum) : CurrentData(devicenum, STECAVALS) {}
	virtual void dump() const;
	virtual bool hasWsolar() const   {return true;}	virtual float getWsolar() const; // const {return vals[8] > 0.0 ? vals[8] * vals[2] : 0.0;}
	virtual bool hasSOC()    const   {return true;}	virtual float getSOC()  const  {return vals[0];}
	virtual void stateChange(float vals[])  const;
	virtual void modbus() const;
	/* Vals are
	0 - SOC
	1 - I Batt signed
	2 - V Batt
	3 - Temp1
	4 - Temp2
	5 - Temp3
	6 - Temp4
	7 - Control Volts
	8 - Control Amps (Panel Current) */
};

class Turbine : public CurrentData {
public:
	Turbine(int devicenum) : CurrentData(devicenum, TURBINEVALS) {}
	virtual void dump() const;
	virtual bool hasIwind()  const   {return true;} virtual float getIwind() const {return vals[0] > 0.0 ? vals[0] : 0.0;}
	virtual void modbus() const;
};

class Fronius : public CurrentData {
public:
	Fronius(int devicenum) : CurrentData(devicenum, FRONIUSVALS) {}
	virtual void dump() const;
	virtual bool hasWload() const;		// Defined in Journal.cpp. Uses conf.capabilities
	virtual float getWload() const {return vals[4] * vals[5];}
	virtual bool hasWsolar() const   {return true;} virtual float getWsolar() const {return vals[0];}
	virtual void stateChange(float vals[])  const;	// For RiCo support -- won't handle multiple Froniuses
	virtual bool hasVdc() const {return true;}		virtual float getVdc() const {return vals[8];}
	virtual bool hasEsolar() const {return true;}	virtual float getEsolar() const {return vals[1] / 1000.0;}  /* To get it into kWh */
	virtual void modbus() const;
	
	/* Vals are: 
	0 - PowerNow  watts
	1 - EnergyTotal	watthours
	2 - EnergyDay	watthours
	3 - EnergyYear	watthours
	4 - AC Current	amps
	5 - AC Voltage	volts
	6 - Frequency	Hertz
	7 - DC Current	Amps
	8 - DC Voltage	Volts */
};

extern const char *sensorname[];
class Davis : public CurrentData {
public:
	Davis(int devicenum) : CurrentData(devicenum, SENSORVALS, sensorname) {}
	// Don't use DAVISVALS(=29) here as we don't want to allocate storage.
	// 2.8 Davis now contributes irradiation and ambient
	// 2.8 instead, use SENSORVALS
	virtual void dump() const;
	virtual void modbus() const;
	virtual bool isSensor()        const {return true;}
	virtual float getIrradiation() const {return vals[index("irradiation")];}
	virtual float getAmbient ()    const {return vals[index("ambient")];}
	virtual float getPaneltemp()   const {return vals[index("paneltemp")];}
};

class Pulse : public CurrentData {
public:
	Pulse(int devicenum) : CurrentData(devicenum, PULSEVALS) {}
	virtual void dump() const;
	virtual bool hasPulses() const {return true;}
	virtual int getPulsesIn()	 const {return int(vals[0]);}
	virtual int getPulsesOut()	 const {return int(vals[1]);}
	virtual void modbus() const;
};

//class Elster : public CurrentData {
//ublic:
//	Elster(int devicenum) : CurrentData(devicenum, ELSTERVALS) {}
//	virtual void dump() const;
//};

class Rico : public CurrentData {
public:
	Rico(int devicenum) : CurrentData(devicenum, RICOVALS) {}
	virtual void dump() const;
	virtual void modbus() const;
};

class Generic : public CurrentData {
public:
	Generic(int devicenum) : CurrentData(devicenum, GENERICVALS) {}
	virtual void dump() const;
	virtual void modbus() const;
};

class Owl : public CurrentData {
public:
	Owl(int devicenum) : CurrentData(devicenum, OWLVALS) {}
	virtual void dump() const;
	virtual bool hasWload() const {return true;}
	virtual float getWload() const {return vals[0] + vals[1] + vals[2];}
	virtual void modbus() const;
};

class Soladin : public CurrentData {
public:
	Soladin(int devicenum) : CurrentData(devicenum, SOLADINVALS) {}
	virtual void dump() const;
	virtual bool hasWload() const;		// Defined in Journal.cpp. Uses conf.capabilities
	virtual float getWload() const {return vals[0] * vals[1];}
	// The wind and solar functions depend on model="wind|solar"
	virtual bool hasWwind() const; virtual float getWwind() const {return vals[4];}
	virtual bool hasWsolar() const; virtual float getWsolar() const {return vals[4];}
	virtual void stateChange(float vals[])  const;	// For RiCo support
	virtual bool hasVdc() const {return true;}		virtual float getVdc() const {return vals[0];}
	virtual bool hasEwind() const ;	virtual float getEwind() const {return vals[5] / 1000.0;}
	virtual bool hasEsolar() const ;	virtual float getEsolar() const {return vals[5] / 1000.0;}
	virtual void modbus() const;
	/*  0: VDC
		1: IDC
		2: Mains Freq
		3: ACV
		4: Watts
		5: Watthours
		6: Temp
		7: Running hours
	*/
};

class SevenSeg : public CurrentData {		// Doesn't store data ,just calls Steca::StateChange and Victron::StateChange
public:
	SevenSeg(int devicenum) : CurrentData(devicenum, SEVENSEGVALS) {}
	virtual void dump() const;
	virtual void modbus() const;
};

class Meter : public CurrentData {
public:
	Meter(int devicenum) : CurrentData(devicenum, METERVALS) {}
	virtual void dump() const;
	virtual void modbus() const {};
	static void staticmodbus(int, float v[]);	// This is the one to use.
};

extern const char * resolname[];
class Resol : public CurrentData {
public:
	Resol(int devicenum) : CurrentData(devicenum, RESOLVALS, resolname) {}
	// NOTE Although data val 1 is energy, it also transmits it in a meter message which is the correct way to deal with it.
	virtual void dump() const;
	virtual void modbus() const;
	// Val 1 - Total Energy (kwh)
	// Val 2 - Sensor 1 temp
	// Val 3 - Sensor 2 temp
	// Val 4 - Sensor 3 temp
	// Val 5 - Sensor 4 temp
	// Val 6 - relays as integer bitmask
};

class Sma : public CurrentData {
public:
	Sma(int devicenum) : CurrentData(devicenum, SMAVALS) {}
	virtual void dump() const;
	virtual bool hasWload () const;		// Uses conf.capabilites: InverterNoLoad 
	virtual float getWload() const {return vals[0];}
	// This depends on model="solar|wind" in <device> tag
	virtual bool hasWsolar() const;	virtual float getWsolar() const { return vals[0];}
	virtual bool hasWwind() const;  virtual float getWwind()  const { return vals[0];}
	virtual void stateChange(float vals[])  const;	// For RiCo support -- won't handle multiple Froniuses
	virtual bool hasEsolar() const;	virtual float getEsolar() const { return vals[1];}
	virtual bool hasEwind() const;  virtual float getEwind()  const { return vals[1];}
	virtual void modbus() const;
	// Val 0 Watts Pac = Iac x Vac (not the DC values!)
	// Val 1 kwh Etotal
	// Val 2 Iac
	// Val 3 Vac
	// Val 4 Hz
	// Val 5 Idc
	// Val 6 Vdc
	// Val 7 Spare 1
	// Val 8 Spare 2
};

extern const char *invertername[];
class Inverter : public CurrentData {	// Use instead of Fronius or SMA
public: 
	Inverter(int devicenum) : CurrentData(devicenum, INVERTERVALS, invertername) {}
	virtual void dump() const;
	virtual void modbus() const;
	virtual bool hasWload () const;		// Uses conf.capabilites: InverterNoLoad 
	virtual float getWload() const {return vals[index("watts")];}
	// This depends on model="solar|wind" in <device> tag
	virtual bool hasWsolar() const;	virtual float getWsolar() const { return vals[index("watts")];}
	virtual bool hasWwind() const;  virtual float getWwind()  const { return vals[index("watts")];}
	virtual void stateChange(float vals[])  const;	// For RiCo support -- won't handle multiple Froniuses
	virtual bool hasEsolar() const;	virtual float getEsolar() const { return vals[index("kwh")];}
	virtual bool hasEwind() const;  virtual float getEwind()  const { return vals[index("kwh")];}
};

extern const char *thermalname[];
class Thermal : public CurrentData {
public: 
	Thermal(int devicenum) : CurrentData(devicenum, THERMALVALS, thermalname) {}
	virtual void dump() const;
	virtual void modbus() const;
};

// extern const char *sensorname[];
// Referenced above for Davis
class Sensor : public CurrentData { 
public:
	Sensor(int devicenum) : CurrentData(devicenum, SENSORVALS, sensorname) {}
	virtual void dump() const;
	virtual void modbus() const;
	virtual bool isSensor()        const {return true;}
	virtual float getIrradiation() const { return vals[index("irradiation")];}
	virtual float getAmbient ()    const { return vals[index("ambient")];}
	virtual float getPaneltemp()   const { return vals[index("paneltemp")];}
};

class Journal : public expatpp {
// This not only holds the main journal structure but associated info like time of last upload; purge-to value and so on
typedef list<Entry *> journal_type;
	journal_type journal;
public: 
	~Journal ();
	Journal() : info(0), warn(0), error(0), unknown(0) {MEMDEBUG cerr << "Journal constructed\n";};
	void add(Entry *e); 
	void add_ordered(Entry *e);
	void xml(FILE * fp) const;		// Output all entries before var.lastUpload
	void xml(const char * name) const;
	void backup(const char * name) const;	// Output all entries 
	void restore(const char * name);		// Load entries
	void purge(const time_t t);	// clear out entries older than this.
	size_t size() const {return journal.size();}
	void sort() { journal.sort(); }
private:
	// count of messages
	int info;
	int warn;
	int error;
	int unknown;
	// XML parsing (expatpp)
	virtual void startElement(const XML_Char *name, const XML_Char **atts);
	virtual void endElement(const XML_Char* name);
	virtual void charData(const XML_Char *s, int len);
	// Local state values during parsing
	char chardata[1024];
	time_t time;
	int device, unit;
	Entry * pending_entry;
	std::map<string, double> vals;	
};

class DeviceData {	// a pretty dumb class for storage in the Journal. It only knows how to print itself
public:
	DeviceData(int c, int n) {devicenum = c; numvals = n; vals = new float[n];}
	~DeviceData() { delete [] vals;}
	void setVals(const float f[]) {for (int i=0; i< numvals; i++) vals[i] = f[i];}
	void xml(FILE * f) const;
	int getNumVals(void) const {return numvals;}
private:
	int devicenum;
	int numvals;
public:	// to allow direct access from RawData::xml()
	float * vals;
};

class RawData : public Entry{	// contains all the Devicedata for all devices
public:
	RawData() : numdevices(0), Entry() { for (int i = 0; i < MAXDEVICES; i++)
		for (int j = 0; j < MAXUNITS; j++) devices[i][j] = NULL;};
	~RawData();
	void addDevice(const int device, const int unit, const int num, const float vals[]);
	void xml(FILE * f) const;
private:
	int numdevices;
	DeviceData * devices[MAXDEVICES][MAXUNITS];
};

class Current {
// The current data being accumulated. Consists of an array of CurrentData per device
public:
	Current();		// initialise these to zero
	~Current();		// delete the vals objects
	void setDevices(Config & conf); 
	void setDevice(const int i, const int unit, const enum DeviceType type);
	void addData(const int dev, const int unit, const char *);
	void setUnits(const int dev, const char * message);
	void clear() {for (int i = 0; i < numdevices; i++)
		for (int j = 0; j < MAXUNITS; j++)
			if (currentdata[i][j]) 
				currentdata[i][j]->clear();}
	void consolidate(Journal &);		// consolidate and populate the Data record. Add data & rawdata to journal
	CurrentData * getData(int dev, int unit) const {return currentdata[dev][unit];}
	void parse(const int dev, const int unit, char * msg);
	void davis(int dev, char * msg);
	int getNumDevices() const {return numdevices;}
	void addDevice();
	void modbus(float, float) const;
private:
	float sanityCheck(const float val, const int devicenum, const float prev, const char * name);
	CurrentData * currentdata[MAXDEVICES][MAXUNITS];
	int numdevices;
	time_t last;
	float prevWsolar[MAXDEVICES][MAXUNITS];
	float prevWload[MAXDEVICES][MAXUNITS];
};
