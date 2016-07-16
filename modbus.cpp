/*
 *  modbus.cpp
 *  Mcp
 *
 *  Created by Martin Robinson on 10/04/2009.
 *  Copyright 2009. All rights reserved.
 *
 */
#include <netinet/in.h>	// for sockaddr_in
#include <sys/socket.h>
#include <fcntl.h>

#include "mcp.h"
#include "modbus.h"
#include "journal.h"
#include "config.h"

pthread_mutex_t mutexModbus = PTHREAD_MUTEX_INITIALIZER;
extern Config conf;
extern Journal journal;	

// extern int errno;
#include <errno.h>

// ADD CONTROLLER
int deviceModbus [] =  {0, STECAMODBUS, VICTRONMODBUS, TURBINEMODBUS, FRONIUSMODBUS, DAVISMODBUS, PULSEMODBUS, RICOMODBUS, 
	GENERICMODBUS, OWLMODBUS, SOLADINMODBUS, SEVENSEGMODBUS, METERMODBUS, RESOLMODBUS, SMAMODBUS, INVERTERMODBUS, THERMALMODBUS, SENSORMODBUS, 
	0};

void Config::dumpModbus() {
	int i;
	cerr << "Modbus: " << modbusSlots << " slots\n";
	for (i = 0; i < modbusSlots; i++)
		fprintf(stderr, "[%d] = %hu (%hd)\n", i, modbus[i], modbus[i]);
}

#define TCP_LEN (4)
#define TCP_UID (6)
#define TCP_CMD (7)
#define TCP_DATA (8)
/* Some confusion with Red Lion.  In theory Read Multiple Inputs is 4 not 3 but RedLion sends 3 to access data in the range 30000. 
   Turns out that it's pinging a Holding Register to see if the target (slave) is alive.
 */
#define CMD_RMI (3)  /* READ MULTIPLE INPUT REGISTERS */
#define CMD_RMR (4)	/* READ MULTIPLE HOLDING REGISTERS */

#define TCPBUFSIZE 256
int sock[MODBUSSOCKETS];
int socketsopen;
unsigned char tcpbuf[MODBUSSOCKETS][TCPBUFSIZE];
int bufPos[MODBUSSOCKETS];
int bytesLeft[MODBUSSOCKETS];
int modbusFD[MODBUSSOCKETS];

void releaseClient(int i);		// Close specific client
void closeSockets();			// Close all sockets
int portInit(int port);			// Open listen port
int acceptConn();				// Accept 
void processRead(int num);		// Build up message until complete
void processMessage(int num);	// Process it and reply.
void printPacket(unsigned char * data);	// Debugging output

typedef enum {
    EX_NONE = 0x00,
    EX_ILLEGAL_FUNCTION = 0x01,
    EX_ILLEGAL_DATA_ADDRESS = 0x02,
    EX_ILLEGAL_DATA_VALUE = 0x03,
    EX_SLAVE_DEVICE_FAILURE = 0x04,
    EX_ACKNOWLEDGE = 0x05,
    EX_SLAVE_BUSY = 0x06,
    EX_MEMORY_PARITY_ERROR = 0x08,
    EX_GATEWAY_PATH_FAILED = 0x0A,
    EX_GATEWAY_TGT_FAILED = 0x0B
} eMBException;

/**
 * NOTE - the vals[] passed in are NOT the vals belonging to the CurrentData class as they accumulate
 * It's the v[] array
*/

/**************/
/* INITMODBUS */
/**************/
void initModbus() {
	// This only gets called once
	// but if the portInit fails, it will get retried in handle_modbus
	
	int i;
	for (i  = 0; i < MODBUSSOCKETS; i++)
	{
		modbusFD[i] = -1;
	}
	
	// If the init failed, socketsopen will still be 0.
	socketsopen = 0;
	if (portInit(502) == 0);
}

void setModbusFD(fd_set *fd) {
	int i;
	for (i = 0; i < MODBUSSOCKETS; i++) if (modbusFD[i] != -1) FD_SET(modbusFD[i], fd);
}

