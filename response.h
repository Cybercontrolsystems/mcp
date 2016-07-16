/*
 *  Response.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 23/10/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * This handles the Response data. We assume it's a file - it could be a stream.
 *
 * $Id: response.h,v 3.5 2013/04/08 20:20:55 martin Exp $
 */

#include "expatpp.h" 

class Response : public expatpp {
public:
	Response() : timeval(0), after(0) {};
	bool read(const string & str);  // from XML file
	bool stat(const string & filename) const; // Is it there yet?
	void dump();			// to stdout
private:
	time_t timeval;
	time_t after;
	virtual void startElement(const XML_Char *name, const XML_Char **atts);
	virtual void endElement(const XML_Char* name);
	virtual void charData(const XML_Char *s, int len);
	char text[TEXTSIZE];
	char varName[TEXTSIZE];				// for setvar
	char owner[TEXTSIZE];				// for get
	char server[TEXTSIZE];				// for putto and getfrom
	char from[TEXTSIZE];
	char to[TEXTSIZE];
	char mode[TEXTSIZE];					
	
};

class Ack : public expatpp {
public:
	Ack() : lastProcessed(0) {};
	bool read(const string & str);  // from XML file
	bool stat(const string & filename) const; // Is it there yet?
	void dump();			// to stdout
	time_t lastProcessed;	
private:
	virtual void startElement(const XML_Char *name, const XML_Char **atts);
	virtual void endElement(const XML_Char* name);
	virtual void charData(const XML_Char *s, int len);
	char text[TEXTSIZE];

};
