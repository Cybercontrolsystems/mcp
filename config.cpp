/*
 *  config.cpp
 *  mcp1.0
 *
 *  Created by Martin Robinson on 11/09/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 * $Id: config.cpp,v 3.5 2013/04/08 20:20:55 martin Exp $
 */
 
#include <stdio.h>  // for stdio
#include <string.h>	// for strcmp
#include <stdlib.h> // for atoi
#include <sys/stat.h>	// for stat

#include "mcp.h"
#include "meter.h"
#include "journal.h"
#include "config.h"
#include "action.h"
#include "command.h"
#include "modbus.h"

const char * elementTypeStr[] = {"noelement", "config", "site", "server", "mcp", "charging", "equalisation", "g83", "controller", 
	"device", "port", "path", "options", "maxcharge", "battery", "capabilities", "meter", "processlag", "display", NULL};
enum ElementType {noelement=0, config_e, site_e, server_e, mcp_e, charging_e, equalisation_e, g83_e, controller_e,  
	device_e, port_e, path_e, options_e, maxcharge_e, battery_e, capabilities_e, meter_e, processlag_e, display_e};
	// ADD CONTROLLER
const char * deviceStr[] = {"none", "steca", "victron", "turbine", "fronius", "davis", "pulse", "rico", "generic", "owl", 
	"soladin", "sevenseg", "meter", "resol", "sma", "inverter", "thermal", "sensor", NULL};
const char * modelStr[] = {"-", "16A", "30A", "IG30", "Vantage2", "solar", "wind", "cylinder", "solasyphon", "pool", NULL};
const char * meterStr[] = {"none", "Eload", "Emains", "Esolar", "Ewind", "Ethermal", "Eexport", "GenImp", "GenExp", NULL};

const char * systemStr[] = {"Storage", "Hybrid", "Grid", NULL};

// enum portType {noport=-1, com0, com1, com2, com3};
// char * portTypeStr[] = {"none", "/dev/ttyAM0", "/dev/ttyAM1", "/dev/ttyAM2", "/dev/ttyAM3", NULL};
	// ADD CONTROLLER
int deviceVals [] =  {0,      STECAVALS, VICTRONVALS,  TURBINEVALS, FRONIUSVALS, DAVISVALS, PULSEVALS, RICOVALS, GENERICVALS, OWLVALS, SOLADINVALS, 
	SEVENSEGVALS, METERVALS, RESOLVALS, SMAVALS, INVERTERVALS, THERMALVALS, SENSORVALS, 0};

bool power;	// Nasty little gotcha. Can't set derivePower flag as we see the attribute
		// power="yes" before we know what type of meter it is.

// LOCALS
void checkVersion(const string &config, const char * mcp);
char *getversion(void);

// EXTERNALS
extern Variables var;
extern MeterData meter[];
extern Journal journal;
// extern int errno;
#include <errno.h>
extern int deviceModbus[];

////////////
// CONFIG //
////////////