/*****************/
/* HANDLE_MODBUS */
/*****************/
void handle_modbus(fd_set *fds){
	int i;
	if (0 == socketsopen)
		if(portInit(502) == 0)
		{
			ostringstream s;
			s << "event ERROR Failed to initialise port 502 in handle_modbus";
			journal.add(new Message(s.str()));
			return;
		}
	
	for (i = 1; i < MODBUSSOCKETS; i++)
		if ((modbusFD[i] != -1) && (FD_ISSET(modbusFD[i], fds))) 
		{
			MODBUSDEBUG fprintf(stderr, "\nSocket %d FD %d readable ", i, modbusFD[i]); 
			processRead(i);
		}
	// Is the order important? process readable sockets before accept()?
	if (modbusFD[0] != -1 && FD_ISSET(modbusFD[0], fds))
		acceptConn();
}

/****************/
/* PROCESS READ */
/****************/
void processRead(int num) {
	// Working with socket NUM.
	int retval, length;
	retval = read(modbusFD[num], &tcpbuf[num][bufPos[num]], bytesLeft[num]);
	DEBUG fprintf(stderr, "MODBUS read socket %d FD %d got %d bytes at bufpos %d ", num, modbusFD[num], retval, tcpbuf[num][bufPos[num]]);
	if (retval <= 0) {	// Maybe read = 0 isn't an error ?  Yes it is - it means close socket
		// But why does read() return 0 bytes when FD_ISSET says it's readable?
		releaseClient(num);
		return;
	}
	bufPos[num] += retval;
	bytesLeft[num] -= retval;
	if (bufPos[num] >= TCP_UID) {	// Got the header so can calculate remaining length
		length = (tcpbuf[num][TCP_LEN] << 8) | tcpbuf[num][TCP_LEN+1];
		MODBUSDEBUG fprintf(stderr, "MODBUS required packet length %d ", length);
		if (bufPos[num] < length + TCP_LEN) {
			bytesLeft[num] = length - bufPos[num] + TCP_UID;
			MODBUSDEBUG fprintf(stderr, "Bytesleft: %d ", bytesLeft[num]);
		}
		else 
			processMessage(num);
	}
	// Haven't got the entire header so go back until it becomes readable. Unlikely.
}

/*******************/
/* PROCESS MESSAGE */
/*******************/
void processMessage(int num) {
// Do whatever and build the return packet
	//
	// Bodge for RedLion - it pings Holding Register 1 so just need to implement a respinse to Holding Register.
	// For now, treat Inputs as if they were HOlding registers.
	
unsigned short start, len, responselen;
int i, retval;
	MODBUSDEBUG
	{
		fprintf(stderr, "Processmessage[%d]: ", num);
		printPacket(tcpbuf[num]);
		for (i = 0; i < bufPos[num]; i++) fprintf(stderr," %02x ", tcpbuf[num][i]);
	}
	if (tcpbuf[num][TCP_CMD] != CMD_RMI && tcpbuf[num][TCP_CMD] != CMD_RMR) // Only handle Read Multiple Inputs
	{	
		tcpbuf[num][TCP_LEN] = 0;
		tcpbuf[num][TCP_LEN+1] = 3;
		tcpbuf[num][TCP_CMD] |= 0x80;
		tcpbuf[num][TCP_CMD+1] = EX_ILLEGAL_FUNCTION;
		DEBUG fprintf(stderr, "ILLEGAL FUNCTION %d len=%d ", tcpbuf[num][TCP_CMD], TCP_UID+3);
		retval = write(modbusFD[num], tcpbuf[num], TCP_UID+3); 
		bufPos[num] = 0;
		bytesLeft[num] = TCPBUFSIZE;
		MODBUSDEBUG for (i = 0; i < retval; i++) fprintf(stderr, "%02x ", tcpbuf[num][i]);
		MODBUSDEBUG fprintf(stderr,"write returned %d\n", retval);
		return;
	}
	start = (tcpbuf[num][8] << 8) | tcpbuf[num][9];
	len = (tcpbuf[num][10] << 8) | tcpbuf[num][11];
	MODBUSDEBUG fprintf(stderr, "Got Start=%d Len=%d ", start, len);
	if (start > conf.modbusSlots - 1 || start + len > conf.modbusSlots) // Illegal request
	{	
		tcpbuf[num][TCP_LEN] = 0;
		tcpbuf[num][TCP_LEN+1] = 3;
		tcpbuf[num][TCP_CMD] |= 0x80;
		tcpbuf[num][TCP_LEN+4] = EX_ILLEGAL_DATA_ADDRESS;
		DEBUG fprintf(stderr, "ILLEGAL DATA ACCESS len=%d ", TCP_UID+3);
		retval = write(modbusFD[num], tcpbuf[num], TCP_UID+3); 
		bufPos[num] = 0;
		bytesLeft[num] = TCPBUFSIZE;
		MODBUSDEBUG for (i = 0; i < retval; i++) fprintf(stderr, "%02x ", tcpbuf[num][i]);
		MODBUSDEBUG fprintf(stderr,"write returned %d\n", retval);
		return;
	}
	DEBUG fprintf(stderr, " READ from %d for %d\n", start, len); 
	// Valid response
	responselen = len * 2 + 3;
	tcpbuf[num][TCP_LEN] = responselen >> 8;
	tcpbuf[num][TCP_LEN+1] = responselen & 0xFF;
	tcpbuf[num][TCP_DATA] = len * 2;	// Byte count
	for (i = 0; i < len; i++)
	{
		tcpbuf[num][i * 2 + TCP_DATA + 1] = conf.modbus[start + i] >> 8;
		tcpbuf[num][i * 2 + TCP_DATA + 2] = conf.modbus[start + i] & 0xFF;
	}
	MODBUSDEBUG fprintf(stderr, "Valid Response len=%d ", responselen + 6);
	retval = write(modbusFD[num], tcpbuf[num], responselen + 6);
	bufPos[num] = 0;
	bytesLeft[num] = TCPBUFSIZE;
	MODBUSDEBUG fprintf(stderr,"Write sent %d bytes\n", retval);
}

