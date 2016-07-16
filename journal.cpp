/*
 *  journal.cpp -- part of mcp1.0
 *
 *  Created by Martin Robinson on 17/09/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * $Id: journal.cpp,v 3.5 2013/04/08 20:20:55 martin Exp $
 */

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>		// for strtof
	using std::string;
#include <iostream>
	using std::cerr; using std::endl;
	using std::ostream;
#include <sstream>
	using std::ostringstream;
#include <fcntl.h>
#include "mcp.h"
#include "meter.h"
#include "journal.h"
#include "config.h"
#include "comms.h"
#include "action.h"
#include "command.h"

extern Config conf; 
extern Variables var;
extern Sockconn socks;
extern Journal journal;
extern struct sockconn_s consock;
extern Data prev;
extern MeterData meter[];

const char *invertername[] = INVERTERNAMES;
const char *thermalname[] = THERMALNAMES;
const char *resolname[] = RESOLNAMES;
const char *sensorname[] = SENSORNAMES;

/* UTILITY FUNCTIONS */
string & htmlentities(string & str) {
	int loc = 0;
	while ((loc = str.find('&', loc)) != string::npos)
        str.replace(loc++, 1, "&amp;");
	loc = 0;
	while ((loc = str.find('<', loc)) != string::npos)
        str.replace(loc, 1, "&lt;");
	loc = 0;
	while ((loc = str.find('>', loc)) != string::npos)
        str.replace(loc, 1, "&gt;");
	return str;
}

#define makeshort(lsb, msb)  ( lsb | (msb << 8))

char buf[200];  // Global for formatting text messages

/* JOURNAL FUNCTIONS */

Journal::~Journal () {
	for (journal_type::iterator ji = journal.begin(); ji != journal.end(); ++ji) {
		MEMDEBUG cerr << "Journal deleting " << *ji << "\n";
		delete *ji;
		}	
}

void Timestamp::xml(FILE * f) const {
	struct tm * now_tm = localtime(&timeval);
	fprintf(f, "\"%04d-%02d-%02dT%02d:%02d:%02d%+03ld:%02zd\"", now_tm->tm_year + 1900,	now_tm->tm_mon + 1, 
		now_tm->tm_mday,	now_tm->tm_hour,	now_tm->tm_min,	now_tm->tm_sec, now_tm->tm_gmtoff/3600, 
		now_tm->tm_gmtoff % 3600);
}

char * Timestamp::getTime(void) const {
// Return pointer to string in form yyyy/mm/dd hh:mm:ss
	static char buf [20];		// overrides global buf
	struct tm * mytm;
	mytm = localtime(&timeval);
	snprintf(buf, sizeof(buf), "%4d/%02d/%02d %02d:%02d:%02d", mytm->tm_year + 1900, mytm->tm_mon+1,
		mytm->tm_mday, mytm->tm_hour, mytm->tm_min, mytm->tm_sec);
	return buf;
}
void Entry::xml(FILE * f) const {
	fprintf(f,"  <entry time=");
	timeval.xml(f);
	fprintf(f,">\n");
}

void Message::xml(FILE * f) const {
	Entry::xml(f);		// shame - would be nice to call the parent
	fprintf(f, "    <message>%s</message>\n  </entry>\n", msg.c_str());
}

void Data::xml(FILE * f) const {
	Entry::xml(f);
	fprintf(f, "    <data>\n      <period>%d</period>\n", period);
	if (wmains)	fprintf(f, "      <Wmains>%.2f</Wmains>\n", wmains);
	if (wload)	fprintf(f, "      <Wload>%.2f</Wload>\n", wload);
	if (wexport)fprintf(f, "      <Wexport>%.2f</Wexport>\n", wexport);
	if (wsolar) fprintf(f, "      <Wsolar>%.2f</Wsolar>\n", wsolar);
	if (wthermal) fprintf(f, "      <Wthermal>%.2f</Wthermal>\n", wthermal);
	if (wwind)	fprintf(f, "      <Wwind>%.2f</Wwind>\n", wwind);
	if (emains)	fprintf(f, "      <Emains>%.2f</Emains>\n", emains);
	if (eload)	fprintf(f, "      <Eload>%.2f</Eload>\n", eload);
	if (eexport)fprintf(f, "      <Eexport>%.2f</Eexport>\n", eexport);
	if (esolar)	fprintf(f, "      <Esolar>%.2f</Esolar>\n", esolar);
	if (ethermal)fprintf(f, "      <Ethermal>%.2f</Ethermal>\n", ethermal);
	if (ewind)	fprintf(f, "      <Ewind>%.2f</Ewind>\n", ewind);
	if (vbatt)	fprintf(f, "      <Vbatt>%.2f</Vbatt>\n", vbatt);
	if (ibatt)	fprintf(f, "      <Ibatt>%.2f</Ibatt>\n", ibatt);
	if (soc)	fprintf(f, "      <SOC>%.2f</SOC>\n", soc);
	if (irradiation) 	fprintf(f, "      <irradiation>%.2f</irradiation>\n", irradiation);
	if (ambient > -273.0) 	fprintf(f, "      <ambient>%.2f</ambient>\n", ambient);
	if (paneltemp > -273.0) fprintf(f, "      <paneltemp>%.2f</paneltemp>\n", paneltemp);
	if (pulsein + pulseout) fprintf(f, "      <InPulses>%d</InPulses><OutPulses>%d</OutPulses>\n", pulsein, pulseout);
	fprintf(f, "    </data>\n  </entry>\n");
}

void Status::xml(FILE *f) const {
	Entry::xml(f);
	fprintf(f, "    <status><load>%f</load><diskused>%.f</diskused><diskfree>%.f</diskfree>\n",
		load, diskused, diskfree);
	fprintf(f, "      <memused>%.f</memused><memfree>%.f</memfree>\n    </status>\n  </entry>\n", memused, memfree);
}

/*	RAWDATA */	//////////////////////////////////////////////////////////////////////////////////////////////////////

