/*
 *  modbus.h
 *  Mcp
 *
 *  Created by Martin Robinson on 10/04/2009.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#define MODBUSLAYOUT 101
#define MODBUSSOCKETS 5

void setModbusFD(fd_set *fds);
void handle_modbus(fd_set *fds);
void initModbus();
extern int modbusFD[];