/****************/
/* PRINT PACKET */
/****************/
void printPacket(unsigned char * data)
{
	unsigned short txid, prot, len;
	txid = (data[0] << 8) + data[1];
	prot = (data[2] << 8) + data[3];
	len =  (data[4] << 8) + data[5];
	fprintf(stderr ,"MODBUS Packet TXID %d Prot %d Len %d Unit %d CMD %d ", txid, prot, len, data[6], data[7]);
	for (int i = 2; i < len; i += 2)
		fprintf(stderr, "%d ", data[i + 6] << 8 + data[i + 7]);
	fprintf(stderr, "\n");
}

/******************/
/* RELEASE CLIENT */
/******************/
void releaseClient(int num)
{
	int ret;
	ret = read(modbusFD[num], &tcpbuf, BUFSIZE);
	MODBUSDEBUG fprintf(stderr," Releaseclient %d discarding %d bytes ", num, ret);
	close(modbusFD[num]);
	modbusFD[num] = -1;
	socketsopen--;
	MODBUSDEBUG fprintf(stderr, "Socketsopen now %d\n", socketsopen);
}
	
/** *********** 
 * CLOSESOCKETS
 */
void closeSockets()
{
	int i;
	for (i = 0; i < MODBUSSOCKETS; i++)
		if (modbusFD[i] != -1) 
		{
			read(modbusFD[i], &tcpbuf, TCPBUFSIZE);
			close(modbusFD[i]);
		}
}