bool Config::read(const char * filename) {	
	// from XML file
	DEBUG cerr << "Reading " << filename << endl;
	numDevices = 0;
	FILE * f;
	char buf[BUFSIZE];
	size_t len;
	int done = 0;
	int i;
	theVictron = theSevenSeg = -1;
	rico.kwdisp = rico.kwhdisp = rico.importdisp = 0;
	bCharging = false;
	for (i = 0; i < MAXDEVICES; i++) howMany[i] = 0;		// reset number of devices of each type
	if (!(f = fopen(filename, "r"))) {
		snprintf(buf, sizeof(buf), "event ERROR MCP Can't open config file %s: %s", filename, strerror(errno));
		journal.add(new Message(buf));
		return false;	// error 1 = problem with file
	}
	
	try {
		while (!done) {	// loop until entire file has been processed.
			len = fread(buf, 1, BUFSIZE, f);
			done = len < BUFSIZE;
			if (!XML_Parse(buf, len, done)) {
				fprintf(stderr,	"%s at line %d\n",
						XML_ErrorString(XML_GetErrorCode()),
						XML_GetCurrentLineNumber());
				fclose(f);
				return false;	// error 2 - XML problem
			}
		}
	}  catch (char const * msg) {
		snprintf(buf, sizeof(buf), "event ERROR MCP error in config.xml: %s", msg);
		journal.add(new Message(buf));
		fclose(f);
		return false;
	}
	fclose(f);
	
	// Work out numInverters.  Needs to be done here (used to be done in the StartElement) because we need to 
	// take into account the number of each device.
	
	for (i = 0; i < numDevices; i++) 
	{
		if (sma_t      == device[i].type) numSma += device[i].units;
		if (fronius_t  == device[i].type) numFronius += device[i].units;
		if (soladin_t  == device[i].type) numSoladin += device[i].units;
		if (inverter_t == device[i].type) numInverters += device[i].units;
	}
	DEBUG fprintf(stderr, "At end NumSMA=%d NumFronius=%d NumSoladin=%d NumInverters=%d\n", 
				numSma, numFronius, numSoladin, numInverters);
	
	// Set up the Modbus stuff if required
	
	if (bModbus()) {
		int count = 4 + FIXEDMODBUS + 2 * numDevices;
		for (i = 0; i < numDevices; i++) {
			device[i].slot = count;
			DEBUG fprintf(stderr, "Dev[%d] slot starts at %d ", i, count);
			count += deviceModbus[device[i].type] + 1;
		}
		DEBUG fprintf(stderr,"Modbus: allocating %d shorts\n", count);
		modbusSlots = count;
		if ((modbus = (unsigned short int*)calloc(count, 2)) == 0) {
			journal.add(new Message("event WARN MCP failed to malloc for modbus"));
			modbusSlots = 0;
			capabilities &= ~CAPMODBUS;
		}
		else
		{
			// Remember the table appears to be 1-indexed!
			modbus[0] = MODBUSLAYOUT;
			modbus[1] = FIXEDMODBUS;
			modbus[2 + FIXEDMODBUS] = numDevices;
			count = 3 + FIXEDMODBUS + 2 * numDevices;
			for(i = 0; i < numDevices; i++) {
				modbus[3 + FIXEDMODBUS + 2 * i] = device[i].type;
				// If the number of values is zero, set location to zero.
				if (deviceModbus[device[i].type]) {
					// Note the slots are zero-based but Modbus addressing is 1-based
					modbus[4 + FIXEDMODBUS + 2 * i] = count + 1;
					device[i].slot = count;
				}
				else
					modbus[4 + FIXEDMODBUS + 2 * i] = 0;
				count += deviceModbus[device[i].type] + 1;
				DEBUG fprintf(stderr,"Type[%d] = %d (%s),  Index[%d] = %d\n", 3 + FIXEDMODBUS + 2 * i, 
							  modbus[3 + FIXEDMODBUS + 2 * i], 
							  deviceStr[modbus[3 + FIXEDMODBUS + 2 * i]],	4 + FIXEDMODBUS + 2 * i, 
							  modbus[4 + FIXEDMODBUS + 2 * i]);  
			}
		}
	}
	// Not sure if it belongs here, but set the timezone.
	
	setenv("TZ", timezone.c_str(), 1);
	return true; // so far so good
}

int Config::setSiteId(int val) throw (const char *) {
	if (siteId >= 0) throw "Refusing to set site id";
	return siteId = val;
}

