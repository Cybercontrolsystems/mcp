/*
 *  config.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 11/09/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010,2011 Wattsure Ltd. All rights reserved.
 *  Copyright 2012 Martin Robinson. All rights reserved.
 *
 */
 
#include "expatpp.h" 

// Bitmask for the Capabilities bits
#define CAPINVERTERNOLOAD	(1)
#define CAPMODBUS (2)

extern const char *deviceStr[];		// Forward declaration

class Device {
public: 
	Device() : type(noDevice), model(modelNormal), numVals(0), parallel(1), serialPort(""), path(""),
		options(""), start(true), meterType(noMeter), pid(0), slot(0), hasdisplay(0), kwdisp(0),
		kwhdisp(0), importdisp(0), capacity(0), units(1), restart(false) {};		// RICO 
	enum DeviceType		type;
	enum ModelType			model;
	const char * getTypeStr (void) const {return deviceStr[type];}
//	char * typeStr;
	int numVals;
	int parallel;		// how many devices in parallel
	bool	start;		// default is true
	string serialPort;
	string path;
	string options;
	int major, minor;	// Version number from logon. Needed to handle VE.bus Victrons
	enum MeterType	meterType;
	int pid;
	int slot;	// for Modbus
	bool hasdisplay;
	int kwdisp, kwhdisp, importdisp;	// For recreating <display> tag on output of config.xml (RICO)
	int capacity;		// Rating of inverter
	int units;		// Number of subdevices connected
	bool restart;	// Whether to restart
};

class Config : public expatpp {
// ADD CONTROLLER
friend void processMessage(int);
friend void Steca::modbus() const;
friend void Victron::modbus() const;
friend void Turbine::modbus() const;
friend void Fronius::modbus() const;
friend void DavisRealtime::modbus() const;
friend void Pulse::modbus() const;
friend void Rico::modbus() const;
friend void Generic::modbus() const;
friend void Owl::modbus() const;
friend void Soladin::modbus() const;
friend void SevenSeg::modbus() const;
friend void Meter::staticmodbus(int, float []) const;
friend void Resol::modbus() const;
friend void Sma::modbus() const;
friend void Inverter::modbus() const;
friend void Thermal::modbus() const;
friend void Sensor::modbus() const;
public:
// NOTE if you add data here, update operator=.
	Config() : IMainsScale(1.0), numDevices(0), timezone("XXXnYYY"), theVictron(-1),  
		theSevenSeg(-1), theDavis(-1), bCharging(false), serverComms("/root/send.sh"), 
		serverUrl("ftp.wattsure.com"), siteId(-1), modbus(NULL), capabilities(0), 
		numFronius(0), numSma(0), numSoladin(0), numInverters(0), hwModel("unset"), capacity(0) {};
	bool read(const char * filename);	// from XML file
	bool write(const char * filename);	// to XML file
	void dump();						// to stdout
	// iteration interface
	const Device * begin() const {return &device[0];}
	const Device * end()   const {return &device[numDevices];}
	const size_t size()		 const {return numDevices;}
	enum DeviceType  getType(int n) const {return device[n].type;}
	const int getSiteId() {return siteId;}
	int setSiteId(int) throw (const char *);
	string & setSiteName(string & name) {return siteName = name;}
	Device & operator[](int i) {return device[i];}
	Config & operator=(const Config &c);
	int theVictron;		// Device number of Victron
	int	theSevenSeg;	// Device number of SevenSegment display if present
	int theDavis;		// Device number of Davis if present
	int numFronius;		// How many Fronius's - required for multiple Rico support - takes into account num of units.
	int numSma;			// How many SMA - required for multilple Rico support
	int numSoladin;		// As above
	int numInverters;	// Now that inverters are supported by inverter data lines.
	bool bInverterLoad() const {return !(capabilities & CAPINVERTERNOLOAD);}
	bool bModbus()       const {return (capabilities & CAPMODBUS);}
	bool bOneInverter()	 const {return (numFronius + numSma + numSoladin + numInverters == 1); }
	bool bCharging;
	enum SystemType systemType;
	void dumpModbus();	// in modbus.cpp
	void addDevice()   { if (numDevices < MAXDEVICES) numDevices++;}
private:
	Device * currDevice;
	virtual void startElement(const XML_Char *name, const XML_Char **atts);
	virtual void endElement(const XML_Char* name);
	virtual void charData(const XML_Char *s, int len);
	int siteId;
public:
	int capabilities;	// bitmask.
	float IMainsScale;	// For use with 30A victrons or multiples.
	string serverComms;
	string serverUrl;
	string siteName;
	string username;
	string password;
	string mcpVersion;
private:
	string timezone;
	int numDevices;
	Device device[MAXDEVICES];	// these are in declaration order, not com port order.
	int howMany[MAXDEVICES];			// how many of each type
	char text[TEXTSIZE];				// for body text
// public:
	unsigned short * modbus;				// Modbus array
	friend void Current::modbus(float, float)const;
	int modbusSlots;
public:			// multiple Rico handling - set with <display kw="2"> tag
	struct {
		int kwdev, kwdisp, kwhdev, kwhdisp, importdev, importdisp; } rico;
	string hwModel;
	int capacity;
};

int findString(const char * msg, const char *list[]);