void RawData::xml(FILE * f) const {
	// This is where the <inverter> tag is written out.
	// This violates most principles of good style ond object orientation in particular
	// Let me count the ways:
	// Data hiding : an object should know how to represent itself without special knowledge
	// Duplication: knowledge should not be expressed in multiple places
	// Specifically the rawdata is meant to know nothing about the data it holds
	// Bad style - the point of dynamic typing is to avoid case statement and special cases
	// That said, re-interpreting the raw data here avoids having an Inverter object which would 
	// probably double the size of the journal
	
	// 2.8 removed <rawdata>
	int i, j, num;
	if (numdevices) {
		try {
			// Count inverters and sensors
			for (i = 0, num = 0; i < numdevices; i++) 
				if (conf[i].type == fronius_t || 
					conf[i].type == soladin_t || 
					conf[i].type == sma_t || 
					conf[i].type == inverter_t) num++;
			
			if (num) {
				Entry::xml(f);
				fprintf(f, "    <inverters>\n");
				for (i = 0, num = 0; i < numdevices; i++) 
				for (j = 0; j < conf[i].units; j++)
					if (devices[i][j] && conf[i].type == fronius_t) {
						if (devices[i][j]->getNumVals() != FRONIUSVALS)
							fprintf(stderr, "RawData::xml ERROR device %d.%d has %d vals not %d\n", i, j, devices[i][j]->getNumVals(), FRONIUSVALS);
						if (conf[i].units > 1) 
							fprintf(f, "      <inverter device=\"%d\" unit=\"%d\" type=\"solar\">\n", i, j);
						else
							fprintf(f, "      <inverter device=\"%d\"type=\"solar\">\n", i);
						fprintf(f, "        <watts>%.f</watts>\n", devices[i][j]->vals[0]);
						fprintf(f, "        <kwh>%.1f</kwh>\n", devices[i][j]->vals[1] / 1000.0);
						fprintf(f, "        <vdc>%.1f</vdc>\n", devices[i][j]->vals[8]);
						fprintf(f, "        <idc>%.2f</idc>\n", devices[i][j]->vals[7]);
						fprintf(f, "        <vac>%.1f</vac>\n", devices[i][j]->vals[5]);
						fprintf(f, "        <iac>%.2f</iac>\n", devices[i][j]->vals[4]);
						fprintf(f, "        <hz>%.2f</hz>\n",   devices[i][j]->vals[6]);
						fprintf(f, "      </inverter>\n");
					} 
					else if (devices[i][j] && conf[i].type == soladin_t) {
						if (devices[i][j]->getNumVals() != SOLADINVALS)
							fprintf(stderr, "RawData::xml ERROR device %d.%d has %d vals not %d\n", i, j, devices[i][j]->getNumVals(), SOLADINVALS);
						if (conf[i].units > 1) 
							fprintf(f, "      <inverter device=\"%d\" unit=\"%d\" type=\"%s\">\n", i, j, conf[i].model == solar_model ? "solar" : "wind");
						else
							fprintf(f, "      <inverter device=\"%d\"type=\"%s\">\n", i, conf[i].model == solar_model ? "solar" : "wind");
						fprintf(f, "        <watts>%.f</watts>\n", devices[i][j]->vals[4]);
						fprintf(f, "        <kwh>%.1f</kwh>\n", devices[i][j]->vals[5] / 1000.0);
						fprintf(f, "        <vdc>%.1f</vdc>\n", devices[i][j]->vals[0]);
						fprintf(f, "        <idc>%.2f</idc>\n", devices[i][j]->vals[1]);
						fprintf(f, "        <vac>%.1f</vac>\n", devices[i][j]->vals[3]);
						fprintf(f, "        <hz>%.2f</hz>\n",   devices[i][j]->vals[2]);
						fprintf(f, "      </inverter>\n");
					}
					else if (devices[i][j] && conf[i].type == sma_t) {
						if (devices[i][j]->getNumVals() != SMAVALS)
							fprintf(stderr, "RawData::xml ERROR device %d.%d has %d vals not %d\n", i, j, devices[i][j]->getNumVals(), SMAVALS);
						if (conf[i].units > 1) 
							fprintf(f, "      <inverter device=\"%d\" unit=\"%d\" type=\"%s\">\n", i, j, conf[i].model == solar_model ? "solar" : "wind");
						else
							fprintf(f, "      <inverter device=\"%d\" type=\"%s\">\n", i, conf[i].model == solar_model ? "solar" : "wind");
						fprintf(f, "        <watts>%.f</watts>\n", devices[i][j]->vals[0]);
						fprintf(f, "        <kwh>%.1f</kwh>\n", devices[i][j]->vals[1]);
						fprintf(f, "        <vdc>%.1f</vdc>\n", devices[i][j]->vals[6]);
						fprintf(f, "        <idc>%.2f</idc>\n", devices[i][j]->vals[5]);
						fprintf(f, "        <vac>%.1f</vac>\n", devices[i][j]->vals[3]);
						fprintf(f, "        <iac>%.1f</iac>\n", devices[i][j]->vals[2]);
						fprintf(f, "        <hz>%.2f</hz>\n",   devices[i][j]->vals[4]);
						fprintf(f, "      </inverter>\n");
					}
					else if (devices[i][j] && conf[i].type == inverter_t) {
						float v;
						if (devices[i][j]->getNumVals() != INVERTERVALS)
							fprintf(stderr, "RawData::xml ERROR device %d.%d has %d vals not %d\n", i, j, devices[i][j]->getNumVals(), INVERTERVALS);
						if (conf[i].units > 1) 
							fprintf(f, "      <inverter device=\"%d\" unit=\"%d\" type=\"%s\">\n", i, j, conf[i].model == wind_model ? "wind" : "solar");
						else
							fprintf(f, "      <inverter device=\"%d\" type=\"%s\">\n", i, conf[i].model == wind_model ? "wind" : "solar");
						fprintf(f, "        <watts>%.1f</watts>\n", devices[i][j]->vals[name("watts", invertername)]);
						v = devices[i][j]->vals[name("wdc", invertername)];		if (v) fprintf(f, "        <wdc>%.1f</wdc>\n", v);
						v = devices[i][j]->vals[name("wdc2", invertername)];	if (v) fprintf(f, "        <wdc2>%.1f</wdc2>\n", v);
						fprintf(f, "        <kwh>%.1f</kwh>\n", devices[i][j]->vals[name("kwh", invertername)]);
						fprintf(f, "        <iac>%.1f</iac>\n", devices[i][j]->vals[name("iac", invertername)]);
						v = devices[i][j]->vals[name("iac2", invertername)];	if (v) fprintf(f, "        <iac2>%.1f</iac2>\n", v);
						v = devices[i][j]->vals[name("iac3", invertername)];	if (v) fprintf(f, "        <iac3>%.1f</iac3>\n", v);
						fprintf(f, "        <vac>%.1f</vac>\n", devices[i][j]->vals[name("vac", invertername)]);
						v = devices[i][j]->vals[name("vac2", invertername)];	if (v) fprintf(f, "        <vac2>%.1f</vac2>\n", v);
						v = devices[i][j]->vals[name("vac3", invertername)];	if (v) fprintf(f, "        <vac3>%.1f</vac3>\n", v);
						fprintf(f, "        <hz>%.2f</hz>\n",   devices[i][j]->vals[name("hz", invertername)]);
						v = devices[i][j]->vals[name("hz2", invertername)];	if (v) fprintf(f, "        <hz2>%.1f</hz2>\n", v);
						v = devices[i][j]->vals[name("hz3", invertername)];	if (v) fprintf(f, "        <hz3>%.1f</hz3>\n", v);
						fprintf(f, "        <vdc>%.1f</vdc>\n", devices[i][j]->vals[name("vdc", invertername)]);
						v = devices[i][j]->vals[name("vdc2", invertername)];	if (v) fprintf(f, "        <vdc2>%.1f</vdc2>\n", v);
						fprintf(f, "        <idc>%.2f</idc>\n", devices[i][j]->vals[name("idc", invertername)]);
						v = devices[i][j]->vals[name("idc2", invertername)];	if (v) fprintf(f, "        <idc2>%.1f</idc2>\n", v);
						v = devices[i][j]->vals[name("ophr", invertername)];	if (v) fprintf(f, "        <ophr>%.1f</ophr>\n", v);
						v = devices[i][j]->vals[name("t1", invertername)];		if (v) fprintf(f, "        <t1>%.1f</t1>\n", v);
						v = devices[i][j]->vals[name("t2", invertername)];		if (v) fprintf(f, "        <t2>%.1f</t2>\n", v);
						v = devices[i][j]->vals[name("t3", invertername)];		if (v) fprintf(f, "        <t3>%.1f</t3>\n", v);
						v = devices[i][j]->vals[name("t4", invertername)];		if (v) fprintf(f, "        <t4>%.1f</t4>\n", v);
						v = devices[i][j]->vals[name("fan1", invertername)];	if (v) fprintf(f, "        <fan1>%.2f</fan1>\n", v);
						v = devices[i][j]->vals[name("fan2", invertername)];	if (v) fprintf(f, "        <fan2>%.2f</fan2>\n", v);
						v = devices[i][j]->vals[name("fan3", invertername)];	if (v) fprintf(f, "        <fan3>%.2f</fan3>\n", v);
						fprintf(f, "      </inverter>\n");
					}
				fprintf(f, "    </inverters>\n  </entry>\n");
			}
			
			num = 0;		// Thermals section
			for (i = 0; i < numdevices; i++) 
				if (conf[i].type == thermal_t ||
					conf[i].type == resol_t) num++;
			if (num) {
				Entry::xml(f);
				fprintf(f, "    <thermals>\n");
				for (i = 0; i < numdevices; i++)
				for (j = 0; j < conf[i].units; j++) {
					if (devices[i][j] && conf[i].type == thermal_t) {
						float v;
						if (devices[i][j]->getNumVals() != THERMALVALS)
							fprintf(stderr, "RawData::xml ERROR device %d.%d has %d vals not %d\n", i, j, devices[i][j]->getNumVals(), THERMALVALS);
						if (conf[i].units > 1) 
							fprintf(f, "      <thermal device=\"%d\" unit=\"%d\">\n", i, j);
						else
							fprintf(f, "      <thermal device=\"%d\">\n", i);
						fprintf(f, "        <kwh>%.1f</kwh>\n", devices[i][j]->vals[name("kwh", thermalname)]);
						v = devices[i][j]->vals[name("col", thermalname)];		if (v) fprintf(f, "        <col>%.1f</col>\n", v);
						v = devices[i][j]->vals[name("col2", thermalname)];	if (v) fprintf(f, "        <col2>%.1f</col2>\n", v);
						v = devices[i][j]->vals[name("tst", thermalname)];		if (v) fprintf(f, "        <tst>%.1f</tst>\n", v);
						v = devices[i][j]->vals[name("tstu", thermalname)];	if (v) fprintf(f, "        <tstu>%.1f</tstu>\n", v);
						v = devices[i][j]->vals[name("tstl", thermalname)];	if (v) fprintf(f, "        <tstl>%.1f</tstl>\n", v);
						v = devices[i][j]->vals[name("trf", thermalname)];		if (v) fprintf(f, "        <trf>%.1f</trf>\n", v);
						v = devices[i][j]->vals[name("tst2l", thermalname)];	if (v) fprintf(f, "        <tst2l>%.1f</tst2l>\n", v);
						v = devices[i][j]->vals[name("tst2u", thermalname)];	if (v) fprintf(f, "        <tst2u>%.1f</tst2u>\n", v);
						v = devices[i][j]->vals[name("extra1", thermalname)];	if (v) fprintf(f, "        <extra1>%.1f</extra1>\n", v);
						v = devices[i][j]->vals[name("extra2", thermalname)];	if (v) fprintf(f, "        <extra2>%.1f</extra2>\n", v);
						v = devices[i][j]->vals[name("n1", thermalname)];		if (v) fprintf(f, "        <n1>%.1f</n1>\n", v);
						v = devices[i][j]->vals[name("n2", thermalname)];		if (v) fprintf(f, "        <n2>%.1f</n2>\n", v);
						v = devices[i][j]->vals[name("ophr", thermalname)];	if (v) fprintf(f, "        <ophr>%.1f</ophr>\n", v);
						fprintf(f, "      </thermal>\n");
					}
					if (devices[i][j] && conf[i].type == resol_t) {
						float v;
						if (devices[i][j]->getNumVals() != RESOLVALS)
							fprintf(stderr, "RawData::xml ERROR device %d.%d has %d vals not %d\n", i, j, devices[i][j]->getNumVals(), RESOLVALS);
						if (j) 
							fprintf(f, "      <thermal device=\"%d\" unit=\"%d\">\n", i, j);
						else
							fprintf(f, "      <thermal device=\"%d\">\n", i);
						fprintf(f, "        <kwh>%.1f</kwh>\n", devices[i][j]->vals[name("kwh", resolname)]);
						v = devices[i][j]->vals[name("col", resolname)];		if (v) fprintf(f, "        <col>%.1f</col>\n", v);
						v = devices[i][j]->vals[name("tst", resolname)];		if (v) fprintf(f, "        <tst>%.1f</tst>\n", v);
						v = devices[i][j]->vals[name("tstu", resolname)];		if (v) fprintf(f, "        <tstu>%.1f</tstu>\n", v);
						v = devices[i][j]->vals[name("trf", resolname)];		if (v) fprintf(f, "        <trf>%.1f</trf>\n", v);
						v = devices[i][j]->vals[name("n1", resolname)];			if (v) fprintf(f, "        <n1>%.1f</n1>\n", v);
						fprintf(f, "      </thermal>\n");
					}
				}
				fprintf(f, "    </thermals>\n  </entry>\n");
			}	// End of thermals 
			
		}		// try
		
		catch (const char * msg) {
			snprintf(buf, sizeof(buf), "event WARN Rawdata::xml Exception: %s", msg);
			journal.add(new Message(buf));
		}
	} // if (numdevices)
}

void RawData::addDevice(const int device, const int unit, const int num, const float vals[]) {
	DEBUG fprintf(stderr,"RawData::addDevice device = %d unit = %d num = %d\n", device, unit, num);
	if (devices[device][unit]) {
		fprintf(stderr, "Rawdata::addDevice ERROR - [%d][%d] is not null but 0x%x - deleting it\n", device, unit, (int)devices[device][unit]);
		delete devices[device][unit];
	}
	devices[device][unit] = new DeviceData(device, num);
	MEMDEBUG cerr << "DeviceData created at " << devices[device][unit] << " Size " << sizeof(devices[device][unit]) << endl;
	devices[device][unit]->setVals(vals);
	if (device >= numdevices) numdevices = device + 1;
}

RawData::~RawData() {
	for (int i = 0; i < numdevices; i++)
		for (int j = 0; j < conf[i].units; j++)
			if (devices[i][j]) 	{ 
				MEMDEBUG cerr << "Rawdata Delete device " << devices[i][j] << "\n";
				delete devices[i][j];
				devices[i][j] = NULL;
			}
}

/*	DEVICEDATA */	//////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceData::xml(FILE * f) const {
	if (numvals) {
		fprintf(f, "      <values device=\"%d\" ids=\"%d\">\n", devicenum, numvals);
		for (int i = 0; i < numvals; i++)
			fprintf(f, "        <value id=\"%d\">%f</value>\n", i, vals[i]);
		fprintf(f, "      </values>\n");
	}
}