bool Config::write(const char * filename) {
// return true if file written ok
	struct stat sb;
	
	if (stat(filename, &sb) == 0) { // file exists; need to rename it 
		string newfile(filename);
		newfile += ".old";
		if(rename(filename, newfile.c_str())) {
			perror(filename);
			return false; // problem renaming config.xml to config.xml.old
		}
	}

	std::ofstream f(filename);
	if (!f) { // can't open file
		perror("Config::write Creating new config.xml");
		return false; // problem creating new file
	}
	f << "<?xml version=\"1.0\"?>\n<config xmlns=\"http://www.naturalwatt.com\"\n";
	f << "    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n";
	f << "    xsi:schemaLocation=\"http://www.naturalwatt.com config.xsd\">\n";
	f << "  <site id=\"" << siteId << "\" timezone=\"" << timezone << "\">" << siteName << "</site>\n\n";
	f << "  <server comms=\"" << serverComms << "\"";
	if (username.length() != 0) f << " username=\"" << username << "\"";
	if (password.length() != 0) f << " password=\"" << password << "\"";
	f << ">" << serverUrl << "</server>\n";
	f << "  <mcp version=\"" << getversion() << "\"\n";
	f << "    samplefrequency=\"" << var.sampleFrequency << "\"\n";
	f << "    statusupdate=\"" << var.statusUpdate << "\"\n";
	f << "    dataupload=\"" << var.dataUpload << "\"\n";
	f << "    purgeto=\"" << asDuration(var.purgeTo) << "\"\n";
	f << "    processlag=\"" << var.processLag << "\"\n";
	if (hwModel != "unset")
		f << "    model=\"" << hwModel << "\"\n";
	if (capacity != 0.0)
		f << "    capacity=\"" << capacity << "\"\n";
	f << "    highwaterincmax=\"" << var.highwaterincmax << "\"/>\n";
	if (bCharging) {
		f << "  <charging inverteroff=\"" << var.inverterOff << "\"\n";
		f << "    inverteron=\"" << var.inverterOn << "\"\n";
		f << "    emergencysoc=\"" << var.emergencySOC << "\"\n";
		f << "    recoverysoc=\"" << var.recoverySOC << "\"\n";
		f << "    chargetosoc=\"" << var.chargetoSOC << "\"\n";
		f << "    maintain=\"" << var.maintain << "\"\n";
		f << "    minbatt=\"" << var.minBatt << "\"\n";
		f << "    limit=\"" << var.chargeLimit << "\"\n";
		f << "    start=\"" << asTime(var.chargeStartTime) << "\"\n";
		f << "    stop=\"" << asTime(var.chargeStopTime) << "\"\n";
		f << "    system=\"" << systemStr[systemType] << "\"/>\n";
		CONFIGDEBUG fprintf(stderr, "Write: system type %d (%s) ", systemType, systemStr[systemType]);
		f << "  <equalisation voltage=\"" << var.eqVolts << "\"" << " duration=\"" << asTime(var.eqDuration);
		f <<		"\" interval=\"" << asDuration(var.eqInterval) << "\"/>\n";
		f << "  <g83 start=\"" << asTime(var.G83StartTime) << "\"";
		f <<		" stop=\"" << asTime(var.G83StopTime) << "\" kwh=\"" << var.G83KWh << "\"/>\n";
	}
	if (capabilities) {
		f << "  <capabilities>";
		if (!bInverterLoad()) f << "InverterNoLoad ";
		if (bModbus()) f << "Modbus ";
		f << "</capabilities>\n";
	}
	for (const Device * ci = begin(); ci != end(); ci++) {
		f << "  <device type=\"" << ci->getTypeStr() << "\"";
		if (ci->model != modelNormal) 		f << " model=\"" << modelStr[ci->model] << "\"";
		if (ci->parallel != 1)				f << " parallel=\"" << ci->parallel << "\"";
		if (ci->start == false)				f << " start=\"no\"";
		if (ci->units != 1)					f << " units=\"" << ci->units << "\"";
		if (ci->capacity)						f << " capacity=\"" << ci->capacity << "\"";
		f << ">\n";
		if (ci->serialPort.length())		f << "    <port>" << ci->serialPort << "</port>\n";
		if (ci->path.length())				f << "    <path>" << ci->path << "</path>\n";
		if (ci->options.length())			f << "    <options>" << ci->options << "</options>\n";
		if (ci->hasdisplay) {
			f << "    <display ";		// RICO
			if (ci->kwdisp) f << "kw=\"" << ci->kwdisp << "\" ";
			if (ci->kwhdisp) f << "kwh=\"" << ci->kwhdisp << "\" ";
			if (ci->importdisp) f << "import=\"" << ci->importdisp << "\" ";
			f << "/>\n";
		}
		if (ci->meterType != noMeter) {
			f << "    <meter";
			if (meter[ci->meterType].derivePower)			f << " power=\"yes\"";
			f << ">" << meterStr[ci->meterType] << "</meter>\n";
		}
		f << "  </device>\n";
	}
	f << "</config>\n";
	f.close();
	return true; // means ok
}

