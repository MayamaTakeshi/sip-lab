#Modify this to point to the PJSIP location.
PJBASE=/usr/local/src/svn/pjproject

include $(PJBASE)/build.mak

CC      = g++
LDFLAGS = $(APP_LDFLAGS)
LDLIBS  = $(APP_LDLIBS)
CFLAGS  = $(APP_CFLAGS)
CPPFLAGS= -O0 -g -fPIC -I include/chainlink ${CFLAGS} 

CHAINLINK_CPPFLAGS = -I include/chainlink 

vpath %.c src/chainlink
vpath %.h include/chainlink

all: libchainlink.a

#libchainlink.a: chainlink_wire_port.o chainlink_dtmfdet.o chainlink_tonegen.o chainlink_wav_player.o
#libchainlink.a: chainlink_wire_port.o chainlink_dtmfdet.o chainlink_tonegen.o
libchainlink.a: chainlink_wire_port.o chainlink_dtmfdet.o chainlink_wav_player.o chainlink_wav_writer.o chainlink_tonegen.o
	ar rcs $@ $^

chainlink_wire_port.o: 

chainlink_dtmfdet.o: 

chainlink_wav_player.o: 

chainlink_wav_writer.o: 

chainlink_tonegen.o:

clean:
	rm -f *.o
	rm -f *.a

