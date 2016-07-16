/*
 *  mcp.h
 *  mcp1.0
 *
 *  Created by Martin Robinson on 01/10/2006.
 *  Copyright 2006,2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010, 2011, 2012 Wattsure Ltd. All rights reserved.
 *
 * This should be included in every .CPP file as it sets constants and defines.
 */

#include <string>
	using std::string;
#include <iostream>
	using std::cerr; using std::endl; using std::cin; using std::cout;
	using std::hex;  using std::dec;
	using std::ostream;
#include <list>
	using std::list;
#include <sstream>
	using std::ostringstream;
#include <fstream>
	using std::ofstream;
	
const int MAXDEVICES = 45;	// Entries in config file
const int MAXUNITS = 25;	// units per device (inverters on daisychain)
const int MAXSOCKETS = MAXDEVICES + 2 * MAXUNITS;
// ADD CONTROLLER
enum DeviceType {noDevice, steca_t, victron_t, turbine_t, fronius_t, davis_t, pulse_t, rico_t, 
	generic_t, owl_t, soladin_t, sevenseg_t, meter_t, resol_t, sma_t, inverter_t, thermal_t, sensor_t};
enum ModelType {modelNormal, Victron16A, Victron30A, IG30, vantage_model, solar_model, wind_model, cylinder_model,
	solasyphon_model, pool_model};
enum MeterType {noMeter = -1, none, Eload, Emains, Esolar, Ewind, Ethermal, Eexport, GenImp, GenExp};
enum SystemType {storage_t, hybrid_t, grid_t};

// This is used to allocate an array indexed by MeterType so needs to be the exact number of entries in the enum
#define MAXMETER 9
extern int debug;