/*  METERENTRY*/  /////////////////////////////////////////////////////////////////////////////////////////////////////
void MeterEntry::xml(FILE * fp) const {
// representation to open file stream
// At the moment, just use reading1 until it all gets sorted out
	Entry::xml(fp);		
	fprintf(fp, "    <meter device=\"%d\">%.3f</meter>\n  </entry>\n", device, reading1);
}

/*	CURRENT */	//////////////////////////////////////////////////////////////////////////////////////////////////////

Current::Current() {
	DEBUG cerr << "Current initialised\n";
	for (int i = 0; i < MAXDEVICES; i++)
		for (int j = 0; j < MAXUNITS; j++) {
			currentdata[i][j] = NULL;
			prevWload[i][j] = prevWsolar[i][j] = 0.0;
	}
	numdevices = 0;
	last = time(0);
}

Current::~Current() {
	DEBUG cerr << "Current destructor\n";
	for (int i = 0; i< MAXDEVICES; i++)
		for (int j = 0; j < MAXUNITS; j++)
			if (currentdata[i][j]) {
				MEMDEBUG fprintf(stderr, "Deleting currentdata[%d][%d] 0x%x ", i, j, (int)currentdata[i][j]);
				delete currentdata[i][j];
				currentdata[i][j] = NULL;
			}
}

void Current::setDevice(const int i, const int unit, enum DeviceType type) {
	// ADD CONTROLLER
	DETAILDEBUG cerr << " SetDevice " << i << " as " << deviceStr[type] << "\n";
	if (i >= MAXDEVICES) {
		ostringstream msg;
		msg << "event ERROR Can't add another device - increase MAXDEVICES";
		journal.add(new Message(msg.str()));
		return;
	}
	CurrentData * cdp;
	switch(type) {
		case steca_t:		cdp = new Steca(i); break;
		case victron_t:		cdp = new Victron(i); break;
		case turbine_t:		cdp = new Turbine(i); break;
		case fronius_t: 	cdp = new Fronius(i); break;
		case davis_t:		cdp = new Davis(i); break;
		case pulse_t:		cdp = new Pulse(i); break;
		case rico_t:		cdp = new Rico(i); break;
		case generic_t:		cdp = new Generic(i); break;
		case owl_t:			cdp = new Owl(i); break;
		case soladin_t:		cdp = new Soladin(i); break;
		case sevenseg_t:	cdp = new SevenSeg(i); break;
		case meter_t:		cdp = new Meter(i); break;
		case resol_t:		cdp = new Resol(i); break;
		case sma_t:			cdp = new Sma(i); break;
		case inverter_t:	cdp = new Inverter(i); break;
		case thermal_t:		cdp = new Thermal(i); break;
		case sensor_t:		cdp = new Sensor(i); break;
			
		default: fprintf(stderr,"Error setting device %d.%d: type = %d\n", i, unit, conf.getType(i));
			cdp = new Generic(i);
			// Doesn't work to set this to NULL - get a segmentation fault.
	}
	DEBUG cerr << "Current::setDevice " << i << "." << unit << endl;
	if (currentdata[i][unit]) {
		DEBUG cerr << "(Deleting previous data)";
		delete currentdata[i][unit];
	}
	MEMDEBUG cerr << "CurrentData:setDevice CDP " << cdp << endl;
	currentdata[i][unit] = cdp;
	if (i >= numdevices) numdevices = i + 1;
}

void Current::setDevices(Config & conf) {
// Setup Current data
	for (int i = 0; i < conf.size(); i++) {
		DETAILDEBUG cerr << "SetDevices " << i << " as type " << conf.getType(i);
		for(int j = 0; j < conf[i].units; j++)
			setDevice(i, j, conf.getType(i));
		DEBUG getData(i, 0)->dump();
	}
}

void Current::setUnits(const int dev, const char * message) {
	// Deal with 'units 2' messages 
	int i, units;
	try {
		units = getNonZeroInt(message + 6, 0);
	}
	catch (const char *) {
		return;
	}
	DEBUG fprintf(stderr, "SetUnits: Device %d changing units from %d to %d ", dev, conf[dev].units, units);
	if (units > MAXUNITS) {
		ostringstream outmsg;
		outmsg << "event ERROR Can't set Units to " << units << " as MAXUNITS is " << MAXUNITS;
		journal.add(new Message(outmsg.str()));
		return;
	}
	for (i = conf[dev].units; i < units; i++)
		setDevice(dev, i, conf.getType(dev));
	if (units > conf[dev].units)	 // Never actually decrease it - memory leak?
		conf[dev].units = units;
}

