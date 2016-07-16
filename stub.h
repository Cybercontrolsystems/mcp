/*
 *  stub.h
 *  Mcp
 *
 *  Created by Martin Robinson on 07/04/2013.
 *  Copyright 2013. All rights reserved.
 *
 * To permit linking on OSX where these functions are not implemented.
 *
 * $Revision$
 */

char * getVersion(const char *) { return const_cast<char *>("NONE"); }

void determinePlatform() {}

void blinkLED(int, int) {}