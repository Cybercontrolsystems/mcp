/*
 *  meter.c
 *  Mcp
 *
 *  Created by Martin Robinson on 23/11/2007.
 *  Copyright 2007 - 2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 - 2012 Wattsure Ltd. All rights reserved.
 *
 */

#include <stdio.h>
#include "mcp.h"
#include "meter.h"
#include "journal.h"
#include "config.h"

extern MeterData meter[];
extern Journal journal;
extern Config conf;
extern char * meterStr[];

//Class Members
void MeterData::setEnergy(float val) 
	{DETAILDEBUG fprintf(stderr, "Meter::setEnergy %f count %d ", val, count+1); 
	nowVal = val; nowTime = time(NULL); count++;}
	
float MeterData::getEnergy()	{DETAILDEBUG fprintf(stderr, "Meter::GetEnergy %f ", nowVal); return nowVal;}

float MeterData::getPower()		{
	float f;
	if (nowVal < prevVal) {
		char buf[100];
		snprintf(buf, sizeof(buf), "event WARN Current meter reading %.2f less than previous value %.2f",
				 nowVal, prevVal);
		journal.add(new Message(buf));
	}
				 
	if (prevTime > 0 && nowTime > prevTime) {
		f = (nowVal - prevVal)  * 3600.0 * 1000.0 / (float) (nowTime - prevTime);
		DEBUG fprintf(stderr, "Meter::getPower %f interval %ld (%.3f - %.3f) ", f, nowTime - prevTime, nowVal, prevVal);
		return f;
	}
	DEBUG fprintf(stderr, "Meter::getPower 0\n");
	return 0.0;
}

void MeterData::reset() {	// reset counter.
	DETAILDEBUG fprintf(stderr, "Meter::reset was %d ", count);
	if (count > 0) {
		prevTime = nowTime;
		prevVal = nowVal;
		count = 0;
	}
}	

void MeterData::dump(FILE * f) {
	 fprintf(f, "Now %zu %f Prev %zu %f Count %d Inuse %d Derive %d\n", nowTime, nowVal, prevTime, prevVal, count, inUse, derivePower);
}

void MeterData::text(int i) {
	char buf[120];
	snprintf(buf, sizeof(buf), "event INFO Meter %10s Now %zu %f Prev %zu %f Count %d Inuse %d Derive %d\n", meterStr[i], 
			 nowTime, nowVal, prevTime, prevVal, count, inUse, derivePower);
	DEBUG fprintf(stderr, "METER '%s'\n", buf);
	journal.add(new Message(buf));
}

/* ***************/
/* HANDLE_METER */
/*****************/

// Modified 11/Oct/2008 to accept either 1 or 2 meter vals.
// 3.2 2012/02/02 to handle GenImp and GenExp
/* GenImp: v[0] = Generation (ESolar) v[1] - EImport
   GenExp: v[0] - Generation (Esolar) v[1] = EExport
 */
void handle_meter(const int contr, const char * msg) {
	int read, expected;
	enum MeterType meternum = conf[contr].meterType;
	float v[2];
	if (meternum == noMeter) {
		fprintf(stderr,"Handle_meter for device %d with no meter defined\n", contr);
		return;
	}
	read = sscanf(msg, "%*s %d %f %f", &expected, &v[0], &v[1]);
	if (read != expected + 1) {
		char buf[100];
		snprintf(buf, sizeof(buf), "event ERROR Meter read %d values expected %d in '%s'\n", read, expected, msg);
		journal.add(new Message(buf));
		return;
	}
	if (expected == 1) v[1] = 0.0;
	DEBUG fprintf(stderr, "Handle_Meter %d (%s) adding contr: %d v: %.2f %.2f ", meternum, meterStr[meternum],  contr, v[0], v[1]);
	journal.add(new MeterEntry(v[0], v[1], contr));		
		
	// Primitive meter handling. Only use first value; assume it is always EMAINS
	// If it's emains, use second value as Eexport
	if (meter[meternum].inUse) {
		meter[meternum].setEnergy(v[0]);
		DEBUG fprintf(stderr, "Handle_meter %d (%d = %s) using %.2f ", contr, meternum, meterStr[meternum], v[0]); 
		switch(meternum) {
			case Emains:
				meter[Eexport].setEnergy(v[1]);
				DEBUG fprintf(stderr, "In EMains %.2f Setting Eexport to %.2f ", v[0], v[1]);
				break;
			case GenImp:
				DEBUG fprintf(stderr, "GenImp: set ELoad %.2f Emains %.2f ", v[0], v[1]);
				meter[Eload].setEnergy(v[0]);
				meter[Emains].setEnergy(v[1]);
				break;
			case GenExp:
				DEBUG fprintf(stderr, "GenExp: set Eload %.2f EExport %.2f ", v[0], v[1]);
				meter[Eload].setEnergy(v[0]);
				meter[Eexport].setEnergy(v[1]);
				break;
		}
	}
	Meter::staticmodbus(contr, v);
	DEBUG fprintf(stderr,"\n");
}