void Current::addData (const int num, const int unit, const char * data) {
// Add a row of floats into the correct currentdata: format data xx xx xx xx
	DETAILDEBUG fprintf(stderr, "Current:addData device %d.%d counts: %d data '%s'\n", num, unit, currentdata[num][unit]->getCount(), data);
	float v[10];
	int read, expected;
	if (currentdata[num][unit] == 0) {
		fprintf(stderr,"\nERROR Currentdata[%d][%d] not allocated using SetDevice\n", num, unit);
		return;
	}
	read = sscanf(data, "%*s %d %f %f %f %f %f %f %f %f %f %f", &expected,
	&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
	if (read != expected + 1) {
		fprintf(stderr, "ERROR read %d expected %d in %s\n", read, expected, data);
	}
	if (currentdata[num][unit]->getnum() != expected) 
		fprintf(stderr, "ERROR This device expects %d values; got %d from %s\n", 
			currentdata[num][unit]->getnum(), expected, data);
	currentdata[num][unit]->addval(v);
	// Process state change
	DETAILDEBUG cerr << "Calling StateChange for dev " << num << endl;
	currentdata[num][unit]->stateChange(v);
	if (conf.bModbus())
	{
		currentdata[num][unit]->modbus();
		MODBUSDEBUG fprintf(stderr, "Called MODBUS from Current::addData");
	}
}

void Current::parse(const int num, const int unit, char * data){
// Parse a row of form inverter w:123  kwh:123
	DETAILDEBUG fprintf(stderr, "In Current::parse %d %d with %s ", num, unit, data);
	ostringstream outmsg;
	if (unit >= conf[num].units) {
		outmsg << "event WARN Current:parse Device " << num << "." << unit << " only " << conf[num].units << " units defined. Ignoring data";
		journal.add(new Message(outmsg.str()));
		return;
		}
	if (!currentdata[num][unit]) {
		outmsg << "event ERROR Current:parse Device " << num << "." << unit << " not defined. Ignoring data";
		journal.add(new Message(outmsg.str()));
		return;
	}
	try {
		DETAILDEBUG fprintf(stderr, "About to parse and conf.bModbus() = %d\n", conf.bModbus());
		currentdata[num][unit]->parse(data);
		if (conf.bModbus())
		{
			currentdata[num][unit]->modbus();
			MODBUSDEBUG fprintf(stderr, "Called MODBUS from Currrent::parse");
		}
		
		if (conf.bOneInverter() ) 
		{
			// If there is only one inverter use these values to update
			float watts, kwh;
			CurrentData * temp(NULL);
			
			int t = conf[num].type;
			switch(t) {
				case inverter_t:	temp = new Inverter(0);
					break;
				default:
					DEBUG cerr << "Error in Current::parse trying to process row for non-inverter " << conf[num].type << "\n";
			}
			if (temp == 0)
				return;
			temp->parse(data);
			watts = temp->name("watts");
			kwh = temp->name("kwh");
			DEBUG cerr << "OneInverter: Watts " << watts << " KWH " << kwh << endl;
			modbus(watts, kwh);	// do the update
			delete temp;
		}
	}
	catch (const char * msg) {
		DEBUG fprintf(stderr, "Parse Error: %s\n", msg);
		ostringstream outmsg;
		outmsg << "event ERROR Parse error on device " << num << "." << unit << ": " << msg;
		journal.add(new Message(outmsg.str()));
	}
}

void Current::davis(const int num, char * data){
	// Put the irradiation and temp values into the vals (same as sensorvals)
	if (!currentdata[num][0]) {
		fprintf(stderr, "Current:davis Device %d not defined. Ignoring data\n", num);
		return;
	}
	try {
		currentdata[num][0]->davis(data);
	}
	catch (const char * msg) {
		DEBUG fprintf(stderr, "Davis Error: %s\n", msg);
		ostringstream outmsg;
		outmsg << "event ERROR Davis error on device " << num << ": " << msg;
		journal.add(new Message(outmsg.str()));
	}
}

void Current::consolidate(Journal & journal) {
	// Consolidate currentdata and populate currentdata.
	/* A bit of a nasty.  The turbine ONLY knows about current, so we can't do the 
	 hasWWind .. getWWind dance. We need to nick a voltage from another devices 
	 data.  This will work IFF the turbine device is higher in device number
	 than the Victron, so we cache the VBatt value.  
	 Even trickier - use a default value so in the event of missing data we get an
	 approximation.
	 
	 Also figure out the LocalGeneration value
	 
	 Also 'unblip' unexpected zero values but issue a WARN message. Currently for SOC only.
	 
	 Also also write the /tmp/vars.dat file
	 
	 Also check for min battery voltage
	 
	 Also preserve ESolar high-water mark to handle inverters shutting down overnight. (NVRAM)
	 
	 Also (1.43) send data to Rico displays if it hasn;t been done already in the ::StateChange functions
	 
	 Also (3.4) invoke Current::modbus()
	 
	 */
	
#define VARSDAT "/tmp/vars.dat"
	
	DEBUG fprintf(stderr, "Consolidating ...\n");
	Data * dp = new Data;
	float battvolts = 50;
	RawData * rdp = new RawData;
	var.localGeneration = 0;
	int stecas = 0;
	int i, j;
	int ok = true;	// Used to detect devices not producing data
	
	for (i = 0; i < numdevices; i++)
		for (j = 0; j < conf[i].units; j++) {
		try {		// name() can throw an exception
			if (currentdata[i][j] == NULL) {
				fprintf(stderr,"Current::Consolidate ERROR currentdata[%d][%d] is NULL\n", i, j);
				continue;
			}
			DEBUG fprintf(stderr, "Device %d.%d: %d samples ", i, j, currentdata[i][j]->getCount());
			if (currentdata[i][j]->getCount()) {
				float val;
				if (!dynamic_cast<Pulse *>(currentdata[i][j]))	// don't average Meter pulses
					currentdata[i][j]->average();
				if (dynamic_cast<Steca *> (currentdata[i][j])) {
					DETAILDEBUG cerr << "Processing a Steca row\n";
					stecas ++;
				}
				DETAILDEBUG fprintf(stderr," HasWload %d HasWwind %d HasWsolar %d HasEWind %d HasESolar %d ",
						currentdata[i][j]->hasWload(), 
						currentdata[i][j]->hasWwind(), 
						currentdata[i][j]->hasWsolar(), 
						currentdata[i][j]->hasEwind(), 
						currentdata[i][j]->hasEsolar()); 
				
				rdp->addDevice(i, j, currentdata[i][j]->getnum(), currentdata[i][j]->getVals());
				if (currentdata[i][j]->hasWmains())    dp->wmains = currentdata[i][j]->getWmains();
				
				if (currentdata[i][j]->hasWload())     {
					val = currentdata[i][j]->getWload();
					val = sanityCheck(val, i, prevWload[i][j], "WLoad");
					prevWload[i][j] = val;
					dp->wload += val;
				}
				if (currentdata[i][j]->hasWsolar())    {
					val = currentdata[i][j]->getWsolar();
					val = sanityCheck(val, i, prevWsolar[i][j], "WSolar");
					prevWsolar[i][j] = val;
					dp->wsolar += val;
					var.localGeneration += val;
					DETAILDEBUG fprintf(stderr, "HasWSolar: %f ", val);
					
				}
				if (currentdata[i][j]->hasWwind())    {
					val = currentdata[i][j]->getWwind();
					dp->wwind += val;
					var.localGeneration += val;
					DETAILDEBUG fprintf(stderr, "HasWWind: %f ", val);
				}
				if (currentdata[i][j]->hasWexport())     dp->wexport = currentdata[i][j]->getWexport();
				if (currentdata[i][j]->hasIwind())     {
					dp->wwind = val = currentdata[i][j]->getIwind() * battvolts;
					var.localGeneration += val;
				}
				if (currentdata[i][j]->hasVbatt())     battvolts = dp->vbatt = currentdata[i][j]->getVbatt();
				if (currentdata[i][j]->hasIbatt())     dp->ibatt = currentdata[i][j]->getIbatt();
				if (currentdata[i][j]->hasSOC())       dp->soc += currentdata[i][j]->getSOC();
				if (currentdata[i][j]->hasPulses())   {
					dp->pulsein = currentdata[i][j]->getPulsesIn();
					dp->pulseout = currentdata[i][j]->getPulsesOut();
				}
				if (currentdata[i][j]->hasVdc())	dp->vdc = currentdata[i][j]->getVdc();
				if (currentdata[i][j]->hasEsolar())	dp->esolar += currentdata[i][j]->getEsolar();
				if (currentdata[i][j]->hasEwind())	dp->ewind += currentdata[i][j]->getEwind();
				
				// 2.7 Sensor handling
				// 2.8 Includes Davis as well an Sensor
				if (currentdata[i][j]->isSensor()) {
					dp->irradiation = currentdata[i][j]->getIrradiation();
					dp->ambient = currentdata[i][j]->getAmbient();
					dp->paneltemp = currentdata[i][j]->getPaneltemp();
					DEBUG fprintf(stderr, "SENSOR setting irradiation %.2f ambient %.2f paneltemp %.2f\n", 
								  dp->irradiation, dp->ambient, dp->paneltemp);
				}
				
				// If the device is configured to run but has no data, set the flag used to write the status line.
				if (conf[i].start && currentdata[i][j]->getCount() == 0) ok = false;
				DEBUG fprintf(stderr,"Device %d.%d Start: %d Count %d OK now %d\n", i, j, conf[i].start,currentdata[i][j]->getCount(), ok);
			}
			
			currentdata[i][j]->clear();	// ready for next time
		}
		catch (const char * msg) {
			snprintf(buf, sizeof(buf), "event ERROR Exception '%s'", msg);
			journal.add(new Message(buf));
		}
	}
	
	// Solar high water mark
	DETAILDEBUG fprintf(stderr, "Before esolar = %.1f high water = %.1f ", dp->esolar, var.esolarhighwater);
	if (var.esolarhighwater > 1e6) {
		snprintf(buf, sizeof(buf), "event WARN Solar High water unlikely value %f. Resetting to zero", var.esolarhighwater);
		var.esolarhighwater = 0.0;
		journal.add(new Message(buf));
	}
	// If reported value is less than high water, presumably an inverter has gone offline, so use previous value.
	
	if (dp->esolar < var.esolarhighwater) dp->esolar = var.esolarhighwater;
	// Sanity checking on using a new high value as new high water mark:
	// Unfortunately the esolar value gets averaged, so the first time around it could
	// be 1/5th of the true value. So trust values within a factor of 5 of the previous one
	// (5 comes from the number of samples per consolidation period - but is stored in solarincmax)
	if (dp->esolar > var.esolarhighwater + var.highwaterincmax && var.esolarhighwater > 0) {
			snprintf(buf, sizeof(buf), "event WARN Discarding eSolar value of %.1f (High water mark is %.1f)", dp->esolar, var.esolarhighwater);
			journal.add(new Message(buf));
			dp->esolar = var.esolarhighwater;
	}
	else if (dp->esolar > var.esolarhighwater) {
		var.esolarhighwater = dp->esolar;
	}
	
	// Wind high water mark
	DETAILDEBUG fprintf(stderr, "Before ewind = %.1f high water = %.1f ", dp->ewind, var.ewindhighwater);
	if (var.ewindhighwater > 1e6) {
		snprintf(buf, sizeof(buf), "event WARN Wind High water unlikely value %f. Resetting to zero", var.ewindhighwater);
		var.ewindhighwater = 0.0;
		journal.add(new Message(buf));
	}
	// If reported value is less than high water, presumably an inverter has gone offline, so use previous value.
	
	if (dp->ewind < var.ewindhighwater) dp->ewind = var.ewindhighwater;
	// Sanity checking on using a new high value as new high water mark:
	// Unfortunately the esolar value gets averaged, so the first time around it could
	// be 1/5th of the true value. So trust values within a factor of 5 of the previous one
	// (5 comes from the number of samples per consolidation period - but is stored in solarincmax)
	if (dp->ewind > var.ewindhighwater + var.highwaterincmax && var.ewindhighwater > 0) {
			snprintf(buf, sizeof(buf), "event WARN Discarding eWind value of %.1f (High water mark is %.1f)", dp->ewind, var.ewindhighwater);
			journal.add(new Message(buf));
			dp->ewind = var.ewindhighwater;
	}
	else if (dp->ewind > var.ewindhighwater) {
		var.ewindhighwater = dp->ewind;
	}

	// Set meter data
 	if (meter[Emains].inUse) {	// Special case - Export is like a subset of Mains.
		dp->emains = meter[Emains].getEnergy();
		dp->eexport = meter[Eexport].getEnergy();
		if (meter[Emains].derivePower) {
			dp->wmains = meter[Emains].getPower();
			dp->wexport = meter[Eexport].getPower();
			DEBUG fprintf(stderr, "[Meter] Setting Wmains %.1f Wexport %.1f ", dp->wmains, dp->wexport);
		}
	}
	
	if (meter[Ethermal].inUse) {	// Must be before Eload as it can use Ethermal
		dp->ethermal = meter[Ethermal].getEnergy();
		if (meter[Ethermal].derivePower) {
			dp->wthermal = meter[Ethermal].getPower();
			DEBUG fprintf(stderr, "[Meter] Setting Wthermal %.1f ", dp->wthermal);
		}
	}

	// 2.8 Override Esolar and Ewind from meter data
	if (meter[Esolar].inUse) {
		dp->esolar = meter[Esolar].getEnergy();
		if (meter[Esolar].derivePower) {
			dp->wsolar = meter[Esolar].getPower();
			DEBUG fprintf(stderr, "[Meter] Setting Wsolar %.1f ", dp->wsolar);
		}
	}
	if (meter[Ewind].inUse) {
		dp->ewind = meter[Ewind].getEnergy();
		if (meter[Ewind].derivePower) {
			dp->wwind = meter[Ewind].getPower();
			DEBUG fprintf(stderr, "[Meter] Setting Wwind %.1f ", dp->wwind);
		}
	}
	
	if (meter[Eload].inUse) {
		dp->eload = meter[Eload].getEnergy();
		if (meter[Eload].derivePower) {
			dp->wload = meter[Eload].getPower();
			DEBUG fprintf(stderr, "[Meter] Setting Wload %.1f ", dp->wload);
		}
	}
	else
	{
		dp->eload = dp->esolar + dp->ewind + dp->ethermal;
		// In systems with a Victron, wload should come from the Victron
		// and not be overwritten here.
		if (conf.theVictron == -1)
			dp->wload = dp->wsolar + dp->wwind + dp->wthermal;
	}
	
	// TRUNCATION and NEGATIVE  v3.1
	// Look for excessive / unlikely Wsolar & Wload
	if (conf.capacity > 0 && dp->wload > conf.capacity) {
		snprintf(buf, sizeof(buf), "event WARN wload %.2f more than system capacity %d - truncating to %.2f", dp->wload, conf.capacity, var.prevWload);
		journal.add(new Message(buf));
		dp->wload = var.prevWload;
	}
	if (conf.capacity > 0 && dp->wsolar > conf.capacity) {
		snprintf(buf, sizeof(buf), "event WARN wsolar %.2f more than system capacity %d - truncating to %.2f", dp->wsolar, conf.capacity, var.prevWsolar);
		journal.add(new Message(buf));
		dp->wsolar = var.prevWsolar;
	}
	if (dp->wload < 0) {
		snprintf(buf, sizeof(buf), "event WARN wload %.2f negative - truncating to %.2f", dp->wload, var.prevWload);
		journal.add(new Message(buf));
		dp->wload = 0.0;
	}
	if (dp->wsolar < 0) {
		snprintf(buf, sizeof(buf), "event WARN wsolar %.2f negative - truncating to %.2f", dp->wsolar, var.prevWsolar);
		journal.add(new Message(buf));
		dp->wsolar = 0.0;
	}
	
	// Save previous values
	var.prevWload = dp->wload;
	var.prevWsolar = dp->wsolar;
	
	// reset meter data
	for (i = 0; i < MAXMETER; i++) 
	if (meter[i].inUse)
		meter[i].reset();
	// Special case
	if (meter[Emains].inUse)
		meter[Eexport].reset();
		
	// Handle multiple Steca interfaces
	if (stecas > 1) {
		// Stecas contribute only wSolar and SOC values. 
		// wSolar accumulates across all Stecas.  SOC must be averaged.
		DETAILDEBUG cerr << "Adjusting SOC due to " << stecas << " Stecas connected from " << dp->soc;
		dp->soc = dp->soc / stecas;
		DETAILDEBUG cerr << " to " << dp->soc << endl;
	}
	
	// Send to Rico displays if required		RICO
	if (conf.rico.kwdisp && conf.numFronius + conf.numSma + conf.numSoladin != 1) {
			snprintf(buf, sizeof(buf), "disp %d %f 3", conf.rico.kwdisp, dp->wload / 1000.0); // Display is in kw. Force 3 decimals.
			DEBUG cerr << "Rico (Consolidate): sending kw '" << buf;
			socks.send2contr(conf.rico.kwdev, buf);
			DEBUG cerr << "' ";
	}
	if (conf.rico.kwhdisp && conf.numFronius + conf.numSma + conf.numSoladin != 1) {
			snprintf(buf, sizeof(buf), "kwh %f -1", dp->eload); // Display is in kw. Floating decimal point
			DEBUG cerr << "Rico (Consolidate): sending '" << buf;
			socks.send2contr(conf.rico.kwhdev, buf);
			DEBUG cerr << "' ";
	}
	if (conf.rico.importdisp) {
			snprintf(buf, sizeof(buf), "disp %d %f 3", conf.rico.importdisp, dp->wmains / 1000.0); // Display is in kw. Force no decimal point
			DEBUG cerr << "Rico (Consolidate): sending '" << buf;
			socks.send2contr(conf.rico.importdev, buf);
			DEBUG cerr << "' ";
	}
	
	// Process Data Row into vars.dat
	dp->vars(ok);	
	
	// set interval and reset current line
	time_t now = ::time(0);
	dp->period = now - last;
	last = now;
	journal.add(dp);
	prev = *dp;			// save Data values in prev.
	journal.add(rdp);
	DEBUG dp->dump();
	DEBUG cerr << "LocalGeneration " << var.localGeneration;
	DEBUG fprintf(stderr, "\n");
	// Report lowest battery voltage seen if it is less than var.MinBatt
	if (var.lowestBatt < var.minBatt) {
		snprintf(buf, sizeof(buf), "event INFO Lowest Battery voltage %.2f", var.lowestBatt);
		journal.add(new Message(buf));
	}
	var.lowestBatt = var.minBatt;
	
	// Finally finally enter modbus sutff.
	
	if (conf.bModbus())
		modbus(dp->wload, dp->eload);
}

float Current::sanityCheck(const float val, const int device, const float prev, const char * name) {
	// This should check that the value provided is reasonable for the device.  Since the 
	// unit number is not provided and the config does not allow for setting capacity on a per
	// unit basis, this relies on all devices having the same capacity OR the capacity attribute
	// being set to the largest, and hoping that a misreported values isn't bigger than a small
	// inverter but below the largest.
	
	float v = val;
	
	if (conf[device].capacity > 0 && val > conf[device].capacity) {
		snprintf(buf, sizeof(buf), "event WARN inv[%d] %s Watts %.2f more than capacity %d: using previous %.2f", 
				 device, name, val, conf[device].capacity, prev);
		journal.add(new Message(buf));
		v = prev;
	}
	if (val < 0) {
		snprintf(buf, sizeof(buf), "event WARN inv[%d] %s Watts %.2f is negative using previous %.2f", 
				 device, name, val, prev);
		journal.add(new Message(buf));
		v = prev;
	}
	return v;
}

/*	VICTRON */	//////////////////////////////////////////////////////////////////////////////////////////////////////

void Victron::dump(void) const {
// Representation to stdout
	fprintf(stderr, "Victron: number %d Wmains %f Wload %f Vbatt %f Ibatt %f ILoad %f\n", device, getWmains(), getWload(), 
		getVbatt(), getIbatt(), getILoadRMS());
}

void Victron::stateChange(float vals[]) const {	// process data, change state if required
/* The only thing we are really interested in is if the mains has recovered */
	const float VMains = vals[0];
	const float VBatt = vals[4];
	STATEDEBUG cerr << "Victron::StateChange VMains = " << VMains << " VBatt = " << VBatt << endl;
	if (VBatt < var.lowestBatt) var.lowestBatt = VBatt;
	if (VBatt < var.minBatt && var.lowBatt == false) {
		snprintf(buf, sizeof(buf), "event WARN Battery Voltage %.1f dropped below threshold %.1f", VBatt, var.minBatt);
		var.lowBatt = true;
		journal.add(new Message(buf));
	}
	if (VBatt > var.minBatt && var.lowBatt == true) {
		snprintf(buf, sizeof(buf), "event INFO Battery Voltage %.1f risen above threshold %.1f", VBatt, var.minBatt);
		var.lowBatt = false;
		journal.add(new Message(buf));
	}
	
	switch (var.state) {
	case off:
		if (VMains > 180) {		// need to go into recovery mode
			journal.add(new Message("event INFO State change from Off to Recharge, VMains > 180"));
			var.state = recharge;
			victronOn();
			chargerOn();
		};
		break;
	case stecaerror:
		if (VBatt < var.minBatt) {	// Protect batteries.  Go to OFF or RECOVERY dependant on mains present.
			if (VMains < 180) {
				snprintf(buf, sizeof(buf), "event WARN State change from StecaError to OFF, VBatt = %f VMains = %f", VBatt, VMains);
				journal.add(new Message(buf));
				var.state = off;
				victronOff();
			} else {
				snprintf(buf, sizeof(buf), "event WARN State change from StecaError to Recharge, VBatt = %f VMains = %f", VBatt, VMains);
				journal.add(new Message(buf));
				var.state = recharge;
				chargerOn();
			}
		}
	}
	
	// Handle change of mains status
	switch (var.mainsstate) {
	case unknown:
		if (VMains > 180) {
			journal.add(new Message("event INFO Initial -> Mains ON"));
			var.mainsstate = mainson;
		} else {
			journal.add(new Message("event INFO Initial -> Mains OFF"));
			var.mainsstate = mainsoff;
		}
		break;
	case mainson: 
		if (VMains < 180) {
			journal.add(new Message("event INFO Mains ON -> Mains OFF Supply LOST"));
			var.mainsstate = mainsoff;
		}
		break;
	case mainsoff: 
		if (VMains > 180) {
			journal.add(new Message("event INFO Mains OFF -> Mains ON Supply restored"));
			var.mainsstate = mainson;
		}
		break;
	}
	// Handle Seven Segment display if connected by sending USE data to device controller
	if (conf.theSevenSeg != -1 && getCount()) {
		// Logically should use Victron::GetWload, but it works into the accumulated vals[] array
		// hence returns wrong values. This code is thus duplciated.  Nasty.
// 		char buf[10];
		float v;
		if (vals[1] < 0.1) // Inverter on mode
			v = vals[2] * vals[3];
		else			// Charging or passthrough mode
		{v = vals[2] * (vals[1] * conf.IMainsScale - vals[3]);}
		
		v = v > 0.0 ? v * conf[device].parallel : 0.0;
		snprintf(buf, sizeof(buf), "use %.0f", v);	
		DEBUG fprintf(stderr, "Sending '%s' to SevenSeg[%d]\n", buf, conf.theSevenSeg);
		socks.send2contr(conf.theSevenSeg, buf);
	}
}

void Victron::victronOff(void)   {socks.send2contr(conf.theVictron, "SwitchOff"); var.victronOn = false;}
void Victron::victronOn(void)    {socks.send2contr(conf.theVictron, "SwitchOn"); var.victronOn = true;}
void Victron::useBattery(void)   {socks.send2contr(conf.theVictron, "UseBattery"); var.useBattery = true;}
void Victron::passthrough(void)  {socks.send2contr(conf.theVictron, "PassThrough"); var.useBattery = false;}
void Victron::chargerOn(void)    {socks.send2contr(conf.theVictron, "ChargerOn"); var.chargerOn = true;}
void Victron::chargerOff(void)   {socks.send2contr(conf.theVictron, "ChargerOff"); var.chargerOn = false;}

float Steca::getWsolar(void)  const {
// Deal with multiple Steca in parallel. (parallel=2)
// A better way is to also deal with multiple stecas by adding their reported Solar values.
	int i = conf[device].parallel;
	DETAILDEBUG if (i != 1) cerr << "Steca::getWSolar scaling by " << i << " from " << vals[8] * vals[2] << " to " << 
		vals[8] * vals[2] * (float)i << endl;
return vals[8] > 0.0 ? vals[8] * vals[2] * (float)i : 0.0;}

void Steca::stateChange(float vals[]) const {	// process data, change state if required

// Process the batt. voltage to see if an Equalisation charge is taking place.  This isn't 
// strictly a statechange, but it's the rght place to put the logic.
	static float prevSOC = 0.0;
	const float volts = vals[2];
	if (!var.equalising && volts > var.eqVolts) {	// Enter potentially equalising state as voltage exceeds threshold
		var.equalisationStart = time(0);
		var.equalising = true;
		journal.add(new Message("event INFO Start of Equalisation"));
	}
	if (volts < var.eqVolts && time(0) > var.equalisationStart + var.eqDuration && var.equalising) {
													// Have maintained required voltage for sufficient time
		var.lastEqualisation = time(0);
		writeCMOS(NVRAM_LastEq, var.lastEqualisation);
		journal.add(new Message("event INFO Last Equalisation time set to now"));
	} // the order of these two if-statements is important
	if (volts < var.eqVolts && var.equalising) {	// leave sub-state of Equalising
		var.equalising = false;
		journal.add(new Message("event INFO End of Equalisation"));
	}
	float SOC = vals[0];
	if (SOC < INVALIDSOC and var.state != stecaerror and var.state != recharge and var.state != off) {
		// Tricky.  need != recharge to avoid flipping back and forth between stecaerror and recharge.
		// recharge is special in that we stay in there with the charger on even at impossibly low SOC
		snprintf(buf, sizeof(buf), "event WARN State change to StecaError SOC reported as %3.1f, previously %3.1f",
			SOC, prevSOC);
		journal.add(new Message(buf));
		var.state = stecaerror;
		Victron::passthrough();
		Victron::chargerOn();	// changed from chargeroff in 1.8
	}
	prevSOC = SOC;
	STATEDEBUG cerr << "Steca::stateChange SOC = " << SOC << " state " << var.stateAsStr();
	switch (var.state) {
	case initial: 
		Victron::chargerOff();		// Always initially disable charger
		if (SOC > var.inverterOff) {
			snprintf(buf, sizeof(buf), "event INFO State change from Initial to Normal due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = normal;
			Victron::useBattery();		// enable virtual switch
			Victron::chargerOff();
		} else {
			snprintf(buf, sizeof(buf), "event INFO State change from Initial to InverterOff due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = inverteroff;
			Victron::passthrough();		// disable virtual switch
			Victron::chargerOff();
		}
		break;
	case charging:
		if (SOC < var.emergencySOC) {		// mains must be off
			snprintf(buf, sizeof(buf), "event INFO State change from Charging to Off due to SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = off;
			Victron::chargerOff();		// for consistency
			Victron::victronOff();
			break;
		}

		// Old behaviour - now defined as storage
	
		if (conf.systemType == storage_t && (!var.chargeWindow || SOC > var.chargetoSOC || var.localGeneration > var.chargeLimit)) {
			// Should only trigger one of these.  Theoretically all causes should be appended.
			if (!var.chargeWindow)
				snprintf(buf, sizeof(buf), "event INFO State change from Charging to Normal as no longer within charge period");
			if (SOC > var.chargetoSOC)
				snprintf(buf, sizeof(buf), "event INFO State change from Charging to Normal due to SOC=%3.1f", SOC);
			if (var.localGeneration > var.chargeLimit)
				snprintf(buf, sizeof(buf), "event INFO State change from Charging to Normal as Local Generation %3.1f is more than Limit %d", 
					var.localGeneration, var.chargeLimit);
			
			journal.add(new Message(buf));
			var.state = normal;
			Victron::useBattery();
			Victron::chargerOff();
			break;
		}
		
		// New behaviour - system type = hybrid.
		
		if (conf.systemType == hybrid_t && !var.chargeWindow) {
			if (!var.chargeWindow)
				snprintf(buf, sizeof(buf), "event INFO State change from Charging to Normal as outside Charge period, SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state = normal;
			Victron::useBattery();
			Victron::chargerOff();
			break;
		}

		if (conf.systemType == hybrid_t && SOC > var.chargetoSOC) {
			snprintf(buf, sizeof(buf), "event INFO State change from Charging to Not Charging due to SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state = notcharging;
			Victron::chargerOff();
			break;
		}
		
/*		if (conf.systemType == hybrid_t && SOC < var.inverterOff) {
			sprintf(buf, "event INFO State change from Charging to Inverter Off due to SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state = inverteroff;
			Victron::chargerOff();
			break;
		}
*/
		break;
	case normal:
		if (SOC < var.inverterOff) {	 // Normal -> InverterOff
			snprintf(buf, sizeof(buf), "event INFO State change from Normal to InverterOff due to SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = inverteroff;
			// TODO validate that Victron IS device 0 and that it is connected
			Victron::passthrough();		// disable virtual switch
			break;
		}
		
		// Old behaviour - type = storage
		if (conf.systemType == storage_t && var.chargeWindow && SOC < var.chargetoSOC && var.localGeneration < var.chargeLimit) { // Normal -> Charging
			snprintf(buf, sizeof(buf), "event INFO State change from Normal to Charging and SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = charging;
			Victron::passthrough();
			Victron::chargerOn();
		}
		// New behaviour - type = hybrid
		if (conf.systemType == hybrid_t && var.chargeWindow) { // Normal -> Charging
			snprintf(buf, sizeof(buf), "event INFO State change from Normal to Charging as within Charge period SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = charging;
			Victron::passthrough();
			Victron::chargerOn();
		}
		break;
	case inverteroff:
		if (SOC > var.inverterOn) {	// turn on inverter - out of passthrough mode
			snprintf(buf, sizeof(buf), "event INFO State change from InverterOff to Normal due to SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = normal;
			// TODO validation
			Victron::useBattery();		// enable virtual switch
			break;
		}
		
		// OLD behaviour - type = Storage
		if (conf.systemType == storage_t && var.chargeWindow && SOC < var.chargetoSOC && var.localGeneration < var.chargeLimit) { // to charge mode
			snprintf(buf, sizeof(buf), "event INFO State change from InverterOff to Charging due to SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = charging;
			Victron::chargerOn();
			break;
		}
		
		// New behavour - type = Hybrid
		if (conf.systemType == hybrid_t && var.chargeWindow) { // to charge mode
			snprintf(buf, sizeof(buf), "event INFO State change from InverterOff to Charging as within Charge period, with SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state = charging;
			Victron::chargerOn();
			break;
		}
		if (SOC <= var.emergencySOC) {	// Recovery
			snprintf(buf, sizeof(buf), "event INFO State change from InverterOff to Recovery due to SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = recovery;
			Victron::chargerOn();
		}
		break;
	case recovery:
		if (SOC > var.recoverySOC && SOC >= var.inverterOn) {		// to Normal
			snprintf(buf, sizeof(buf), "event INFO State change from Recovery to Normal due to SOC=%2.1f", SOC);
			journal.add(new Message(buf));
			var.state = normal;
			Victron::chargerOff();
			Victron::useBattery();
			break;
		}
		if (SOC > var.recoverySOC && SOC < var.inverterOn) {		// to InverterOff
			snprintf(buf, sizeof(buf), "event INFO State change from Recovery to InverterOff due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = inverteroff;
			Victron::chargerOff();
			Victron::passthrough();
			break;
		}
		if (SOC < var.emergencySOC) {		// Recovery -> Off as it is not charging
			snprintf(buf, sizeof(buf), "event INFO State change from Recovery to Off due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = off;
			Victron::victronOff();
		}
		break;
	case off:
		if (SOC > var.inverterOff) {
			snprintf(buf, sizeof(buf), "event INFO State change from Off to InverterOff due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = inverteroff;
			Victron::victronOn();
		}
		break;
	case stecaerror:
		if (SOC > INVALIDSOC) {
			snprintf(buf, sizeof(buf), "event INFO State change from StecaError to Recovery due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = recovery; // 1.8 changed from inverteroff to Recovery
		}
		break;
	case recharge:
		if (SOC > var.emergencySOC) {
			snprintf(buf, sizeof(buf), "event INFO State change from Recharge to Recovery due to SOC=%3.1f", SOC);
			journal.add(new Message(buf));
			var.state = recovery;
		}
		break;
	case notcharging:		// This state only exists for Hybrid systems.
		if (SOC < var.inverterOff) {
			snprintf(buf, sizeof(buf), "event INFO State change from NotCharging to InverterOff due to SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state = inverteroff;
			Victron::chargerOff();
			Victron::passthrough();
			break;
		}
		if (!var.chargeWindow) {
			snprintf(buf, sizeof(buf), "event INFO State change from NotCharging to Normal as outside Charge period with SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state = normal;
			Victron::chargerOff();
			Victron::useBattery();
			break;
		}
		// 1.37 - Go back to charging is SOC < Maintain
		if (SOC < var.maintain) {
			snprintf(buf, sizeof(buf), "event INFO State change from NotCharging to Charging as SOC=%.1f", SOC);
			journal.add(new Message(buf));
			var.state=charging;
			Victron::chargerOn();
			break;
		}
		break;
	}
			
		STATEDEBUG cerr << endl;
	// Process data for SevenSegment display if it is present
	if (conf.theSevenSeg != -1 && getCount()) {
// 		char buf[100];
		snprintf(buf, sizeof(buf), "gen %.0f", getWsolar() / getCount() / getCount()); //Remeber to divide by getcount squared as
			// wsolar is a product of two quantities.
		DEBUG fprintf(stderr, "Steca::StateChange Sending '%s' to SevenSeg[%d]\n", 
			buf, conf.theSevenSeg);
		socks.send2contr(conf.theSevenSeg, buf);
	}
}

float Victron::getWmains() const {
	DETAILDEBUG cerr << "Victron::GetWMains: ImainsScale " << conf.IMainsScale << " Parallel = " << conf[device].parallel <<
		" " << vals[0] * vals[1] * conf.IMainsScale * conf[device].parallel << " watts\n";
	return vals[0] * vals[1] * conf.IMainsScale * conf[device].parallel;}

float Victron::getWload()  const {
	float v;
	if (vals[1] < 0.1) // Inverter on mode
		v = vals[2] * vals[3];
	else			// Charging or passthrough mode
		{v = vals[2] * (vals[1] * conf.IMainsScale - vals[3]);}
	DETAILDEBUG cerr << "Victron::GetWLoad IMainsScale = " << conf.IMainsScale << " Parallel = " << conf[device].parallel << 
		" "  << (v > 0.0 ? v * conf[device].parallel : 0.0) << "watts\n";
	return v > 0.0 ? v * conf[device].parallel : 0.0;
}

void Steca::dump(void) const {
// Representation to stdout
	fprintf(stderr, "Steca: number %d WSolar %f SOC %f\n", device, getWsolar(), getSOC());
}

void Turbine::dump(void) const {
// Representation to stdout
	fprintf(stderr, "Turbine: number %d \n", device);
}

void Fronius::dump(void) const {
	cerr << "Fronius: number " << device << endl;
}

void Davis::dump(void) const {
	cerr << "Davis Vantage: Number " << device << endl;
}

void Pulse::dump(void) const {
	cerr << "PulseMeters: number " << device << endl;
}

/* DELETEME void Elster::dump(void) const {
	cerr << "Elster: number " << device << endl;
}
*/
void Rico::dump(void) const {
	cerr << "Rico: number " << device << endl;
}
void Generic::dump(void) const {
	cerr << "Generic: number " << device << endl;
}
void Owl::dump(void) const {
	cerr << "Owl: number " << device << " " << vals[0] << " " << vals[1] << " " << vals[2] << endl;
}
void Soladin::dump(void) const {
	cerr << "Soladin: number " << device << " Vdc " << vals[0] << " Idc " << vals[1] << " Hz " << vals[2] << endl;
}
void SevenSeg::dump(void) const {
	cerr << "SevenSeg: number " << device << endl;
}

void Meter::dump(void) const {
	cerr << "Meter: number " << device << endl;
}

void Resol::dump(void) const {
	cerr << "Resol: number " << device << " Names: ";
	const char ** ptr = names;
	if (!ptr) fprintf(stderr, "--none--");
	else
		for (; *ptr; ptr++) fprintf(stderr, "'%s' ", *ptr);
	fprintf(stderr, "\n");
}

void Sma::dump(void) const {
	cerr << "Sma: number " << device << endl;
}

void Data::dump(void) const {
// Representation to stderr
	fprintf(stderr, "Data row: period %d  wload %.2f wmains %.2f wsolar %.2f wwind %.2f wthermal %.2f "
		"wexport %.2f eload %.2f emains %.2f esolar %.2f ewind %.2f ethermal %.2f eexport %.2f "
	"vbatt %.2f ibatt %.2f soc %.1f inpulses %d outpulses %d irradiation %.2f ambient %.2f paneltemp %.2f ", 
	period,  wload, wmains, wsolar, wwind, wthermal, wexport, eload, emains, esolar, ewind, ethermal, 
	eexport, vbatt, ibatt, soc, pulsein, pulseout, irradiation, ambient, paneltemp);
}

void Inverter::dump(void) const {
	cerr << "Inverter: number " << device << " Names: ";
	const char ** ptr = names;
	if (!ptr) fprintf(stderr, "--none--");
	else
	for (; *ptr; ptr++) fprintf(stderr, "'%s' ", *ptr);
	fprintf(stderr, "\n");
}

void Thermal::dump(void) const {
	cerr << "Thermal: number " << device << " Names: ";
	const char ** ptr = names;
	if (!ptr) fprintf(stderr, "--none--");
	else
	for (; *ptr; ptr++) fprintf(stderr, "'%s' ", *ptr);
	fprintf(stderr, "\n");
}

void Sensor::dump(void) const {
	cerr << "Sensor: number " << device << " Names: ";
	const char ** ptr = names;
	if (!ptr) fprintf(stderr, "--none--");
	else
		for (; *ptr; ptr++) fprintf(stderr, "'%s' ", *ptr);
	fprintf(stderr, "\n");
}
/*	JOURNAL */	//////////////////////////////////////////////////////////////////////////////////////////////////////

void Journal::xml(FILE * fp) const {
// Representation to open file stream
	int written = 0; 
	DEBUG fprintf(stderr,"Dumping journal (%zu entries total), ", journal.size());
	journal_type::const_iterator it = journal.begin();
	if (journal.size() == 0) return;
	fprintf(fp, "<?xml version=\"1.0\"?>\n<upload xmlns=\"http://www.naturalwatt.com\"\n");
	fprintf(fp, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
	fprintf(fp, "  xsi:schemaLocation=\"http://www.naturalwatt.com data.xsd\">\n\n");
	fprintf(fp, "  <header site=\"%d\" time=", conf.getSiteId());
	Timestamp now;
	now.xml(fp);
	fprintf(fp, "/>\n");

// Skip entries earlier than LastUpload
// MCP 3.5 changed comparison: from < to <= to try and avoid duplicates
	
	while (it != journal.end() && (*it)->getTimeval() <= var.lastUpload) ++it;
	
	while(it != journal.end()) {
		(*it)->xml(fp);
		++it;  written++;
	}
	fprintf(fp, "</upload>\n");
	DEBUG fprintf(stderr, "%d entries written\n", written);
}


void Journal::xml(const char * name) const {
// open and write a file
	FILE * fp;
	if ((fp = fopen(name, "w"))) 
		xml(fp);
	else		// should throw an exception here
	{
		cerr << "Journal::xml cannot open " << name << " for output\n";
		perror(NULL);
		return;
	}
	fclose(fp);
}

void Journal::backup(const char * name) const
{
	// Write out all of the journal
	int prev = var.lastUpload;
	var.lastUpload = 0;
	xml(name);
	var.lastUpload = prev;
}

void Journal::restore(const char * name)
{
	FILE * fp;
	if (!(fp = fopen(name, "r"))) {
		cerr << "Journal::restore cannot open " << name << " for input\n";
		perror(NULL);
		return;
	}
	try {
		size_t len;
		char buf[BUFSIZE];
		bool done = false;
		while (!done) {	// loop until entire file has been processed.
			len = fread(buf, 1, BUFSIZE, fp);
			done = len < BUFSIZE;
			if (!XML_Parse(buf, len, done)) {
				fprintf(stderr,	"%s at line %d\n",
						XML_ErrorString(XML_GetErrorCode()),
						XML_GetCurrentLineNumber());
				fclose(fp);
				return;	// error 2 - XML problem
			}
		}
	}  catch (char const * msg) {
		snprintf(buf, sizeof(buf), "event ERROR restoring from %s : %s", name, msg);
		add(new Message(buf));
	}
	fclose(fp);
}

void Journal::purge(const time_t tim) {	// clear out entries older than this. The time value is in seconds relative to now.
	time_t t = ::time(0) - tim;
	Timestamp ts(t);
	DEBUG cerr << "Journal::purge to " << ts.getTime();
	// ensure we don't purge to date in the future, or data that has not been sent.
	if (t > ::time(0)) {
		cerr << "*** Error purgeto Time is in the future\n";
		t = ::time(0);
	}
	
	if (t > var.lastUpload && var.lastUpload) {	// First time around, var.lastUpload is zero.
		cerr << "*** Warning purgeto later than last upload\n";
		t = var.lastUpload;
	}
	int rows = 0;
	journal_type::iterator jit = journal.begin();

	while (jit != journal.end()) {
		if ((*jit)->getTimeval() < t) {
			DETAILDEBUG cerr << "Deleting entry at " << (*jit)->getTime() << endl;
			delete *jit;
			journal.pop_front();
			jit = journal.begin();
			rows ++;
		}
		else
			break;

	}
	DEBUG cerr << " Cleared " << rows << " rows .. done\n";
}

void Journal::add(Entry *e) {	// Add a row
// But if it's a Message, and the Console is connected, send it there as well
	MEMDEBUG cerr << "Journal::add " << e << endl;
	journal.push_back(e); 
	Message * mp;
	if ((mp = dynamic_cast<Message *>(e)) && consock.fd) {
		send2fd(consock.fd, mp->getmsg());
	}
	// 3.0 Count errors, warning and info messages
	if (mp = dynamic_cast<Message *>(e)) {
		const char * msg = mp->getmsg();
		if (strstr(msg, " ERROR ")) error++;
		else if (strstr(msg, " WARN ")) warn++;
		else if (strstr(msg, " INFO ")) info++;
		else unknown++;
	}
}

void Journal::add_ordered(Entry *e)
// When restoring the journal, need to explcitly set the time of the Event 
// which is otherwise default contstructed to now.
// Because there are already a couple of entries in the journal at the time the restore
// takes place, entries need to be be in the right place, which is  near the end.
// Hence the use of reverse iterator.
// Since this is a rare operation it doesn't make sense to use a map instead of a list

{
	journal_type::reverse_iterator rit;
	
	for(rit = journal.rbegin(); rit != journal.rend() && *e < **rit; rit++);
	
	// Don't do what add() does which is log to console.
	// Don't add 'duplicates' for a failry loose definition of duplicates.
	// Entries compare as == if the timestamp is the same and the object type is the same.
	if (! ((**rit) == *e))
		journal.insert(rit.base(), e);
}

// Journal XML parsing
void Journal::startElement(const XML_Char *name, const XML_Char **atts)
{
	// Reset chardata;
	chardata[0] = 0;
	// Save attribute time
	if (strcmp(name, "entry") == 0) {
		while (*atts) {
			if (strcmp(atts[0], "time") == 0)
				time = getDateTime(atts[1]);
			atts += 2;
		}
	}
	else
		// Relying on getting device attribute BEFORE the unit attribute.
		while (*atts) {
			if (strcmp(atts[0], "device") == 0) {
				device = getInt(atts[1]);
				unit = 0;
			}
			if (strcmp(atts[0], "unit") == 0)
				unit = getInt(atts[1]);
		}
}

void Journal::endElement(const XML_Char* name)
{
	if (strcmp(name, "entry") == 0) {
		add_ordered(pending_entry);
		// Set pending_entry to null for debugging
		pending_entry = NULL;
		return;
	}
	
	// Attampt to add it to the map
	try {
		float val = getFloat(chardata, 0);
		vals[name] = val;
	}
	catch (const char *) {
	}
	
	// MESSAGE
	if (strcmp(name, "message") == 0) {
		// Should really check pending_entry is null;
		DEBUG cerr << "New Message. Chardata length " << strlen(chardata) << endl;
		pending_entry = new Message(chardata);
	}
	// STATUS
	if (strcmp(name, "status") == 0)
		pending_entry = new Status(vals["load"], 
								   vals["diskused"], 
								   vals["diskfree"], 
								   vals["memused"], 
								   vals["memfree"]);
	
	// DATA
 	if (strcmp(name, "data") == 0) {
		Data * dp = new Data();
	// This relies on map behaviour of returning 0 (0.0) for un-initialised value.
		dp->period =	  static_cast<int>(vals["period"]);
		dp->wmains =	  vals["wmains"];
		dp->wload = 	  vals["wload"];
		dp->wsolar =	  vals["wsolar"];
		dp->wexport = 	  vals["wexport"];
		dp->wwind = 	  vals["wwind"];
		dp->wthermal = 	  vals["wthermal"];
		dp->vbatt = 	  vals["vbatt"];
		dp->ibatt = 	  vals["ibatt"];
		dp->soc = 		  vals["soc"];
		dp->vdc =		  vals["vdc"];
		dp->emains =	  vals["emains"];
		dp->eload =		  vals["eload"];
		dp->esolar =	  vals["esolar"];
		dp->eexport =	  vals["eexport"];
		dp->ewind =		  vals["ewind"];
		dp->ethermal	= vals["ethermal"];
		dp->irradiation = vals["irradiation"];
		dp->paneltemp =   vals["paneltemp"];
		dp->ambient =	  vals["ambient"];
		dp->pulsein =	  static_cast<int>(vals["pulsein"]);
		dp->pulseout = 	  static_cast<int>(vals["pulseout"]);
		pending_entry = dp;
	}
	
	/*/ INVERTER
	if (strcmp(name, "inverter") == 0) {
		pending_entry = new RawData();
		stringstream ss;
		ss << "inverter watts:" << vals["watts"];
		pending_entry->parse(ss.str());
	}
	/*
									 vals["wdc"],
									 vals["wdc2"],
									 vals["kwh"],
									 vals["iac"],
									 vals["iac2"],
									 vals["iac3"],
									 vals["vac"],
									 vals["vac2"],
									 vals["vac3"],
									 vals["hz"],
									 vals["hz2"],
									 vals["hz3"],
									 vals["idc"],
									 vals["idc2"],
									 vals["vdc"],
									 vals["vdc2"],
									 vals["ophr"],
									 vals["t1"],
									 vals["t2"],
									 vals["t3"],
									 vals["t4"],
									 vals["fan1"],
									 vals["fan2"],
									 vals["fan3"],
									 vals["fan4",
	 */
	
	

	
}

void Journal::charData(const XML_Char *s, int len)
// Append char data
{
	int prevlen = strlen(chardata);
	if (len + prevlen > sizeof(chardata)) {
		cerr << "Trying to overflow Journal::chardata by adding " << len << " characters '" << s << "'\n";
		return;
	}
	if (len > 0) {
		strncat(chardata, s, len);
	//	chardata[len + prevlen] = 0;
	}
		
}

void Fronius::stateChange(float vals[]) const {
	// Handle Rico Display
	// If there is exactly one Fronius and no SMA we can update the Rico displays here, for nearly 
	// instant updates.  Otherwise it has to be done in Consolidate when all the data is on
	if (conf.numFronius == 1 && conf.numSma == 0 && conf.numSoladin == 0) {
		if (conf.rico.kwdisp) {
			// 		char buf[30];
			snprintf(buf, sizeof(buf), "disp %d %f", conf.rico.kwdisp, vals[0] / 1000.0); // Display is in kw
			DEBUG cerr << "Rico (Fronius): sending kw '" << buf;
			socks.send2contr(conf.rico.kwdev, buf);
		}
		if (conf.rico.kwhdisp) {
			// WARNING The kwh command calculates the CO2 figure using a fixed rate and it ASSUMES
			// that kwh is display 5 nd CO2 is display 8.
			// If that is not true we have to use the disp command but also calculate and send
			// the CO2 figure.
			snprintf(buf, sizeof(buf), "kwh %f", vals[1] / 1000.0);
			DEBUG cerr << "Rico (Fronius): sending '" << buf << endl;
			socks.send2contr(conf.rico.kwhdev, buf);
			DEBUG cerr << "' ";
		}
	}
}

// Place it here instead of journal.h to avoid declaration order problems.
bool Fronius::hasWload()  const   {return conf.bInverterLoad();}
bool Soladin::hasWload()  const   {return conf.bInverterLoad();}
bool Soladin::hasWsolar() const   {return conf[device].model == solar_model;}
bool Soladin::hasEsolar() const   {return conf[device].model == solar_model;}
bool Soladin::hasWwind()  const   {return ! (conf[device].model == solar_model);}
bool Soladin::hasEwind()  const   {return ! (conf[device].model == solar_model);}
void Soladin::stateChange(float vals[]) const {
	// Handle Rico Display
	// If there is exactly one Fronius and no SMA we can update the Rico displays here, for nearly 
	// instant updates.  Otherwise it has to be done in Consolidate when all the data is on
	if (conf.numFronius == 0 && conf.numSma == 0 && conf.numSoladin == 1) {
		if (conf.rico.kwdisp) {
			// 		char buf[30];
			snprintf(buf, sizeof(buf), "disp %d %f", conf.rico.kwdisp, vals[0] / 1000.0); // Display is in kw
			DEBUG cerr << "Rico (Soladin): sending kw '" << buf;
			socks.send2contr(conf.rico.kwdev, buf);
		}
		if (conf.rico.kwhdisp) {
			// WARNING The kwh command calculates the CO2 figure using a fixed rate and it ASSUMES
			// that kwh is display 5 and CO2 is display 8.
			// If that is not true we have to use the disp command but also calculate and send
			// the CO2 figure.
			snprintf(buf, sizeof(buf), "kwh %f", vals[1] / 1000.0);
			DEBUG cerr << "Rico (Soladin): sending '" << buf << endl;
			socks.send2contr(conf.rico.kwhdev, buf);
			DEBUG cerr << "' ";
		}
	}
}

bool Sma::hasWload()      const   {return conf.bInverterLoad();}
bool Sma::hasWsolar()     const   {return conf[device].model == solar_model;}
bool Sma::hasWwind()      const   {return ! (conf[device].model == solar_model);}
bool Sma::hasEsolar()     const   {return conf[device].model == solar_model;}
bool Sma::hasEwind()      const   {return ! (conf[device].model == solar_model);}

void Sma::stateChange(float vals[]) const {
	// Handle Rico Display
	// If there is exactly one SMA and no Fronius we can update the Rico displays here, for nearly 
	// instant updates.  Otherwise it has to be done in Consolidate when all the data is on
	if (conf.numSma == 1 && conf.numFronius == 0 && conf.numSoladin == 0) {
		if (conf.rico.kwdisp) {
			// 		char buf[30];
			snprintf(buf, sizeof(buf), "disp %d %f", conf.rico.kwdisp, vals[0] / 1000.0); // Display is in kw
			DEBUG cerr << "Rico (SMA): sending kw '" << buf << " ";
			socks.send2contr(conf.rico.kwdev, buf);
		}
		if (conf.rico.kwhdisp) {
			// WARNING The kwh command calculates the CO2 figure using a fixed rate and it ASSUMES
			// that kwh is display 5 nd CO2 is display 8.
			// If that is not true we have to use the disp command but also calculate and send
			// the CO2 figure.
			snprintf(buf, sizeof(buf), "kwh %f", vals[1]);		// Display in kwh, as reported.
			DEBUG cerr << "Rico (SMA): sending '" << buf << "'\n";
			socks.send2contr(conf.rico.kwhdev, buf);
		}
	}
}

bool Inverter::hasWload()      const   {return conf.bInverterLoad();}
bool Inverter::hasWsolar()     const   {return conf[device].model == solar_model;}
bool Inverter::hasWwind()      const   {return ! (conf[device].model == solar_model);}
bool Inverter::hasEsolar()     const   {return conf[device].model == solar_model;}
bool Inverter::hasEwind()      const   {return ! (conf[device].model == solar_model);}
void Inverter::stateChange(float vals[]) const {
	// Handle Rico Display
	// If there is exactly one SMA and no Fronius we can update the Rico displays here, for nearly 
	// instant updates.  Otherwise it has to be done in Consolidate when all the data is on
	if (conf.numSma == 1 && conf.numFronius == 0 && conf.numSoladin == 0) {
		if (conf.rico.kwdisp) {
			// 		char buf[30];
			snprintf(buf, sizeof(buf), "disp %d %f", conf.rico.kwdisp, vals[0] / 1000.0); // Display is in kw
			DEBUG cerr << "Rico (Inverter): sending kw '" << buf << " ";
			socks.send2contr(conf.rico.kwdev, buf);
		}
		if (conf.rico.kwhdisp) {
			// WARNING The kwh command calculates the CO2 figure using a fixed rate and it ASSUMES
			// that kwh is display 5 nd CO2 is display 8.
			// If that is not true we have to use the disp command but also calculate and send
			// the CO2 figure.
			snprintf(buf, sizeof(buf), "kwh %f", vals[1]);		// Display in kwh, as reported.
			DEBUG cerr << "Rico (Inverter): sending '" << buf << "'\n";
			socks.send2contr(conf.rico.kwhdev, buf);
		}
	}
}

void CurrentData::parse(const char * data) throw (const char *) {
	char *ptr2, *next;
	char *ptr, *stringcopy;
	float val;

	stringcopy = ptr = strdup(data);
	MEMDEBUG fprintf(stderr, "Pointer is 0x%x ", reinterpret_cast<unsigned int>(ptr));
	
	// Format is type name:val name:val where type is a word like thermal or inverter.
	while (* ptr && *ptr != ' ') ptr++;	// Skip first word
	if (*ptr == 0) throw "No data after first word";
	DETAILDEBUG fprintf(stderr, "Currentdata::parse '%s'", data);
	
	try {
		while (*ptr) {
			while (*ptr && *ptr == ' ') ptr++;	// Skip whitespace
			if (*ptr == 0) break;			// End of string after whitespace. Not an error
			ptr2 = strchr(ptr, ':');
			if (ptr2 == 0) throw "No item:value pair found";
			*ptr2 = 0;
			if (!*(ptr2+1)) throw "No value in item:value";
			val = strtof((ptr2+1), &next);
			DETAILDEBUG fprintf(stderr, "Got name '%s' val %.2f ", ptr, val);
			name(ptr) += val;
			ptr = next;
		}
		count++;	// when the entire row has been read;
	}
	catch (const char * e)
	{
		if (stringcopy)
			free (stringcopy);
		else
			fprintf(stderr, "ERROR pointer is already NULL(1)\n");
		stringcopy = NULL;
		throw;
	}
	
	if (stringcopy)
		free (stringcopy);
	else
		fprintf(stderr, "ERROR pointer is already NULL(2)\n");
}

void CurrentData::davis(const char * data) throw (const char *) {
	// This is counted from the very beginning of the 'davis realtime ... ' string.
	// So these offset are 15 more than in DavisRealtime:xml
	float temp = float(makeshort(data[44 + 15], data[45 + 15]));
	DAVISDEBUG fprintf(stderr, "Irradiation (44/45 ) %02x %02x %f ", data[44 + 15], data[45 + 15], temp);
	if (temp == 32767) vals[index("irradiation")] += 0;
		else vals[index("irradiation")] += temp;	// Irradiation not fitted.
	vals[index("paneltemp")] += -273.0;
	float ambient = float(makeshort(data[12 + 15], data[13 + 15]));
	DAVISDEBUG fprintf(stderr, "Outdoor (12/13) %02x %02x %f Farh ", data[12 + 15], data[13 + 15], ambient / 10.0);
	if (ambient != 32767.0)
		ambient = (ambient - 320.0) * 5.0 / 90.0;	// Fahrenheit -> Centigrade (and divide by 10)
	else
		ambient = -273.0;	// Sensor not reporting or not fitted
	vals[index("ambient")] += ambient;
	vals[index("extra1")] += 0.0;
	count++;
	DEBUG fprintf(stderr,"CurrentData::Davis setting (count %d) %f %f %f %f\n", (count-1), temp, -273.0, ambient, vals[3]);
}

void Current::addDevice()  {		// This is for the add command
	conf.addDevice(); 
	setDevice(numdevices++, 0, generic_t);
}

float & CurrentData::name(const char * n) throw (const char *) {
	const char ** ptr;
	int i;
	if (names == 0) throw "No names defined";	// No names defined for this object
	for (ptr = names, i = 0; *ptr; ptr++, i++)
		if (strcmp(*ptr, n) == 0) return vals[i];
	throw n /* "item not recognised" */;		// Name not found
}

int name(const char * n, const char ** names) throw (const char *) {
	const char ** ptr;
	int i;
	if (names == 0) throw "No names defined";	// No names defined for this object
	for (ptr = names, i = 0; *ptr; ptr++, i++)
		if (strcmp(*ptr, n) == 0) return i;
	throw n /* "item not recognised" */;		// Name not found
}

int CurrentData::index(const char * n) const throw (const char *) {
	const char ** ptr;
	int i;
	if (names == 0) throw "No names defined";	// No names defined for this object
	for (ptr = names, i = 0; *ptr; ptr++, i++)
		if (strcmp(*ptr, n) == 0) return i;
	throw n /* "item not recognised" */;		// Name not found
}

void Data::vars(const bool ok) const
{
	// Initialise vars.dat
	FILE * f;
	if (!(f = fopen(VARSDAT, "w"))) 
		cerr << "Consolidate: Failed to open " VARSDAT << "\n";
	else {
		struct tm * tmptr;
		time_t now = time(NULL);
		tmptr = localtime(&now);
		strftime(buf, 60, "date='%A %e %B %Y'\n", tmptr);		fputs(buf, f);
		strftime(buf, 60, "time='%T'\n", tmptr);				fputs(buf, f);
		fprintf(f, "wmains=%.1f\n",  wmains);
		fprintf(f, "wload=%.1f\n",   wload);
		fprintf(f, "wsolar=%.1f\n",  wsolar);
		fprintf(f, "vbatt=%.2f\n",   vbatt);
		fprintf(f, "ibatt=%.2f\n",   ibatt);
		fprintf(f, "soc=%.1f\n",     soc);
		fprintf(f, "vdc=%.1f\n",     vdc);
		fprintf(f, "esolar=%.1f\n",  esolar);
		fprintf(f, "eload=%.1f\n",   eload);
		// Write out status 
		fprintf(f, "status='%s'\n", ok ? "Ok" : "No Data");
		fclose(f);
	}
}
