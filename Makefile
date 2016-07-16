# Makefile for the MCP program
# Subtleties:
# With the introduction of common.c and sbus.c, it is necessary to use g++ for compiling
# both C and C++ files otherwise they won't link.  But Make tries to use gcc for C files.
# Use -NDEBUG to remove assert statements. It helps with static code analysis.

CC=$(CROSSTOOL)/$(ARM)/bin/g++
CXX=$(CROSSTOOL)/$(ARM)/bin/g++
CXXOBJS=action.o command.o comms.o config.o davis.o expatpp.o journal.o mcp.o meter.o response.o modbus.o
COBJS=common.o sbus.o
CFLAGS=-I../expat-arm/xmlparse -L../expat-arm/xmlparse -DNDEBUG
CXXFLAGS=$(CFLAGS)

all: mcp.new

mcp.new: $(CXXOBJS) $(COBJS)
	$(CXX) -o mcp.new $(COBJS) $(CXXOBJS) $(CFLAGS) -lexpat -lstdc++
	$(CROSSTOOL)/$(ARM)/bin/strip mcp.new

action.o: action.cpp mcp.h config.h expatpp.h xmlparse.h journal.h \
  davis.h action.h response.h command.h
command.o: command.cpp mcp.h command.h config.h expatpp.h xmlparse.h \
  journal.h davis.h action.h comms.h
comms.o: comms.cpp mcp.h comms.h config.h expatpp.h xmlparse.h
config.o: config.cpp mcp.h config.h expatpp.h xmlparse.h journal.h \
  davis.h action.h command.h
davis.o: davis.cpp mcp.h config.h expatpp.h xmlparse.h davis.h journal.h
expatpp.o: expatpp.cpp expatpp.h xmlparse.h
journal.o: journal.cpp mcp.h config.h expatpp.h xmlparse.h journal.h \
  davis.h comms.h action.h command.h
mcp.o: mcp.cpp mcp.h expatpp.h xmlparse.h config.h journal.h davis.h \
  comms.h action.h response.h command.h
meter.o: meter.cpp meter.h mcp.h journal.h config.h
response.o: response.cpp mcp.h config.h expatpp.h xmlparse.h journal.h \
  davis.h action.h command.h response.h
modbus.o: modbus.cpp modbus.h mcp.h journal.h config.h
# $(COBJS): %.o: %.c
#	$(CXX) -c $(CFLAGS) $< -o $@

clean:
	rm -f mcp.new $(COBJS) $(CXXOBJS)