// 14/03/2007 1.1.6 Startup file handling
// 14/03/2007 1.1.7 Handle spurious zero SOC
// 15/03/2007 1.1.8 Handle Off to InverterOff transition
// 22/03/2007 1.1.9 Version startup message; console socket close; no .txt file
// 23/03/2007 1.1.10 Turbine controller has 2 data values
// 03/04/2007 1.2.0  Support for Fronius
// 22/04/2007 1.2.1 Support for multiple Stecas.
// 27/04/2007 1.2.2 Corrupt download.xml now moved out of the way
// 11/05/2007 1.2.3 Disable bulk protection
// 10/06/2007 1.3.0 Use new Victron commands. Report Mains status. Change to ChargetoSOC. 
// 13/10/2007 1.3.1 New state StecaError. New attribute MinBatt in charging.
// 27/10/2007 1.3.2 Uses parallel attrribute for Victrons
// 06/07/2007 1.3.3 Improved SIGCHLD handling from APUE
// 08/07/2007 1.2 Initial conversion to RCS
// 20/08/2007 1.3 Initial Davis Support
// 21/09/2007 1.4 Events now scheduled to exact multiples. 
// 25/09/2007 1.5 FIFO on /var/lock/command and result to support commands
// 01/10/2007 1.6 Meter support
// 05/10/2007 1.7 Fixed core dump on invalid controller number
// 14/10/2007     Change from Controller to Device everywhere including XML
// 18/11/2007 1.8 Change to StecaError behaviour to start charging
// 18/11/2007 1.10 setvar sends messages
// 20/11/2007 1.11 Elster support   
// 22/11/2007 1.12 Variables can set relatively
// 23/11/2007 1.13 Fixed crash when journal is empty
// 25/11/2007 1.14 Bug in state logic - fell through from Recovery to Normal leaving charger on.
// 28/11/2007 1.15 Fixed bug where <pulse> causes crash
// 20/12/2008 1.16 Meter (Elster) support
// 22/12/2007 1.17 /tmp/vars.dat written out.
// 13/02/2008 1.18 New devices: rico and generic 
// 23/05/2008 1.19 Fronius does not report WLoad, only WSolar. Add <capabilities>FroniusNoLoad</capabliities> to config.xml
// 26/06/2008 1.20 Owl support as 'data 3 x.x y.y z.z for the threee phases
// 26/06/2008 1.21 Warnings on VBatt < var.minBatt
// 26/06/2008 1.22 Schedule DataUpload for 1 second after Consolidation
// 13/07/2008 1.23 Report battery voltage if less than minBatt
// 01/08/2008 1.24 Soladin (MAstervolt) support. Change FroniusNoLoad to InverterNoLoad as it applies to Soladn as well.
// 21/08/2008 1.25 MAXDEVICES=10 and <device start="no">
// 11/09/2008 1.26 Tidied up state change message; added ChargeLimit to Show Var 
// 25/09/2008 1.27 start="no" written out using commit
// 26/09/2008 1.28 Based on victron logon message, alter the way /tmp/led.dat is written in ProcessLEDMessage
// 27/10/2008 1.29 SevenSegment support - very similar to Rico
/* 09/11/2008 1.30 Loads added. First deployment at Okehampton.  
	- Handle meter 1 and meter 2.
	- Fixed readCMOS & writeCMOS
	- Supprot for SevenSeg trailer display
	- added <emains> <esolar> to <data>
	- Console commands: save FILENAME, load FILENAME
	- Adde Meter device type, change ElsterVal to MeterVal
	- Added <inverters> tag for inverter drill down
*/
// 12/11/2008 1.31 Can now set lines in config file.  Added Resol device. Added <meter power="yes|no" to <device>. 
// Changes to meter device. Drop Elster. Changed MeterVal to MeterEntry.
// 17/11/2008 1.32 Added systemtype=storage, hybrid to allow different rules
// 23/11/2008 1.33 Fixed bug in state change logic
// 25/11/2008 1.34 Fixed inverteroff bug in state change logic.
/* 08/12/2008 1.35 Fixed bug in state change: charger is not turned off initially.
	- EExport derived if Emains has two values.
	- WExport derived if Emains has Power = yes.
	- SolarHighwater maintained in NVRAM
	- SolarIncMax in <mcp> limits rise of SolarHighWater
	- Variables $site, $time, $date, $timestamp can be used in <command>, <get> and <put>
	- Initial upload within 10 seconds of program start
	- use of ack.xml to update lastupload if older than processlag.
*/
// 06/02/2009 1.36 Commit all above work.
// 11/02/2009 1.37 Add threshold 'maintain' in hybrid mode. Bugfix to not overwrite wload when a Victron is in use
// 22/03/2009 1.38 SMA Support
// 13/04/2009 1.39 Modbus support + bugfix for crash with no debug
// 07/05/2009 1.40 Bugfix for Meter/Modbus interaction
// 12/07/2009 1.41 Addition of windhighwater as for solarhighwater. Replaced solarincmax with highwaterincmax
// 24/07/2009 1.42 Retaining LastActivity time in NVRAM; report at startup.
// 13/09/2009 1.43 Multiple Rico displays via <display> tag
// 22/09/2009 1.44 Bugfix: FTPGET no longer deletes files. Username and password can be set.
// 07/01/2010 1.45 Introduction of type=inverter|thermal to support Data Dictionary device programs.
//					Addition of power="123" attribute to inverter tag, and command 'N set power 123' as well as 'N set power yes|no'
// 08/03/2010 1.46 Message tidy up and increase MAXDEVICES to 16 in anticipation of Chingford. Remove NVRAM storage of HighWater.
// 19/05/2010 1.47 Added <thermal> tag to output and model="cylinder|solasyphon|pool" to type="thermal"
	// And model field to show connection, set XX model command.

// 23/05/2010 2.0 Added platform detection. Remove NVRAM. Blink Green LED off while running.
// 01/07/2010 2.1 Backport - will output <thermal> tags given resol input, not just thermal
// 2010/07/16 2.2 Add more fields to <thermal>: tst2l, tst2h, extra1, extra2.
// 2010/08/06 2.3 Add support for Generic and multiline FIFO input
// 2010/09/12 2.4 Secure: listen on localhost only to 10010 and 10011.  Revert to previous behaviour with -i
// 2010/10/01 2.5 Support for Messages: /var/lock/command world writeable. And /tmp/deviceXX.txt files written
// 2010/11/21 2.6 Support for <mcp model="mk1|mk2mini|mk2midi|mk2rs485"> and resistance to change siteid during reload
// 2011/03/28 2.7 Addition of <sensor> to config and output. Add ambient etc to <data> row
// 2011/05/26 2.8 Removed <sensor> and <rawdata> from output. Improved meter handling.  Davis now populates irradiation and temp (untested).
//			<meter>Esolar|EWind</meter> now populates properly. Debug level can be entered as 0x...
// 2011-07/04 2.9 Bugfix - removed 0x.. as it causes problems parsing 09 as an hour (illegal octal sequence)
// 2011/07/17 2.10 Add units to devices. For inverter only.  Altered restart command.  MAXDEVICES = 45 CONSOLEBUFFERSIZE=8192
// 2011/09/28 3.0 Major release. Subunits. Set server and comms. MCP responds to version command from clients
// 2011/12/08 3.1 Use of capacity= to limit output values per inverter and <mcp capacity=> for sitewide version.
// 2012/02/02 3.2 Added GenImp and GenImp to Meter definitions
// 2012/04/05 3.3 if there is no DataUpload in the queue, complain and add one. 1st upload changed to 90 seconds.  
//				  Display username and password if set.
// 2012/08/11 3.4 Tested Modbus
// 2013/04/07 3.5 Added Rapid Updates to var.dat and modbus if exactly and only one inverter is configured.
//				  AND changed comparison on Journal::ml to try and avoid duplicate rows.

