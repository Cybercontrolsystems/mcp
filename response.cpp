/*
 *  Response.cpp
 *  mcp1.0
 *
 *  Created by Martin Robinson on 23/10/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 * This handles both Response and Ack files.
 *
 * $Id: response.cpp,v 3.5 2013/04/08 20:20:55 martin Exp $
 */

#include <stdio.h>  // for stdio
#include <sys/stat.h>	// for struct stat

#include "mcp.h"
#include "meter.h"
#include "journal.h"
#include "config.h"	
#include "action.h" 
#include "command.h"
#include "response.h"

extern Queue queue;
extern Journal journal;

const char * responseTypeStr[] = {"noelement", "download", "response", "setvar", "command", "get", "put", 
	"getfrom", "putfrom", "getto", "putto", "internal", NULL};
enum responseType {noelement=0, download_e, response_e, setvar_e, command_e, get_e, put_e, 
	getfrom_e, putfrom_e, getto_e, putto_e, internal_e};

/* UTILITY FUNCTIONS */
string & xmldecode(string & str) {
	int loc = 0;
	while ((loc = str.find("&gt;", loc)) != string::npos)
			str.replace(loc, 4, ">");
	loc = 0;
	while ((loc = str.find("&lt;", loc)) != string::npos)
			str.replace(loc, 4, "<");
	loc = 0;
	while ((loc = str.find("&amp;", loc)) != string::npos)
			str.replace(loc, 4, "&");
	return str;
}

bool Response::read(const string & str) {
// Read in a response file
// Do we want to know if there is a problem processing the file?
// Is it an error if the file is not there?
// from XML file
	const char * filename = str.c_str();
	DEBUG cerr << "Response::Reading " <<  filename << endl;
	FILE * f;
	char buf[BUFSIZE];
	char buf2[64];
	size_t len;
	int done = 0;
	int retval = true;
	if (!(f = fopen(filename, "r"))) {
		snprintf(buf, sizeof(buf), "Can't open %s", filename);
		throw buf; 
	}
	
	try {
	while (!done) {	// loop until entre file has been processed.
		len = fread(buf, 1, BUFSIZE, f);
		done = len < BUFSIZE;
		if (!XML_Parse(buf, len, done)) {
			snprintf(buf2, sizeof(buf2),	"%s at line %d\n",		
				XML_ErrorString(XML_GetErrorCode()),
				XML_GetCurrentLineNumber());
			throw buf2;
		}
	}
	}
	catch (const char * msg) {
		string str("event ERROR reading response file ");
		str += msg;
		journal.add(new Message(str));
		retval = false;
	}
	fclose(f);
	return retval; // so far so good
}

bool Response::stat(const string & filename) const {
	struct stat sb;
	return (::stat(filename.c_str(), &sb) == 0);
}

/* -------------*/
/* STARTELEMENT */
/*--------------*/

void Response::startElement(const char *name, const char **atts)
// XML_Parse function called at element start time.
{
	int i, element;
	
	CONFIGDEBUG fprintf(stderr, "Start: %s ", name);
	// identify the element name and store in userdata
	for (i = 0; responseTypeStr[i]; i++)
	if (strcmp(name, responseTypeStr[i]) == 0) {
		element = i;
		break;
	}
	if (element == 0) {
		fprintf(stderr, "Config: element <%s> not valid\n", name);
	}
	
	// For all start tags, initialise text buffer.
	text[0] = '\0';
	
	switch(element) {
	case response_e:						// <response at="yyyy-mm-ddThh:mm:ss+00:00 after="nn">
		timeval = time(0) + after;
		while (*atts) {
		if (strcmp(atts[0],"at")==0) {
			if ((timeval = getDateTime(atts[1])) <= 0)
				fprintf(stderr, "Time in at= is not a date/time %s \n", atts[1]);
			DEBUG cerr<< "Set timeval = " << timeval << endl;
		} else
		if (strcmp(atts[0], "after")==0) {
			after += getNonZeroInt(atts[1]);
			timeval = time(0) + after;
		}
		else fprintf(stderr, "Site: unrecognised attribute %s\n", atts[0]);
		atts += 2;
		};
		break;
	case setvar_e:						// <setvar name="">
		if (atts[0] && strcmp(atts[0],"name")==0) {
		strcpy(varName, atts[1]);
		} else fprintf(stderr, "Server: unrecognised attribute %s\n", atts[0]);
		break;
	case get_e:						// <get owner="" mode-"">
		while (*atts) {
			if (strcmp(atts[0],"owner")==0) {
				strcpy(owner, atts[1]);
			} else if (strcmp(atts[0], "mode")==0) {
				strcpy(mode, atts[1]);
			} else fprintf(stderr, "MCP: unrecognised attribute %s\n", atts[0]);
			atts += 2;
		}
		break;
	case getfrom_e:
	case putto_e:
		if (atts[0] && strcmp(atts[0],"server")==0) {
		strcpy(server, atts[1]);
		} else fprintf(stderr, "Server: unrecognised attribute %s\n", atts[0]);
		break;
	
		// These are simple elements, to be handled by Texthandler
	case put_e:
	case getto_e:
	case putfrom_e:
	case download_e:
	case command_e:
	case internal_e:
		break;
	default: fprintf(stderr,"Unknown start element %s\n", name);
	}
}