/* -------------*/
/* STARTELEMENT */
/*--------------*/

void Config::startElement(const char *name, const char **atts)
// XML_Parse function called at element start time.
{
	int i, element, found;
	
	CONFIGDEBUG fprintf(stderr, "Start: %s ", name);
	// identify the element name and store in userdata
	for (i = 0; elementTypeStr[i]; i++)
	if (strcmp(name, elementTypeStr[i]) == 0) {
		element = i;
		break;
	}
	if (element == 0) {
		fprintf(stderr, "Config: element <%s> not valid\n", name);
	}
	
	// For all start tags, initialise text buffer.
	text[0] = '\0';
	
	// process attributes
	switch(element) {
		case site_e:						// <site id="" timezone="">
			while (*atts) {
				if (strcmp(atts[0],"id")==0) {
					siteId = getInt(atts[1]);
				} else if (strcmp(atts[0],"timezone")==0) {
					timezone = atts[1];
				} else fprintf(stderr, "Site: unrecognised attribute %s\n", atts[0]);
				atts += 2;
			};
			break;
			case server_e:						// <server comms="" username="" password ="">
			while (*atts) {
			if (strcmp(atts[0],"comms")==0) {
				serverComms = atts[1];
			} 
			else if (strcmp(atts[0], "username")==0)
				username = atts[1];
			else if (strcmp(atts[0], "password")==0)
				password = atts[1];
			else fprintf(stderr, "Server: unrecognised attribute %s\n", atts[0]);
			atts += 2;
			}
			break;
			case mcp_e:						// <mcp version and others>
			while (*atts) {
				CONFIGDEBUG fprintf(stderr,"att %s=%s ", atts[0], atts[1]);
				try {
					if (strcmp(atts[0],"version")==0) {
						mcpVersion = atts[1];
						checkVersion(mcpVersion, getversion());	// Can throw exception
					}
					else if (strcmp(atts[0], "samplefrequency")==0)		// permitted to be zero to disable consolidations
						var.sampleFrequency = getInt(atts[1]);
					else if (strcmp(atts[0], "statusupdate")==0)
						var.statusUpdate = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "dataupload")==0)
						var.dataUpload = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "purgeto") == 0)
						var.purgeTo = getDuration(atts[1]);
					else if (strcmp(atts[0], "inverteroff")==0)		// COMPATABILITY
						var.inverterOff = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "inverteron")==0)		// COMPATABILITY
						var.inverterOn = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "emergencysoc")==0)		// COMPATABILITY
						var.emergencySOC = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "recoverysoc")==0)		// COMPATABILITY
						var.recoverySOC = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "optimalsoc")==0)		// COMPATABILITY
						var.chargetoSOC = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "chargelimit")==0)		// COMPATABILITY
						var.chargeLimit = getInt(atts[1]);
					else if (strcmp(atts[0], "chargestarttime")==0)		// COMPATABILITY
						var.chargeStartTime = getTime(atts[1]);
					else if (strcmp(atts[0], "chargestoptime")==0)		// COMPATABILITY
						var.chargeStopTime = getTime(atts[1]);
					else if (strcmp(atts[0], "eqinterval")==0)		// COMPATABILITY
						var.eqInterval = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "eqvoltage")==0)		// COMPATABILITY
						var.eqVolts = getInt(atts[1]);
					else if (strcmp(atts[0], "eqtime")==0)		// COMPATABILITY
						var.eqDuration = getInt(atts[1]);
					else if (strcmp(atts[0], "eqduration")==0)		// COMPATABILITY
						var.eqDuration = getInt(atts[1]);
					else if (strcmp(atts[0], "g83start")==0)		// COMPATABILITY
						var.G83StartTime = getTime(atts[1]);
					else if (strcmp(atts[0], "g83stop")==0)		// COMPATABILITY
						var.G83StopTime = getTime(atts[1]);
					else if (strcmp(atts[0], "g83kwh")==0)		// COMPATABILITY
						var.G83KWh = getInt(atts[1]);
					else if (strcmp(atts[0], "equalisationfrequency")==0)		// COMPATABILITY
						var.eqInterval = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "g83kwh")==0)		// COMPATABILITY
						var.G83KWh = getInt(atts[1]);
					else if (strcmp(atts[0], "g83kwh")==0)		// COMPATABILITY
						var.G83KWh = getInt(atts[1]);
					else if (strcmp(atts[0], "g83kwh")==0)		// COMPATABILITY
						var.G83KWh = getInt(atts[1]);
					else if (strcmp(atts[0], "nightratestarttime")==0)
						var.chargeStartTime = getTime(atts[1]);
					else if (strcmp(atts[0], "nightratestoptime")==0)
						var.chargeStopTime = getTime(atts[1]);
					else if (strcmp(atts[0], "nightratecharge")==0)
						;
					else if (strcmp(atts[0], "processlag")==0)
						var.processLag = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "solarincmax")==0)		// COMPATIBILITY pre 1.41
						var.highwaterincmax = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "highwaterincmax")==0)		
						var.highwaterincmax = getNonZeroInt(atts[1]);
					else if (strcmp(atts[0], "model") == 0) 
						hwModel = atts[1];
					else if (strcmp(atts[0], "capacity") == 0)
						capacity = getInt(atts[1]);
					else	
						fprintf(stderr, "MCP: unrecognised attribute %s\n", atts[0]);
				}
				catch (char *) {
					fprintf(stderr, "MCP: error in value '%s' for attribute '%s'\n", atts[1], atts[0]);
				}
				atts += 2;
			}
			break;
			case charging_e:						// <Charger parameters>
			bCharging = true;
			CONFIGDEBUG fprintf(stderr, "Reading Charging tag ");
			while (*atts) {
				try {
					if (strcasecmp(atts[0], "inverteroff")==0)
						var.inverterOff = getNonZeroInt(atts[1]);
					else if (strcasecmp(atts[0], "inverteron")==0)
						var.inverterOn = getNonZeroInt(atts[1]);
					else if (strcasecmp(atts[0], "emergencysoc")==0)
						var.emergencySOC = getNonZeroInt(atts[1]);
					else if (strcasecmp(atts[0], "recoverysoc")==0)
						var.recoverySOC = getNonZeroInt(atts[1]);
					else if (strcasecmp(atts[0], "optimalsoc")==0)			// Compatability
						var.chargetoSOC = getNonZeroInt(atts[1]);
					else if (strcasecmp(atts[0], "chargetosoc")==0)
						var.chargetoSOC = getNonZeroInt(atts[1]);
					else if (strcasecmp(atts[0], "minbatt")==0)
						var.lowestBatt = var.minBatt = getFloat(atts[1]);
					else if (strcasecmp(atts[0], "limit")==0)
						var.chargeLimit = getInt(atts[1]);
					else if (strcasecmp(atts[0], "start")==0)
						var.chargeStartTime = getTime(atts[1]);
					else if (strcasecmp(atts[0], "stop")==0)
						var.chargeStopTime = getTime(atts[1]);
					else if (strcasecmp(atts[0], "maintain")==0)
						var.maintain = getInt(atts[1]);
					else if (strcasecmp(atts[0], "system") ==0) {
						found = findString(atts[1], systemStr);
						if (found < 0)
							fprintf(stderr, "<Charging system=> unrecognised type '%s'", atts[1]);
						else
							systemType = (SystemType)found;
						CONFIGDEBUG fprintf(stderr, "System Type = %d (%s) ", systemType, systemStr[systemType]);
					}
					
					else	
						fprintf(stderr, "<Charging>: unrecognised attribute %s\n", atts[0]);
				}
				catch (char *) {
					fprintf(stderr, "<Charging>: error in value '%s' for attribute '%s'\n", atts[1], atts[0]);
				}
				atts += 2;
			}
			break;
			case equalisation_e:						// <equalisation parameters>
			while (*atts) {
				try {
					if (strcmp(atts[0], "interval")==0)
						var.eqInterval = getDuration(atts[1]);
					else if (strcmp(atts[0], "voltage")==0)
						var.eqVolts = getFloat(atts[1]);
					else if (strcmp(atts[0], "time")==0)		//COMPATABILITY
						var.eqDuration = getTime(atts[1]);
					else if (strcmp(atts[0], "duration")==0)
						var.eqDuration = getTime(atts[1]);
					else	
						fprintf(stderr, "<Equalisation>: unrecognised attribute %s\n", atts[0]);
				}
				catch (char *) {
					fprintf(stderr, "<Equalisation>: error in value '%s' for attribute '%s'\n", atts[1], atts[0]);
				}
				atts += 2;
			}
			break;
			case g83_e:						// <mcp version and others>
			while (*atts) {
				try {
					if (strcmp(atts[0], "start")==0)
						var.G83StartTime = getTime(atts[1]);
					else if (strcmp(atts[0], "stop")==0)
						var.G83StopTime = getTime(atts[1]);
					else if (strcmp(atts[0], "kwh")==0)
						var.G83KWh = getFloat(atts[1]);
					else	
						fprintf(stderr, "MCP: unrecognised attribute %s\n", atts[0]);
				}
				catch (char *) {
					fprintf(stderr, "MCP: error in value '%s' for attribute '%s'\n", atts[1], atts[0]);
				}
				atts += 2;
			}
			break;
			case controller_e:				// device type="" 
			case device_e:
			// remember that devices is statically allocated 
			CONFIGDEBUG fprintf(stderr, "Device: ");
			power = false;	// set to false on initial <device> tag
			currDevice = &device[numDevices];
			while (*atts) {
				CONFIGDEBUG fprintf(stderr, "Atts: %s=%s\n", atts[0], atts[1]);
				if (strcasecmp(atts[0], "type") == 0) {
					found = findString(atts[1], deviceStr);
					if (found != -1) {
						CONFIGDEBUG fprintf(stderr, "Matches %d %s\n", found, deviceStr[found]);
						currDevice->type = DeviceType(found);
						currDevice->numVals = deviceVals[found];
						howMany[found]++;
						// capabilities |= 1 << (found-1);	// MTR What's this!!!
						if (currDevice->type == victron_t) theVictron = numDevices;
						if (currDevice->type == sevenseg_t) theSevenSeg = numDevices;
						if (currDevice->type == davis_t) theDavis = numDevices;
						// break; // OOPS!!
					}
					else {
						fprintf(stderr, "Device type='%s' not recognised\n", atts[1]);
						currDevice->type = noDevice;
						currDevice->numVals = 0;
					}
				}
				else if (strcasecmp(atts[0], "model") == 0) {
					found = findString(atts[1], modelStr);
					if (found > -1) {
							currDevice->model = ModelType(found);
							CONFIGDEBUG fprintf(stderr,"Setting model=%s ", modelStr[currDevice->model]);
					}
					else 
						fprintf(stderr, "Model='%s' not recognised\n", atts[1]);
					if (currDevice->model == Victron30A)
						IMainsScale = 2.0;
				}
				else if (strcasecmp(atts[0], "parallel") == 0) {	// default is 1
					currDevice->parallel = getNonZeroInt(atts[1]);
				}
				else if (strcmp(atts[0], "start") == 0) {		// if missing, assume start="yes"
					if (strcmp(atts[1], "no") == 0)
						currDevice->start = false;
					CONFIGDEBUG fprintf(stderr," start = %d ", currDevice->start);
				}
				else if (strcmp(atts[0], "capacity") == 0) {		// power="2000", inverter rating
					currDevice->capacity = getInt(atts[1]);
					CONFIGDEBUG fprintf(stderr, "capacity = %d ", currDevice->capacity);
				}
				else if (strcmp(atts[0], "units") == 0) {		
					currDevice->units = getInt(atts[1]);
					CONFIGDEBUG fprintf(stderr, "Units = %d ", currDevice->units);
					if (currDevice->units > MAXUNITS) {
						journal.add(new Message("event ERROR Units value too large. Reducing it to MAXUNITS"));
						currDevice->units = MAXUNITS;
					}
				}
				else fprintf(stderr,"Device: unrecognised attribute %s\n", atts[0]);
				atts += 2;
			}
			numDevices++;	
			if (numDevices > MAXDEVICES) {
				journal.add(new Message("event ERROR Too many devices in config.xml. Increase MAXDEVICES"));
				numDevices --;
			}
			break;
			case meter_e:		// <meter power="yes/no">
			if (*atts && strcasecmp(atts[0], "power") == 0) {
				power = (strcasecmp(atts[1], "yes") == 0);
				CONFIGDEBUG fprintf(stderr,"<meter power=%d> ", power);
			}
			break;
			// These are simple elements, to be handled by Texthandler
			case maxcharge_e:		// COMPATABILITY
			case battery_e:				// COMPATABILITY
			case config_e:
			case port_e:
			case path_e:
			case options_e:
			case capabilities_e:
			break;
			case display_e:			// RICO within <device>
				currDevice->hasdisplay = true;
				while (*atts) {
					int val;
					CONFIGDEBUG fprintf(stderr, "<display %s='%s'>", atts[0], atts[1]);
					val = atoi(atts[1]);
					if (!val) {
						journal.add(new Message("event ERROR Nonnumeric or zero value in <display> tag"));
						continue;
					}
					if (strcasecmp(atts[0],"kw") == 0) {
						currDevice->kwdisp = val;
						rico.kwdisp = val;
						rico.kwdev = numDevices-1;
						CONFIGDEBUG fprintf(stderr, "Rico: setting kwdisp=%d kwdev=%d\n", val, numDevices-1);
					}
					if (strcasecmp(atts[0],"kwh") == 0) {
						currDevice->kwhdisp = val;
						rico.kwhdisp = val;
						rico.kwhdev = numDevices-1;
						CONFIGDEBUG fprintf(stderr, "Rico: setting kwhdisp=%d kwhdev=%d\n", val, numDevices-1);
					}
					if (strcasecmp(atts[0],"import") == 0) {
						currDevice->importdisp = val;
						rico.importdisp = val;
						rico.importdev = numDevices-1;
						CONFIGDEBUG fprintf(stderr, "Rico: setting importdisp=%d importdev=%d\n", val, numDevices-1);
					}
					
					atts+=2;
				}
				break;
			default: fprintf(stderr,"Unknown start element %s\n", name);
	}
}