#define MCPVERSION "$Id: mcp.h,v 3.5 2013/04/08 20:20:55 martin Exp $"
#define REVISION "$Revision: 3.5 $"

#define DEBUG if(debug & 1)
#define CONFIGDEBUG if(debug & 2)
#define SOCKETDEBUG if(debug & 4)
#define MEMDEBUG if(debug & 8)
#define QUEUEDEBUG if(debug & 16)
#define STATUSDEBUG if(debug & 32)
#define DETAILDEBUG if(debug & 64)
#define STATEDEBUG if (debug & 128)
#define DAVISDEBUG if (debug & 256)
#define MODBUSDEBUG if (debug & 512)

#define TEXTSIZE 256
#define BUFSIZE 1024
// cf. BUFSIZE in console.c
#define CONSOLEBUFSIZE 8192

#define MAXVALS 26
// ADD CONTROLLER
#define FIXEDMODBUS 4	/* CALCDATA items */
#define VICTRONVALS 8
#define VICTRONMODBUS 8
#define STECAVALS 9
#define STECAMODBUS 9
#define TURBINEVALS 2
#define TURBINEMODBUS 2
#define FRONIUSVALS 9
#define FRONIUSMODBUS 12
#define DAVISVALS 0
#define DAVISMODBUS 27 
#define PULSEVALS 2
#define PULSEMODBUS 2
#define RICOVALS 0
#define RICOMODBUS 0
#define GENERICVALS 0
#define GENERICMODBUS 9
#define OWLVALS 3
#define OWLMODBUS 1
#define SOLADINVALS 8
#define SOLADINMODBUS 9
#define SEVENSEGVALS 0
#define SEVENSEGMODBUS 0
#define METERVALS 0
#define METERMODBUS 5
#define RESOLVALS 6
#define RESOLMODBUS 7
#define RESOLNAMES {"kwh", "col", "tst", "tstu", "trf", "n1", 0}
#define SMAVALS 9
#define SMAMODBUS 8
#define INVERTERVALS 26
#define INVERTERMODBUS 9
#define INVERTERNAMES {"watts", "wdc", "wdc2", "kwh", "iac", "iac2", "iac3", "vac", "vac2", "vac3", "hz", "hz2", \
			"hz3", "idc", "idc2", "vdc", "vdc2", "ophr", "t1", "t2", "t3", "t4", "fan1", "fan2", "fan3", \
			"fan4", 0}
#define THERMALVALS 14
#define THERMALMODBUS 14
#define THERMALNAMES {"kwh", "col", "col2", "tst", "tstu", "tstl", "trf", "n1", "n2", "ophr", \
			"tst2l", "tst2u", "extra1", "extra2", 0}
#define SENSORVALS 4
#define SENSORMODBUS 4
#define SENSORNAMES {"irradiation", "paneltemp", "ambient", "extra1", 0}

#define STARTUPXML "/root/startup.xml"
#define FIFO_COMMAND "/var/lock/command"
#define FIFO_RESULT "/var/lock/result"

// NVRAM locations
#define NVRAM_PulseCount	(100)
#define NVRAM_SolarHighWater (96)	/* Not used since 1.46 */
#define NVRAM_LastEq (92)
#define NVRAM_WindHighWater (88)	/* Not used since 1.46 */
#define NVRAM_LastActivity (84)

void catcher(int sig);  // Signal catcher to prevent zombies (defined in mcp.cpp)
time_t timeMod(time_t t, int offset);
void handle_meter(const int contr, const char * msg);		// in meter.cpp

class Config;	// Forward declaration
