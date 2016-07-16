/*
 *  davis.cpp
 *  Mcp
 *
 *  Created by Martin Robinson on 17/08/2007.
 *  Copyright 2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2011 Wattsure Ltd. All rights reserved.
 *
 */
 
// $Id: davis.cpp,v 3.5 2013/04/08 20:20:55 martin Exp $

#include "mcp.h"	/* globals */
#include "meter.h"
#include "davis.h"
#include "journal.h"
#include "config.h"

#define makeshort(lsb, msb)  ( lsb | (msb << 8))
#define makelong(lsb, b2, b3, msb) (lsb | (b2 << 8) | (b3 << 16) | (msb << 24))

//Locals
const char * barotrend(unsigned char c);
char * date2xml(int x);
char * mins2hhmm(int x);
// char * graphbytes(const unsigned char * data, int current, int size, int offset);
// char * graphtimes(const unsigned char * data, int current, int size);
// char * graphfloat(const unsigned char * data, int current, int size, float scale);
// char * graphshort(const unsigned char * data, int current, int size);
void dumphex(const char *msg, const int count);

// Externals
extern Journal journal;	
extern Config conf;
extern pthread_mutex_t mutexModbus;

/****************/
/* HANDLE_DAVIS */
/****************/
void handle_davis(const char *msg) {
// A Davis message can start with a null-terminated string but also followed by raw data 
	DEBUG cerr << "Handle_davis: " << msg << endl;
	if (strncmp(msg, "davis realtime", 14) == 0) {
		DavisRealtime * d;
		DAVISDEBUG dumphex(msg + 15, DREALTIMELEN);
		journal.add(d = new DavisRealtime((unsigned char *)msg + 15));
		DAVISDEBUG fprintf(stderr, "Conf.bModbus() = %d ", conf.bModbus());
		if (conf.bModbus())
			d->modbus();		// DavisRealtime::modbus()
		DAVISDEBUG d->xml(stderr);
	}	else if (strncmp(msg, "davis hilow", 11) ==0) {
		DavisHilow *d;
		DAVISDEBUG dumphex(msg + 12, DHILOWLEN);
		journal.add(d = new DavisHilow((unsigned char *)msg + 12));
		DAVISDEBUG d->xml(stderr);
	}	else if (strncmp(msg, "davis graph", 11) ==0) {
		DavisGraph *d;
		DAVISDEBUG dumphex(msg + 12, DGRAPHLEN);
		journal.add(d = new DavisGraph((unsigned char *)msg + 12));
		DAVISDEBUG d->xml(stderr);
	}
}

/**********************/
/* DAVISREALTIME::XML */
/**********************/
void DavisRealtime::xml(FILE * f) const {
// Represent the Davis Real time packet
	Entry::xml(f);
	fprintf(f, "    <weather>\n");
	fprintf(f, "      <barotrend>%s</barotrend>\n", barotrend(data[3]));
	fprintf(f, "      <barometer>%.3f</barometer>\n", (float)(makeshort(data[7], data[8])) / 1000.0);
	fprintf(f, "      <insidetemp>%.1f</insidetemp>\n", (float)(makeshort(data[9], data[10])) / 10.0);
	fprintf(f, "      <insidehumidity>%u</insidehumidity>\n", data[11]);
	fprintf(f, "      <outsidetemp>%.1f</outsidetemp>\n", (float)(makeshort(data[12], data[13])) / 10.0);
	fprintf(f, "      <windspeed>%u</windspeed>\n", data[14]);
	fprintf(f, "      <avgwindspeed>%u</avgwindspeed>\n", data[15]);
	fprintf(f, "      <winddirection>%u</winddirection>\n", makeshort(data[16], data[17]));
	// Extra Temp 
	// Soil Temp
	// Leaf Temp
	fprintf(f, "      <outsidehumidity>%u</outsidehumidity>\n", data[33]);
	// Extra Humidities
	fprintf(f, "      <rainrate>%.2f</rainrate>\n", (float)(makeshort(data[41], data[42])) / 100.0);
	fprintf(f, "      <uv>%u</uv>\n", data[43]);
	fprintf(f, "      <solarradiation>%u</solarradiation>\n", makeshort(data[44], data[45]));
	fprintf(f, "      <stormrain>%.2f</stormrain>\n", (float)(makeshort(data[46], data[47])) / 100.0);
	fprintf(f, "      <stormstart>%s</stormstart>\n", date2xml(makeshort(data[48], data[49])));
	fprintf(f, "      <dayrain>%.2f</dayrain><monthrain>%.2f</monthrain><yearrain>%.2f</yearrain>\n", 
		(float)(makeshort(data[50], data[51])) / 100.0, (float)(makeshort(data[52], data[53])) / 100.0,  
		(float)(makeshort(data[54], data[55])) / 100.0); 
	fprintf(f, "      <dayevapotranspiration>%.2f</dayevapotranspiration>\n", 	(float)(makeshort(data[56], data[57])) / 100.0); 
	fprintf(f, "      <monthevapotranspiration>%.2f</monthevapotranspiration>\n", (float)(makeshort(data[58], data[59])) / 100.0); 
	fprintf(f, "      <yearevapotranspiration>%.2f</yearevapotranspiration>\n", (float)(makeshort(data[60], data[61])) / 100.0); 
	// Soil moistures
	// Leaf wetnesses
	fprintf(f, "      <alarms>%u</alarms>\n", makelong(data[70], data[71], data[72], data[73]));
	// Extra temp alarms
	// soil & leaf alarms
	fprintf(f, "      <battery>%.2f</battery>\n", (float)(makeshort(data[87], data[88]) * 300.0 / 51200.0));
	fprintf(f, "      <forecasticon>%u</forecasticon><forecastrule>%u</forecastrule>\n", data[89], data[90]);
	fprintf(f, "      <sunrise>%s</sunrise>\n", mins2hhmm(makeshort(data[91], data[92])));
	fprintf(f, "      <sunset>%s</sunset>\n",	mins2hhmm(makeshort(data[93], data[94])));

	fprintf(f, "    </weather>\n  </entry>\n");
}