/*-------------*/
/* TEXTHANDLER */
/*-------------*/

void Config::charData(const XML_Char *s, int len) {
	// accumulate data into buffer.
	// Data supplied in s in NOT null-terminated.  Just append it to the buffer buf
	// and move cp along to point to where to dump it to.
	// For safeties sake null-terminate it now (although this could be done in the
	// end element processing
	int curlen = strlen(text);
	if (curlen + len >= TEXTSIZE) {
		fprintf(stderr, "Warning - element text of more than %d encountered. Ignoring. Please increase TEXTSIZE\n", TEXTSIZE);
		return;
	}
	strncat(text, s, len);  // append new data
	text[len + curlen] = '\0';
}

/*------------*/
/* ENDELEMENT */
/*------------*/

void Config::endElement(const char *name)
// XML_Parse function called at element closing time.
// In particular, deal with content of tags.  This has been collected by TextHandler.
{
//	struct userdata * u;
	int element, i;
	for (element=0; elementTypeStr[element]; element++)
		if (strcmp(name, elementTypeStr[element]) == 0) {
			CONFIGDEBUG fprintf(stderr, " End tag %s Text = %s (%zu)\n", name, text, strlen(text));
			break;
		}
	switch(element) {
	case site_e:		// <site>
		siteName = text;
		break;
	case server_e:		// <server>
			serverUrl = text;
		break;
	case port_e:			// <port> within <device>
		CONFIGDEBUG fprintf(stderr, "With port as %s\n", text);
		currDevice->serialPort = text;
		break;
	case path_e:
		CONFIGDEBUG fprintf(stderr, "With path as %s\n", text);
		currDevice->path = text;
		break;
	case options_e:
		CONFIGDEBUG fprintf(stderr, "With options as %s\n", text);
			currDevice->options = text;
		break;
	case capabilities_e:
		CONFIGDEBUG fprintf(stderr, "With capabilities as '%s'\n", text);
		if (strcasestr(text, "InverterNoLoad") != 0)
			capabilities |= CAPINVERTERNOLOAD;
		if (strcasestr(text, "Modbus") != 0)
			capabilities |= CAPMODBUS;
		CONFIGDEBUG fprintf(stderr, " %d ", capabilities);
		break;
	case meter_e:
		CONFIGDEBUG fprintf(stderr, "With meter as '%s'\n", text);
		for(i = 0; meterStr[i] && (strcasecmp(meterStr[i], text) != 0); i++);
		if (meterStr[i])  {
			currDevice->meterType = (enum MeterType) i;
			if (currDevice->start)
				meter[i].inUse = true;
			else
				meter[i].inUse = false;		// Could be shortened at risk of obscurity
			meter[i].derivePower = power;
			CONFIGDEBUG fprintf(stderr, " setting Meter %d to InUse %s with power=%d\n", 
						i, meter[i].inUse ? "true" : "false", power);
		}
		else
			fprintf(stderr, "Didn't recognise meter type '%s (%d)'\n", text, i);
		
		break;
	default:;		// Don't need to do anything. It's also not an error, just text where we don't want it
	}
}

