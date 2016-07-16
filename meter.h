/*
 *  meter.h
 *  Mcp
 *
 *  Created by Martin Robinson on 23/11/2007.
 *  Copyright 2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010,2011 Wattsure Ltd. All rights reserved.
 *
 */

#include <time.h>

class MeterData {
public:
	MeterData() : prevTime(0), count(0), inUse(false) {};
	bool inUse, derivePower;
	void setEnergy(float val);
	float getEnergy();
	float getPower();
	void reset();		// reset counters. Call after getting power.
	void dump(FILE *f);
	void text(int i);
private:	
	float nowVal;
	time_t nowTime;
	int	count;
	float prevVal;
	time_t prevTime;
};


