#Modify this to point to the PJSIP location.
PJBASE=/usr/local/src/svn/pjproject

include $(PJBASE)/build.mak

CC      = $(APP_CC)
LDFLAGS = $(APP_LDFLAGS)
LDLIBS  = -lstdc++ $(APP_LDLIBS)
CFLAGS  = $(APP_CFLAGS)

# CFLAGS is APP_CFLAGS that comes from /user/local/src/svn/pjproject. In it, we have -02. So since we want to disable optimization by setting -O0
# we set -O0 before and after it to ensure -O0 has the final say.
CPPFLAGS= -g -O0 ${CFLAGS} $(LDFLAGS) -O0 -I /usr/local/src/boost_1_51_0 -I pjmedia/include/chainlink

VPATH = pjmedia

.PHONY : clean install

all: sip.so

chainlink:
	make -C pjmedia

idmanager.o: idmanager.cpp idmanager.hpp
	$(CC) -fPIC -c -o idmanager.o idmanager.cpp $(CPPFLAGS)

packetdumper.o: packetdumper.cpp packetdumper.hpp
	$(CC) -fPIC -c -o packetdumper.o packetdumper.cpp $(CPPFLAGS)

event_templates.o: event_templates.cpp event_templates.hpp
	$(CC) -fPIC -c -o event_templates.o event_templates.cpp $(CPPFLAGS)

sip.o: sip.cpp sip.hpp
	$(CC) -fPIC -c -o sip.o sip.cpp $(CPPFLAGS)

sip.so: sip.o idmanager.o packetdumper.o event_templates.o chainlink
	$(CC) -fPIC -shared -o sip.so sip.o idmanager.o packetdumper.o event_templates.o pjmedia/libchainlink.a 3rdParty/spandsp/src/.libs/libspandsp.a -Wl,-E -lm -ldl -ltiff -lpcap $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o *.so
	make -C pjmedia clean