Config& Config::operator=(const Config & c) {
// Copy the important data from one Config to another
	CONFIGDEBUG fprintf(stderr, "Config= Siteid was %d wants to be %d ", siteId, c.siteId);
	
	if (siteId == -1) siteId = c.siteId;		// MTR Check this carefully
	CONFIGDEBUG fprintf(stderr, " Is now %d\n", siteId);
	capabilities = c.capabilities;
	siteName = c.siteName;
	timezone = c.timezone;
	serverComms = c.serverComms;
	username = c.username;
	password = c.password;
	serverUrl = c.serverUrl;
	mcpVersion = c.mcpVersion;
	numDevices = c.numDevices;
	theVictron = c.theVictron;
	rico = c.rico;
	if (rico.importdev != c.rico.importdev) fprintf(stderr, "WARNING rico structure not copied properly!\n");
	theSevenSeg = c.theSevenSeg;
	bCharging = c.bCharging;
	systemType=c.systemType;
	IMainsScale = c.IMainsScale;
	for (int i = 0; i < numDevices; i++) {
		int pid = device[i].pid;
		device[i] = c.device[i]; // why don't I need operator= for Device?
		device[i].pid = pid; //  Preserve PID
		DETAILDEBUG {
			fprintf(stderr, "Dev %d pid was %d is %d numvals was %d is %d ", i, c.device[i].pid, device[i].pid,
					c.device[i].numVals, device[i].numVals);
		}
	}
	if (modbus) {
		MEMDEBUG fprintf(stderr, "Freeing previous modbus[] at %x ", (int)modbus);
		free(modbus);
	}
	modbus = c.modbus;
	hwModel = c.hwModel;
	capacity = c.capacity;
	return *this;
}