/*-------------*/
/* TEXTHANDLER */
/*-------------*/

void Response::charData(const XML_Char *s, int len) {
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

void Response::endElement(const char *name)
// XML_Parse function called at element closing time.
// In particular, deal with content of tags.  This has been collected by TextHandler.
{
	string s;
	int element;
	for (element=0; responseTypeStr[element]; element++)
		if (strcmp(name, responseTypeStr[element]) == 0) {
			CONFIGDEBUG fprintf(stderr, "End tag %s Text = %s (%zd)\n", name, text, strlen(text));
			break;
		}
	switch(element) {
	case setvar_e:		// <setvar>
		DEBUG cerr << "Setvar: got Name:"<<varName<<" Value: "<<text<<" Timeval: " << timeval << " " << ctime(&timeval);
		s = varName;	// Have to build it bit by bit.
		s += "=";
		s += text;
		queue.add(new SetCmd(timeval, s));
		break;
	case command_e:		// <command>
		DEBUG cerr << "Command: "<<text<<endl;
		s = text;
		queue.add(new Command(timeval, s));
		break;
	case internal_e:
		DEBUG cerr << "Internal Command: "<<text<<endl;
		s = text;
		queue.add(new Internal(timeval, s));
		break;
	case getfrom_e:		// <getfrom server=>
	case putfrom_e:		// <putfrom>
		CONFIGDEBUG cerr << "GetFrom/PutFrom: "<<text<<endl;
		strcpy(from, text);
		break;
	case getto_e:		// <getto>
	case putto_e:		// <putto server =>
		CONFIGDEBUG cerr << "GetTo/PutTo: "<<text<<endl;
		strcpy(to, text);
		break;
	case get_e:			// <get owner= mode=>
		DEBUG cerr << "Get Owner: "<<owner<<" Mode: "<<mode<<" Server: "<<server<<" GetFrom: "
			<<from<<" To: "<<to<<endl;
		queue.add(new Get(timeval, owner, mode, server, from, to));
		break;
	case put_e:			// <put>
		DEBUG cerr << "Put Server: " <<  server<<" GetFrom: " << from << " To: " << to << endl;
		queue.add(new Put(timeval, server, from, to));
		break;
	case response_e:	// <response> - clear timeval
		break;
	default:;		// Don't need to do anything. It's also not an error, just text where we don't want it
	}
}

/* ACK */
///////////////////////////////////////////////////////////////////////////////////////////////////////

bool Ack::read(const string & str) {
// Read in a Ack file
// Do we want to know if there is a problem processing the file?
// Is it an error if the file is not there?
// from XML file
	const char * filename = str.c_str();
	DEBUG cerr << " Ack::Reading " <<  filename << endl;
	FILE * f;
	char buf[BUFSIZE];
	char buf2[64];
	size_t len;
	int done = 0;
	int retval = true;
	if (!(f = fopen(filename, "r"))) {
		snprintf(buf, sizeof(buf), "Can't open %s", filename);
		throw buf; 
	}
	
	try {
	while (!done) {	// loop until entre file has been processed.  Shouldn't be necessary for an Ack file.
		len = fread(buf, 1, BUFSIZE, f);
		done = len < BUFSIZE;
		if (!XML_Parse(buf, len, done)) {
			snprintf(buf2, sizeof(buf2),	"%s at line %d\n",		
				XML_ErrorString(XML_GetErrorCode()),
				XML_GetCurrentLineNumber());
			throw buf2;
		}
	}
	}
	catch (const char * msg) {
		string str("event ERROR reading response file ");
		str += msg;
		journal.add(new Message(str));
		retval = false;
	}
	fclose(f);
	return retval; // so far so good
}

bool Ack::stat(const string & filename) const {
	struct stat sb;
	return (::stat(filename.c_str(), &sb) == 0);
}

/* -------------*/
/* STARTELEMENT */
/*--------------*/

void Ack::startElement(const char *name, const char **atts)
// XML_Parse function called at element start time.
{
	
	CONFIGDEBUG fprintf(stderr, "Start: %s ", name);
	// For all start tags, initialise text buffer.
	text[0] = '\0';
}

/*-------------*/
/* TEXTHANDLER */
/*-------------*/

void Ack::charData(const XML_Char *s, int len) {
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

void Ack::endElement(const char *name)
// XML_Parse function called at element closing time.
// In particular, deal with content of tags.  This has been collected by TextHandler.
{
	string s;
	if (strcmp(name, "lastprocessed") != 0) {
		s = "event WARN invalid tag '";
		s += name;
		s += "' in ack.xml";
		journal.add(new Message(s));
		return;
	}
	// If ack is empty, just WARN not ERROR (1.47)
	if (strlen(text) == 0) {
		s = "event WARN empty ack file";
		journal.add(new Message(s));
		return;
	}
	CONFIGDEBUG fprintf(stderr, "End tag %s Text = %s (%zd)\n", name, text, strlen(text));
	try {
	lastProcessed = getDateTime(text);
	}
		catch (const char * msg) {
		s = "event ERROR reading ack file ";
		s += msg;
		journal.add(new Message(s));
	}
}

