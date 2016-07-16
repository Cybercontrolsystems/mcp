/*
 *  command.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 22/03/2007.
 *  Copyright 2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 *  Command processing routines - to take the weight out of mcp.cpp
 *
 * $Id: command.h,v 3.5 2013/04/08 20:20:54 martin Exp $
 */

void systemStatus(void);
void doCommand(const char * command, ostringstream & result);
void start_device(int num);
void doConsole(struct sockconn_s & s);
void doFifo(int fd);
void setupFifo(void);

/* UTILITY FUNCTIONS */
int getInt(const char * cp1, const int prev = 0) throw (const char *);
int getInt(const string & str, const int prev = 0) throw (const char *);
float getFloat(const string & str, const float prev = 0) throw (const char *);
int getNonZeroInt(const string & str, const int prev = 0) throw (const char *);
template <typename T> inline T constrain(T val, T lower, T upper) {
	return val = val > lower ? (val > upper ? upper : val) : lower;
}
inline float constrain(float val, float lower, float upper) {
	return val = val > lower ? (val > upper ? upper : val) : lower;
}
char * asTime(const time_t t);
time_t getTime(const string & str) throw (const char *);
time_t getDateTime(const string & str) throw (const char *);
time_t getDuration(const char * str) throw (const char *);
char * asDuration(const time_t t);
time_t readCMOS(int loc);
void writeCMOS(int loc, time_t t);
char * getmac(void);