/*************************/
/* DAVISREALTIME::MODBUS */
/*************************/
void DavisRealtime::modbus() const {
	int m;
	if (conf.theDavis < 0) return;			// Shouldn't have got here if there isn't a Davis
	m = conf[conf.theDavis].slot;
	DEBUG fprintf(stderr, " Davis::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;									// reference count
	conf.modbus[m++] = data[3];							// Barotrend
	conf.modbus[m++] = makeshort(data[7], data[8]);		// Barometer
	conf.modbus[m++] = makeshort(data[9], data[10]);	// Inside Temp
	conf.modbus[m++] = data[11];						// Inside humidity
	conf.modbus[m++] = makeshort(data[12], data[13]);	// Outside temp
	conf.modbus[m++] = data[14];						// Windpseed
	conf.modbus[m++] = data[15];						// Avg wind speed
	conf.modbus[m++] = makeshort(data[16], data[17]);	// Wind direction
	conf.modbus[m++] = data[33];						// Outside Humidity
	conf.modbus[m++] = makeshort(data[41], data[42]);	// Rain Rate
	conf.modbus[m++] = data[43];						//UV
	conf.modbus[m++] = makeshort(data[44], data[45]);	// Solar radiation
	conf.modbus[m++] = makeshort(data[46], data[47]);	//Storm rain
	conf.modbus[m++] = makeshort(data[48], data[49]);	// Storm start
	conf.modbus[m++] = makeshort(data[50], data[51]);	// Day rain
	conf.modbus[m++] = makeshort(data[52], data[53]);	// Month Rain
	conf.modbus[m++] = makeshort(data[54], data[55]);	// Year Rain
	conf.modbus[m++] = makeshort(data[56], data[57]);	// Day evapo
	conf.modbus[m++] = makeshort(data[58], data[59]);	// Month evapo
	conf.modbus[m++] = makeshort(data[60], data[61]);	// year evapo
	conf.modbus[m++] = makeshort(data[70], data[71]);	// Alarms low word
	conf.modbus[m++] = makeshort(data[72], data[73]);	// alarms high word
	conf.modbus[m++] = makeshort(data[87], data[88]);	// Batt Voltage
	conf.modbus[m++] = data[89];						// Forecast Icon
	conf.modbus[m++] = data[90];						// Forecast Rule
	conf.modbus[m++] = makeshort(data[91], data[92]);	// sunrise
	conf.modbus[m++] = makeshort(data[93], data[94]);	// Sunset
	// END MUTEX on modbus
	pthread_mutex_unlock(&mutexModbus);
}

/*******************/
/* DAVISHILOW::XML */
/*******************
void DavisHilow::xml(FILE * f) const {
// Represent the Davis HiLow  packet
//
// Warning: the mins2hhmm function has static data, so it gets overwritten if fprintf is called with it twice. 
	Entry::xml(f);
	fprintf(f, "    <hilow>\n");
// Barometer 
	fprintf(f, "      <barometer><dailylow>%.3f</dailylow>", (float)(makeshort(data[0], data[1])) / 1000.0);
	fprintf(f, "<dailyhigh>%.3f</dailyhigh>\n", (float)(makeshort(data[2], data[3])) / 1000.0);
	fprintf(f, "        <monthlylow>%.3f</monthlylow>", (float)(makeshort(data[4], data[5])) / 1000.0);
	fprintf(f, "<monthlyhigh>%.3f</monthlyhigh>\n", (float)(makeshort(data[6], data[7])) / 1000.0);
	fprintf(f, "        <yearlow>%.3f</yearlow>", (float)(makeshort(data[8], data[9])) / 1000.0);
	fprintf(f, "<yearhigh>%.3f</yearhigh>\n", (float)(makeshort(data[10], data[11])) / 1000.0);
	fprintf(f, "        <timelow>%s</timelow>", mins2hhmm(makeshort(data[12], data[13])));
	fprintf(f, "<timehigh>%s</timehigh>\n      </barometer>\n", mins2hhmm(makeshort(data[14], data[15])));
// Windspeed
	fprintf(f, "      <windspeed><dailyhigh>%u</dailyhigh>\n", data[16]);
	fprintf(f, "        <time>%s</time>\n", mins2hhmm(makeshort(data[17], data[18])));
	fprintf(f, "        <monthlyhigh>%u</monthlyhigh>\n", data[19]);
	fprintf(f, "        <yearhigh>%u</yearhigh>\n      </windspeed>\n", data[20]); 
// Inside Temp
	fprintf(f, "      <insidetemp><dayhigh>%.1f</dayhigh><daylow>%.1f</daylow>\n", 
		(float)(makeshort(data[21], data[22]))/10.0, (float)(makeshort(data[23], data[24])) / 10.0);
	fprintf(f, "        <timehigh>%s</timehigh>", 	mins2hhmm(makeshort(data[25], data[26])));
	fprintf(f, "<timelow>%s</timelow>\n", 		mins2hhmm(makeshort(data[27], data[28])));
	fprintf(f, "        <monthlylow>%.1f</monthlylow>", (float)(makeshort(data[29], data[30])) / 10.0);
	fprintf(f, "<monthlyhigh>%.1f</monthlyhigh>\n", (float)(makeshort(data[31], data[32])) / 10.0);
	fprintf(f, "        <yearlow>%.1f</yearlow>", (float)(makeshort(data[33], data[34])) / 10.0); 
	fprintf(f, "<yearhigh>%.1f</yearhigh>\n      </insidetemp>\n", (float)(makeshort(data[35], data[36])) / 10.0); 
// Inside Humidity
	fprintf(f, "      <insidehumidity><dayhigh>%u</dayhigh><daylow>%u</daylow>\n", data[37], data[38]);
	fprintf(f, "        <timehigh>%s</timehigh>",	mins2hhmm(makeshort(data[39], data[40])));
	fprintf(f, "<timelow>%s</timelow>\n",  mins2hhmm(makeshort(data[41], data[42])));
	fprintf(f, "        <monthlyhigh>%u</monthlyhigh><monthlylow>%u</monthlylow>\n", data[43], data[44]);
	fprintf(f, "        <yearhigh>%u</yearhigh><yearlow>%u</yearlow>\n      </insidehumidity>\n", data[45], data[46]); 
// Outside Temp
	fprintf(f, "      <outsidetemp><daylow>%.1f</daylow><dayhigh>%.1f</dayhigh>\n", 
		(float)(makeshort(data[47], data[48]))/10.0, (float)(makeshort(data[49], data[50])) / 10.0);
	fprintf(f, "        <timelow>%s</timelow>", 		mins2hhmm(makeshort(data[51], data[52])));
	fprintf(f, "<timehigh>%s</timehigh>\n",  mins2hhmm(makeshort(data[53], data[54])));
	fprintf(f, "        <monthlyhigh>%.1f</monthlyhigh>", (float)(makeshort(data[55], data[56])) / 10.0);
	fprintf(f, "<monthlylow>%.1f</monthlylow>\n", (float)(makeshort(data[57], data[58])) / 10.0);
	fprintf(f, "        <yearhigh>%.1f</yearhigh>", (float)(makeshort(data[59], data[60])) / 10.0); 
	fprintf(f, "<yearlow>%.1f</yearlow>\n      </outsidetemp>\n", (float)(makeshort(data[61], data[62])) / 10.0); 
// Dew Point
	fprintf(f, "      <dewpoint><daylow>%.1f</daylow><dayhigh>%.1f</dayhigh>\n", 
		(float)(makeshort(data[63], data[64]))/10.0, (float)(makeshort(data[65], data[66])));
	fprintf(f, "        <timelow>%s</timelow>", 		mins2hhmm(makeshort(data[67], data[68])));
	fprintf(f, "<timehigh>%s</timehigh>\n",  mins2hhmm(makeshort(data[69], data[70])));
	fprintf(f, "        <monthlyhigh>%.1f</monthlyhigh>", (float)(makeshort(data[71], data[72])));
	fprintf(f, "<monthlylow>%.1f</monthlylow>\n", (float)(makeshort(data[73], data[74])));
	fprintf(f, "        <yearhigh>%.1f</yearhigh>", (float)(makeshort(data[75], data[76]))); 
	fprintf(f, "<yearlow>%.1f</yearlow>\n      </dewpoint>\n", (float)(makeshort(data[77], data[78])));
// Wind Chill
	fprintf(f, "      <windchill><daylow>%.1f</daylow>\n", (float)(makeshort(data[79], data[80])));
	fprintf(f, "        <timelow>%s</timelow>\n", mins2hhmm(makeshort(data[81], data[82])));
	fprintf(f, "        <monthlylow>%.1f</monthlylow>\n", (float)(makeshort(data[83], data[84])));
	fprintf(f, "        <yearlow>%.1f</yearlow>\n      </windchill>\n", (float)(makeshort(data[85], data[86]))); 
// Heat Index
	fprintf(f, "      <heatindex><dayhigh>%.1f</dayhigh>\n", (float)(makeshort(data[87], data[88])));
	fprintf(f, "        <timehigh>%s</timehigh>\n", mins2hhmm(makeshort(data[89], data[90])));
	fprintf(f, "        <monthlyhigh>%.1f</monthlyhigh>\n", (float)(makeshort(data[91], data[92])));
	fprintf(f, "        <yearhigh>%.1f</yearhigh>\n      </heatindex>\n", (float)(makeshort(data[93], data[94]))); 
// THSW
	fprintf(f, "      <thsw><dayhigh>%.1f</dayhigh>\n", (float)(makeshort(data[95], data[96])));
	fprintf(f, "        <timehigh>%s</timehigh>\n", mins2hhmm(makeshort(data[97], data[98])));
	fprintf(f, "        <monthlyhigh>%.1f</monthlyhigh>\n", (float)(makeshort(data[99], data[100])));
	fprintf(f, "        <yearhigh>%.1f</yearhigh>\n      </thsw>\n", (float)(makeshort(data[101], data[102]))); 
// Solar Radiation
	fprintf(f, "      <solarradiation><dayhigh>%.1f</dayhigh>\n", (float)(makeshort(data[103], data[104])));
	fprintf(f, "        <timehigh>%s</timehigh>\n", mins2hhmm(makeshort(data[105], data[106])));
	fprintf(f, "        <monthlyhigh>%.1f</monthlyhigh>\n", (float)(makeshort(data[107], data[108])));
	fprintf(f, "        <yearhigh>%.1f</yearhigh>\n      </solarradiation>\n", (float)(makeshort(data[109], data[110]))); 
// UV
	fprintf(f, "      <uv><dayhigh>%u</dayhigh>\n", data[111]);
	fprintf(f, "        <timehigh>%s</timehigh>\n", mins2hhmm(makeshort(data[112], data[113])));
	fprintf(f, "        <monthlyhigh>%u</monthlyhigh>\n", data[114]);
	fprintf(f, "        <yearhigh>%u</yearhigh>\n      </uv>\n", data[115]);
// Rain Rate
	fprintf(f, "      <rainrate><dayhigh>%.1f</dayhigh>\n", (float)(makeshort(data[116], data[117]))/10.0);
	fprintf(f, "        <timehigh>%s</timehigh>\n", mins2hhmm(makeshort(data[118], data[119])));
	fprintf(f, "        <hourhigh>%.1f</hourhigh>\n", (float)(makeshort(data[120], data[121])) / 10.0);
	fprintf(f, "        <monthlyhigh>%.1f</monthlyhigh>\n", (float)(makeshort(data[122], data[123])) / 10.0);
	fprintf(f, "        <yearhigh>%.1f</yearhigh>\n      </rainrate>\n", (float)(makeshort(data[124], data[125])) / 10.0); 
// Extra Leaf Soil Temps - NA
// Outside / Extra Humidities
	fprintf(f, "      <outsidehumidity<daylow>%u</daylow>><dayhigh>%u</dayhigh>\n", data[276], data[284]);
	fprintf(f, "        <timelow>%s</timelow>", 
		mins2hhmm(makeshort(data[292], data[293])));
	fprintf(f, "<timehigh>%s</timehigh>\n", mins2hhmm(makeshort(data[308], data[309])));
	fprintf(f, "        <monthlyhigh>%u</monthlyhigh><monthlylow>%u</monthlylow>\n", data[324], data[332]);
	fprintf(f, "        <yearhigh>%u</yearhigh><yearlow>%u</yearlow>\n      </outsidehumidity>\n", data[340], data[348]); 
// Soil Moisture - NA
// Leaf Wetness - NA
	fprintf(f, "    </hilow>\n  </entry>\n");
}

*/
// DEFINES related to EEPROM locations
#define GRAPH_START		176

/*******************/
/* DAVISGRAPH::XML */
/*******************
void DavisGraph::xml(FILE * f) const {
// Represent the Davis Real time packet
	Entry::xml(f);
	fprintf(f, "    <graph>\n");
// Inside Temp
	fprintf(f, "      <insidetemp>\n");
	DETAILDEBUG fprintf(stderr,"Data = %d [%x], TEMP_IN_HOUR = %d ", data, data, TEMP_IN_HOUR);
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[TEMP_IN_HOUR], data[NEXT_HOUR_PTR], 24, 90));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[TEMP_IN_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[TEMP_IN_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <daylow>%s</daylow>\n",           graphbytes(&data[TEMP_IN_DAY_LOWS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n",   graphtimes(&data[TEMP_IN_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphbytes(&data[TEMP_IN_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 90));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </insidetemp>\n", graphbytes(&data[TEMP_IN_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 90));
	// Inside Year Highs claims to only have one value.
// Outside Temp
	fprintf(f, "      <outsidetemp>\n");
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[TEMP_OUT_HOUR], data[NEXT_HOUR_PTR], 24, 90));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[TEMP_OUT_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[TEMP_OUT_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <daylow>%s</daylow>\n",           graphbytes(&data[TEMP_OUT_DAY_LOWS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n",   graphtimes(&data[TEMP_OUT_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphbytes(&data[TEMP_OUT_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 90));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </outsidetemp>\n", graphbytes(&data[TEMP_OUT_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 90));
	// Outside temp claims to have 25 years data
// Dew Point
	fprintf(f, "      <dewpoint>\n");
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[DEW_HOUR], data[NEXT_HOUR_PTR], 24, 90));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[DEW_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[DEW_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <daylow>%s</daylow>\n",           graphbytes(&data[DEW_DAY_LOWS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n",   graphtimes(&data[DEW_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphbytes(&data[DEW_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 90));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </dewpoint>\n", graphbytes(&data[DEW_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 90));
// Chilll Factor
	fprintf(f, "      <chill>\n");
	fprintf(f, "        <hour>%s</hour>\n",             graphbytes(&data[CHILL_HOUR], data[NEXT_HOUR_PTR], 24, 90));
	fprintf(f, "        <daylow>%s</daylow>\n",         graphbytes(&data[CHILL_DAY_LOWS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n", graphtimes(&data[CHILL_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </chill>\n", graphbytes(&data[CHILL_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 90));
// THSW
	fprintf(f, "      <thsw>\n");
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[THSW_HOUR], data[NEXT_HOUR_PTR], 24, 90));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[THSW_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[THSW_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n      </thsw>", graphbytes(&data[THSW_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 90));
// Heat Index
	fprintf(f, "      <heatindex>\n");
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[HEAT_HOUR], data[NEXT_HOUR_PTR], 24, 90));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[HEAT_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 90));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[HEAT_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n      </heatindex>", graphbytes(&data[HEAT_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 90));
// Inside Humidity
	fprintf(f, "      <insidehumidity>\n");
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[HUM_IN_HOUR], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[HUM_IN_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[HUM_IN_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <daylow>%s</daylow>\n",           graphbytes(&data[HUM_IN_DAY_LOWS], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n",   graphtimes(&data[HUM_IN_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphbytes(&data[HUM_IN_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 0));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </insidehumidity>\n", graphbytes(&data[HUM_IN_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 0));
// Inside Humidity
	fprintf(f, "      <outsidehumidity>\n");
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[HUM_OUT_HOUR], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[HUM_OUT_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[HUM_OUT_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <daylow>%s</daylow>\n",           graphbytes(&data[HUM_OUT_DAY_LOWS], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n",   graphtimes(&data[HUM_OUT_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphbytes(&data[HUM_OUT_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 0));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </outsidehumidity>\n", graphbytes(&data[HUM_OUT_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 0));
// Barometric
	fprintf(f, "      <barometric>\n");
	fprintf(f, "        <15min>%s</15min>\n",             graphfloat(&data[BAR_15_MIN], data[NEXT_15MIN_PTR], 24, 1000.0));
	fprintf(f, "        <hour>%s</hour>\n",               graphfloat(&data[BAR_HOUR], data[NEXT_HOUR_PTR], 24, 1000.0));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphfloat(&data[BAR_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 1000.0));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[BAR_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <daylow>%s</daylow>\n",           graphfloat(&data[BAR_DAY_LOWS], data[NEXT_DAY_PTR], 24, 1000.0));
	fprintf(f, "        <daylowtime>%s</daylowtime>\n",   graphtimes(&data[BAR_DAY_LOW_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphfloat(&data[BAR_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 1000.0));
	fprintf(f, "        <monthlow>%s</monthlow>\n      </barometric>\n", graphfloat(&data[BAR_MONTH_LOWS], data[NEXT_MONTH_PTR], 25, 1000.0));
// Wind Speed
	fprintf(f, "      <windspeed>\n");
	fprintf(f, "        <10min>%s</10min>\n",             graphbytes(&data[WIND_SPEED_10_MIN_AVG], data[NEXT_10MIN_PTR], 24, 0));
	fprintf(f, "        <hour>%s</hour>\n",               graphbytes(&data[WIND_SPEED_HOUR_AVG], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <hourhigh>%s</hourhigh>\n",       graphbytes(&data[WIND_SPEED_HOUR_HIGHS], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphbytes(&data[WIND_SPEED_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[WIND_SPEED_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <dayhighdir>%s</dayhighdir>\n",   graphbytes(&data[WIND_SPEED_DAY_HIGH_DIR], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n",     graphbytes(&data[WIND_SPEED_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 0));
	fprintf(f, "        <monthhighdir>%s</monthhighdir>\n      </windspeed>\n", graphbytes(&data[WIND_SPEED_MONTH_HIGH_DIR], data[NEXT_MONTH_PTR], 25, 0));
// Wind Direction
	fprintf(f, "      <winddir>\n");
	fprintf(f, "        <hour>%s</hour>\n",       graphbytes(&data[WIND_DIR_HOUR], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <day>%s</day>\n",         graphbytes(&data[WIND_DIR_DAY], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <month>%s</month>\n",     graphbytes(&data[WIND_DIR_MONTH], data[NEXT_MONTH_PTR], 24, 0));
	fprintf(f, "        <daybins>%s</daybins>\n", graphbytes(&data[WIND_DIR_DAY_BINS], data[NEXT_DAY_PTR], 8, 0));	// Which PTR to use *****
	fprintf(f, "        <monthbins>%s</monthbins>\n      </winddir>\n", graphbytes(&data[WIND_DIR_MONTH_BINS], data[NEXT_MONTH_PTR], 8, 0));
// Rain Rate
	fprintf(f, "      <rainrate>\n");
	fprintf(f, "        <1min>%s</1min>\n",               graphfloat(&data[RAIN_RATE_1_MIN], data[NEXT_10MIN_PTR], 24, 100.0));
	fprintf(f, "        <hour>%s</hour>\n",               graphfloat(&data[RAIN_RATE_HOUR], data[NEXT_HOUR_PTR], 24, 100.0));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n",         graphfloat(&data[RAIN_RATE_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 100.0));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n", graphtimes(&data[RAIN_RATE_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <monthhigh>%s</monthhigh>\n      </rainrate>\n",     graphfloat(&data[RAIN_RATE_MONTH_HIGHS], data[NEXT_MONTH_PTR], 25, 100.0));
// Rain
	fprintf(f, "      <rain>\n");
	fprintf(f, "        <15min>%s</15min>\n",             graphfloat(&data[RAIN_15_MIN], data[NEXT_15MIN_PTR], 24, 100.0));
	fprintf(f, "        <hour>%s</hour>\n",               graphfloat(&data[RAIN_HOUR], data[NEXT_HOUR_PTR], 24, 100.0));
	fprintf(f, "        <storm>%s</storm>\n",             graphfloat(&data[RAIN_STORM], data[NEXT_HOUR_PTR], 25, 100.0));
	fprintf(f, "        <stormstarttime>%s</stormstarttime>\n", graphtimes(&data[RAIN_STORM_START], data[NEXT_DAY_PTR], 25));
	fprintf(f, "        <stormendtime>%s</stormendtime>\n", graphtimes(&data[RAIN_STORM_END], data[NEXT_DAY_PTR], 25));
	fprintf(f, "        <day>%s</day>\n",                 graphfloat(&data[RAIN_DAY_TOTAL], data[NEXT_DAY_PTR], 25, 100.0));
	fprintf(f, "        <month>%s</month>\n      </rain>\n",     graphfloat(&data[RAIN_MONTH_TOTAL], data[NEXT_MONTH_PTR], 25, 100.0));
// Evapotranspiration
	fprintf(f, "      <evapotranspiration>\n");
	fprintf(f, "        <hour>%s</hour>\n", graphfloat(&data[ET_HOUR], data[NEXT_HOUR_PTR], 24, 100.0));
	fprintf(f, "        <day>%s</day>\n",   graphfloat(&data[ET_DAY_TOTAL], data[NEXT_DAY_PTR], 25, 100.0));
	fprintf(f, "        <month>%s</month>\n      </evapotranspiration>\n", graphshort(&data[WIND_DIR_MONTH_BINS], data[NEXT_MONTH_PTR], 25));
// Solar radiation
	fprintf(f, "      <solar>\n");
	fprintf(f, "        <hour>%s</hour>\n", graphshort(&data[SOLAR_HOUR_AVG], data[NEXT_HOUR_PTR], 24));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n", graphshort(&data[SOLAR_DAY_HIGHS], data[NEXT_DAY_PTR], 24));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n      </solar>\n", graphtimes(&data[SOLAR_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
// UV
	fprintf(f, "      <uv>\n");
	fprintf(f, "        <hour>%s</hour>\n", graphbytes(&data[UV_HOUR_AVG], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <medshour>%s</medshour>\n", graphbytes(&data[UV_MEDS_HOUR], data[NEXT_HOUR_PTR], 24, 0));
	fprintf(f, "        <medsday>%s</medshigh>\n", graphbytes(&data[UV_MEDS_DAY], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <dayhigh>%s</dayhigh>\n", graphbytes(&data[UV_DAY_HIGHS], data[NEXT_DAY_PTR], 24, 0));
	fprintf(f, "        <dayhightime>%s</dayhightime>\n      </uv>\n", graphtimes(&data[UV_DAY_HIGH_TIMES], data[NEXT_DAY_PTR], 24));
	fprintf(f, "    </graph>\n  </entry>\n");
}

*/
// Conversion functions
const char * barotrend(unsigned char c) {
	switch (c) {
	case 196: return "Falling Rapidly";
	case 236: return "Falling Slowly";
	case 0 : return "Steady";
	case 20: return "Rising Slowly";
	case 60: return "Rising Rapidly";
	case 'P': return "N/A";
	}
	return "Barotrend Error";
}

char * mins2hhmm(int x) {
	static char res[6];
	if (x == 0xFFFF) res[0] = '\0';
	else
		snprintf(res, sizeof(res), "%02d:%02d", x / 100, x % 100);
	return res;
}

// Convert davis format to days since 2000.
// M  M  M  M  D  D  D  D  D  Y  Y  Y  Y  Y  Y  Y
// 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0

char * date2xml(int x) {
// If it's FFFF, return empty string.
	struct tm *tm;
	time_t t;
	static char buf[18];
	if (x == 0xFFFF) {
		buf[0] = '\0';
		return buf;
	}
	time(&t);
	tm = localtime(&t);
	tm->tm_mon = (x & 0xF000) >> 12 - 1;	// tm_mon starts at Jan = 0
	tm->tm_mday = (x & 0xf80) >> 6;
	tm->tm_year = (x & 0x07F) + 100;	// Davis count from 2000 not 1900
	tm->tm_hour = 0;
	strftime(buf, 18, "%Y-%m-%d%z", tm);
	// Glitch - the %z specifier returns +/-hhmm, not +/=hh:mm.
	buf[16] = '\0';
	buf[15] = buf[14];
	buf[14] = buf[13];
	buf[13] = ':';
	DETAILDEBUG cerr << "Date2xml: tm_isdt = " << tm->tm_isdst << " tm_zone = " << tm->tm_zone << " gmtoff = " << tm->tm_gmtoff << endl;
	return buf;
}

/*
char result[300]; // Static array used by the conversion functions below
char * graphbytes(const unsigned char * data, int current, int size, int offset) {
// return a string like 1,2,3,4 from byte data
// Max len = 4 * 25 = 100 (25 of 255,255,255,255) 
// Start at data[current], increment while current < size. Wrap around.
// Subtract offset, as temperatures are biased by 90F.
	int i;
	char item[6];
	result[0] = '\0';
	DETAILDEBUG fprintf(stderr, "Graphbytes Data at %d [%x] size = %d offset = %d ", data, data, size, offset);
	for (i = 0; i < size-1; i++) {
		DETAILDEBUG fprintf(stderr, "Current = %d, data[current] = %d\n", current, data[current]); 
		sprintf(item, "%d,", data[current++] - offset);
		strcat(result, item);
				DETAILDEBUG if (strlen(result) > 150) { result[150] = '\0'; return result;}

		if (current == size) current = 0;	// wrap around
	}
	sprintf(item, "%d", data[current] - offset);		// last one has no comma
	strcat(result, item);
	return result;
}
*/
/*
char * graphtimes(const unsigned char * data, int current, int size) {
// return a string like 11:00,12:00 from byte data
// Max len = 6 * 25 = 150 (25 of hh:mm,)
// Start at data[current], increment while current < size. Wrap around.
// Would be neater is we treated data as array of short - but need to consider byte order,
// so makeshort is the right way to go.
	int i;
	char item[8];
	result[0] = '\0';
	DETAILDEBUG fprintf(stderr, "Graphtimes Data at %d [%x] size = %d ", data, data, size);

	for (i = 0; i < size-1; i++) {
		sprintf(item, "%s,", mins2hhmm(makeshort(data[current++], data[current++])));
		strcat(result, item);		DETAILDEBUG if (strlen(result) > 200) { strcpy(result, "TRUNCATION"); return result;}
		if (current / 2 == size) current = 0;	// wrap around
	}
	sprintf(item, "%s", mins2hhmm(makeshort(data[current], data[current+1])));		// last one has no comma
	strcat(result, item);
	return result;
}
*/
/*
char * graphfloat(const unsigned char * data, int current, int size, float scale) {
// return a string like 25.123,25.126 from short data
// Max len = 8 * 25 = 200 (25 of xxx.yyy,)
// Need to be very careul about the use of *data++ ins makeshort, as it's a macro
	int i;
	char item[10];
	result[0] = '\0';	
	if (scale == 0.0) return 0;
	DETAILDEBUG fprintf(stderr, "Graphfloat Data at %d [%x] size = %d scale = %f ", data, data, size, scale);

	for (i = 0; i < size-1; i++) {
		sprintf(item, "%2.3f,", (float)(makeshort(data[current++], data[current++]))/scale);
		strcat(result, item);		DETAILDEBUG if (strlen(result) > 200) { strcpy(result, "TRUNCATION"); return result;}

		if (current / 2 == size) current = 0;	// wrap around
	}
	sprintf(item, "%2.3f", (float)(makeshort(data[current], data[current]))/scale);
	strcat(result, item);
	return result;
}
*/
/*
char * graphshort(const unsigned char * data, int current, int size) {
// return a string like 123,456,789 from short data
// Max len = 7 * 25 = 175 (25 of xx.yyy,)
// Need to be very careful about the use of *data++ in makeshort, as it's a macro
	int i;
	char item[8];
	result[0] = '\0';
	DETAILDEBUG fprintf(stderr, "Graphshort Data at %d [%x] size = %d ", data, data, size);

	for (i = 0; i < size-1; i++) {
		sprintf(item, "%u,", makeshort(data[current++], data[current++]));
		strcat(result, item);		DETAILDEBUG if (strlen(result) > 200) { strcpy(result, "TRUNCATION"); return result;}

		if (current / 2 == size) current = 0;	// wrap around
	}
	sprintf(item, "%u", makeshort(data[current], data[current]));
	strcat(result, item);
	return result;
}
*/
void dumphex(const char *msg, const int count) {
//
	int i;
	for (i = 0; i < count; i++)
		fprintf(stderr, "%02x ", (unsigned char)*msg++);
	fprintf(stderr, "\n");
}