/****************/
/* CHECKVERSION */
/****************/
void checkVersion(const string &config, const char * mcp){
	int config_major, config_minor, mcp_major, mcp_minor, n;
	n = sscanf(config.c_str(), "%d.%d", &config_major, &config_minor);
	if (n !=2) fprintf(stderr, "Cannot get major and minor numbers from %s\n", config.c_str());
	n = sscanf(mcp, "%d.%d", &mcp_major, &mcp_minor);
	if (n !=2) fprintf(stderr, "Cannot get major and minor numbers from %s\n", mcp);
	if (config_major > mcp_major)
		throw "Major number of config.xml higher than this program can support";
	if (config_major == mcp_major && config_minor > mcp_minor)
		throw "Minor number of config.xml higher than this program can support";
}
	
/**************/
/* GETVERSION */
/**************/
char *getversion(void) {
// return pointer to version part of REVISION macro
	static char version[10] = "";	// Room for xxxx.yyyy
	if (!strlen(version)) {
		strcpy(version, REVISION+11);
		version[strlen(version)-2] = '\0';
	}
return version;
}

/**************/
/* FINDSTRING */
/**************/
int findString(const char * msg, const char *list[]) {
//return index of msg in list or -1 for not found.  List must be terminated with NULL
	int i;
	for (i = 0; list[i] && strcasecmp(msg, list[i]) != 0; i++);
	if (list[i]) return i;
	else return -1;
}