/************/
/* PORTINIT */
/************/
int portInit(int port)
{
	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);
	if ((modbusFD[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) 
	{
		std::stringstream s;
		s << "event ERROR MODBUS Socket creation failed " << strerror(errno);
		journal.add(new Message(s.str()));
		return 0;	// FAILURE
	}
	if ((bind(modbusFD[0], (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)) 
	{
		std::stringstream s;
		s << "event ERROR MODBUS Bind socket failed " << strerror(errno);
		journal.add(new Message(s.str()));
		return 0;	// FAILURE
	}
	if (listen(modbusFD[0], 5) == -1) 
	{
		std::stringstream s;
		s << "event ERROR MODBUS Listen socket failed" << strerror(errno);
		journal.add(new Message(s.str()));
		return 0;	// FAILURE
	}
	socketsopen = 1;
	MODBUSDEBUG fprintf(stderr,"portInit on FD %d ", modbusFD[0]);
	return 1;	// SUCCESS
}

/**************/
/* ACCEPTCONN */
/**************/
int acceptConn() 
{
	int i;
	MODBUSDEBUG fprintf(stderr, "Acceptconn..");
	if (socketsopen == MODBUSSOCKETS) 
	{
		MODBUSDEBUG fprintf(stderr, "All sockets open");
		sleep(1);
		return 0;	// FAILURE
	}
	
	for (i = 1; i < MODBUSSOCKETS; i++) 
		if (modbusFD[i] == -1) break;
	if (i == MODBUSSOCKETS)
	{
		MODBUSDEBUG fprintf(stderr, "ERROR can't find empty slot athough socketsopen = %d and MODBUSSOCKETS = %d\n", socketsopen, MODBUSSOCKETS);
		sleep(1);
		return 0;	// FAILURE
	}
	
	if ((modbusFD[i] = accept(modbusFD[0], NULL, NULL)) == -1) 
	{
		MODBUSDEBUG fprintf(stderr, "Accept on slot %d failed", i);
		return 0;	// FAILURE
	}
	// See if it's blocking or ont.
	int flags = fcntl(modbusFD[i], F_GETFL, 0);
	MODBUSDEBUG if (flags & O_NONBLOCK) 
		fprintf(stderr,"FD %d is NON blocking ", modbusFD[i]);
	socketsopen++;
	MODBUSDEBUG fprintf(stderr,"Socketopen[%d]=%d FD=%d\n", i, socketsopen, modbusFD[i]);
	bytesLeft[i] = TCPBUFSIZE;
	bufPos[i] = 0;
	return 1;		// SUCCESS
}

void Steca::modbus() const {
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Steca::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)vals[0];				// SOC
	conf.modbus[m++] = (short)(vals[1] * 100);		// Ibatt
	conf.modbus[m++] = (short)(vals[2] * 10);		// Vbatt
	conf.modbus[m++] = (short)vals[3];				// Temp 1
	conf.modbus[m++] = (short)vals[4];				// Temp 2
	conf.modbus[m++] = (short)vals[5];				// Temp 3
	conf.modbus[m++] = (short)vals[6];				// Temp 4
	conf.modbus[m++] = (short)(vals[7] * 10);		// Control Volts
	conf.modbus[m++] = (short)(vals[8] * 100);		// Module current
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

void Victron::modbus() const {
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Victron::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)(vals[0] * 10);		// Mains Volts
	conf.modbus[m++] = (short)(vals[1] * 100);		// Mains Current
	conf.modbus[m++] = (short)(vals[2] * 10);		// Inverter Volts
	conf.modbus[m++] = (short)(vals[3] * 100);		// Inverter Current
	conf.modbus[m++] = (short)(vals[4] * 10);		// Battery voltage
	conf.modbus[m++] = (short)(vals[5] * 100);		// Battery Current
	conf.modbus[m++] = (short)(vals[6] * 10);		// Battery Ripple
	conf.modbus[m++] = (short)(vals[7] * 100);		// Mains Load Current
	pthread_mutex_unlock(&mutexModbus);
	// END MUTEX
}

void Turbine::modbus() const {
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Turbine::modbus at slot [%d]=%d ", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)(vals[0] * 100);
	conf.modbus[m++] = (short)(vals[1] * 10);
	// END MUTEX
	DEBUG fprintf(stderr, "[%d]=%d [%d]=%d\n", m-2, conf.modbus[m-2], m-1, conf.modbus[m-1]);
	pthread_mutex_unlock(&mutexModbus);
}
void Fronius::modbus() const {
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Fronius::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)vals[0];					// Watts now
	conf.modbus[m++] = (short)(int)vals[1] & 0xFFFF;	// Energy Total low word
	conf.modbus[m++] = (short)((int)vals[1] >> 16);	// Energy Total high word
	conf.modbus[m++] = (short)(int)vals[2] & 0xFFFF;	// Energy Today low word
	conf.modbus[m++] = (short)((int)vals[2] >> 16);	// Energy Today high word
	conf.modbus[m++] = (short)(int)vals[3] & 0xFFFF;	// Energy Year low word
	conf.modbus[m++] = (short)((int)vals[3] >> 16);	// Energy Year high word
	conf.modbus[m++] = (short)(vals[4] * 100);			// Mains current
	conf.modbus[m++] = (short)(vals[5] * 10);			// Mains Volts
	conf.modbus[m++] = (short)(vals[6] * 100);			// Mains frequency
	conf.modbus[m++] = (short)(vals[7] * 100);			// DC Current
	conf.modbus[m++] = (short)(vals[8] * 10);			// DC VOlts
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

void Davis::modbus() const {
fprintf(stderr, "ERROR Davis::modbus() called\n");		// Not used. But must exist as it's pure virtual
// The real function is in davis.cpp
}

void Pulse::modbus() const {
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Pulse::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)(vals[0]) & 0xFFFF;			// In Pulses
	conf.modbus[m++] = (short)(vals[1]) & 0xFFFF;			// OutPulses
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

void Rico::modbus() const {}		// No values - do nothing
void Generic::modbus() const {}	// No values - do nothing
void Owl::modbus() const {}
void Soladin::modbus() const
{
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Soladin::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)(vals[0] * 10);			// DC Volts
	conf.modbus[m++] = (short)(vals[1] * 100);			// DC Current
	conf.modbus[m++] = (short)(vals[2] * 100);			// Mains frequency
	conf.modbus[m++] = (short)(vals[3] * 10);			// Mains Volts
	conf.modbus[m++] = (short)(vals[4]);				// Power watts
	conf.modbus[m++] = (short)(int)vals[5] & 0xFFFF;	// Energy Total low word
	conf.modbus[m++] = (short)((int)vals[5] >> 16);	// Energy Total high word
	conf.modbus[m++] = (short)vals[6];					// Temperature
	conf.modbus[m++] = (short)vals[7];					// Operating hours
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

void SevenSeg::modbus() const {}	// No values - do nothing

// Nasty fiddle - although there is a Meter object in CurrentData, it's not used.  Call from within handle_meter().
void Meter::staticmodbus(int device, float vals[]) {
	int m;
	m = conf[device].slot;
	if (!conf.bModbus()) return;
	DEBUG fprintf(stderr, " Meter::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)(int)(vals[0]) & 0xFFFF;	// Import  low word
	conf.modbus[m++] = (short)((int)vals[0] >> 16);	// Import high word
	conf.modbus[m++] = (short)(int)vals[1] & 0xFFFF;	// Export low word
	conf.modbus[m++] = (short)((int)vals[1] >> 16);	// Export high word
	pthread_mutex_unlock(&mutexModbus);
	// END MUTEX
}

void Resol::modbus() const
{
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Resol::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)(int)vals[0] & 0xFFFF;	// Energy low word
	conf.modbus[m++] = (short)((int)vals[0] >> 16);		// Energy high word
	conf.modbus[m++] = (short)vals[2];		// Sensor 1
	conf.modbus[m++] = (short)vals[3];		// Sensor 2
	conf.modbus[m++] = (short)vals[4];		// sensor 3
	conf.modbus[m++] = (short)vals[5];		// Sensor 4
	conf.modbus[m++] = (short)vals[6];		// Relay bitmask
	pthread_mutex_unlock(&mutexModbus);
	// END MUTEX
}

void Sma::modbus() const 
{
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Sma::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)vals[0];		// Power Now
	conf.modbus[m++] = (short)(int)vals[1] & 0xFFFF;	// Energy low word
	conf.modbus[m++] = (short)((int)vals[1] >> 16);		// Energy high word
	conf.modbus[m++] = (short)(vals[2] * 100);		// Mains current
	conf.modbus[m++] = (short)(vals[3] * 10);		// Mains Volts
	conf.modbus[m++] = (short)(vals[4] * 100);		// Herz
	conf.modbus[m++] = (short)(vals[5] * 100);		// DC amps
	conf.modbus[m++] = (short)(vals[6] * 10);		// DC Volts
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

/**
 * Inverter::Modbus
 */

void Inverter::modbus() const 
{
	int m;
	m = conf[device].slot;
	MODBUSDEBUG 
	{
		fprintf(stderr, " Inverter::modbus at slot [%d]=%d count %d\n", m, conf.modbus[m]+1, count);
		for (int i = 0; i < numvals; i++)
			fprintf(stderr, "V[%d] = %f ", i, vals[i]);
	}
	if (count == 0) 
	{
		DEBUG fprintf(stderr, "No data for this device[%d] - not updating Modbus\n", device);
		return;
	}
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = static_cast<unsigned short int>(vals[0] * 10.0 / count);		// Power Now
	unsigned int kwh = static_cast<unsigned int>(vals[3] * 10.0 / count);
	conf.modbus[m++] = static_cast<unsigned short int>(kwh & 0xFFFF);	// Energy low word
	conf.modbus[m++] = static_cast<unsigned short int>(kwh >> 16);	// Energy high word
	conf.modbus[m++] = static_cast<unsigned short int>(vals[4] * 100.0 / count);		// Mains current
	conf.modbus[m++] = static_cast<unsigned short int>(vals[7] * 10.0 / count);		// Mains Volts
	conf.modbus[m++] = static_cast<unsigned short int>(vals[10] * 100.0 / count);		// Herz
	conf.modbus[m++] = static_cast<unsigned short int>(vals[13] * 100.0 / count);		// DC amps
	conf.modbus[m++] = static_cast<unsigned short int>(vals[15] * 10.0 / count);		// DC Volts
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
	MODBUSDEBUG 
		for (int i = conf[device].slot; i < m; i++)
			fprintf(stderr, "%d (%02x) ", conf.modbus[i], conf.modbus[i]);
}

void Thermal::modbus() const 
{
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Thermal::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = static_cast<unsigned short int>(vals[0]);		// Power Now
	conf.modbus[m++] = static_cast<unsigned short int>((int)(vals[1]) & 0xFFFF);	// Energy low word
	conf.modbus[m++] = static_cast<unsigned short int>((int)(vals[1]) >> 16);		// Energy high word
	conf.modbus[m++] = static_cast<unsigned short int>(vals[2] * 100);		// Mains current
	conf.modbus[m++] = static_cast<unsigned short int>(vals[3] * 10);		// Mains Volts
	conf.modbus[m++] = static_cast<unsigned short int>(vals[4] * 100);		// Herz
	conf.modbus[m++] = static_cast<unsigned short int>(vals[5] * 100);		// DC amps
	conf.modbus[m++] = static_cast<unsigned short int>(vals[6] * 10);		// DC Volts
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

void Sensor::modbus() const
{
	int m;
	m = conf[device].slot;
	DEBUG fprintf(stderr, " Sensor::modbus at slot [%d]=%d\n", m, conf.modbus[m]+1);
	// MUTEX on modbus
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// reference count
	conf.modbus[m++] = (short)vals[0];		// Irraditiation in W/m2
	conf.modbus[m++] = (short)(vals[1] * 10);	// Paneltemp x 10 C
	conf.modbus[m++] = (short)(vals[2] * 10);		// Ambient x 10 C
	conf.modbus[m++] = (short)(vals[3] * 10);		// Extra temp x 10 C
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
}

void Current::modbus(float watts, float kwh) const
{
	MODBUSDEBUG fprintf(stderr, "Current::modbus at slot 1 watts %f kwh %f Count %d\n", watts, kwh, conf.modbus[2]);
	int m = 2;
	pthread_mutex_lock(&mutexModbus);
	conf.modbus[m++]++;	// sample count
	conf.modbus[m++] = (unsigned short)(watts * 10.0);		// Watts / 10
	int Ikwh = static_cast<int>(kwh * 10.0);
	conf.modbus[m++] = (int)(Ikwh) & 0xFFFF;
	conf.modbus[m++] = (int)(Ikwh) >> 16;
	DEBUG fprintf(stderr, "KWH %d %x .. %d %x\n", conf.modbus[m-2], conf.modbus[m-2], conf.modbus[m-1], conf.modbus[m-1]);
	
	if (2 + FIXEDMODBUS != m)
		fprintf(stderr, "ERROR Modbus index is %d should be %d\n", m, FIXEDMODBUS + 2);
	// END MUTEX
	pthread_mutex_unlock(&mutexModbus);
				  
}

