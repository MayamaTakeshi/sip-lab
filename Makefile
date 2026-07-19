CXX      ?= g++
CC       ?= gcc
AR       ?= ar

BUILD    ?= debug
OUTDIR   := build/$(BUILD)

TARGET   := sip_lab_server

# Sources shared by all targets (from binding.gyp all-settings)
SHARED_SRCS := \
	src/log.cpp \
	src/event_templates.cpp \
	src/idmanager.cpp \
	src/sip.cpp \
	src/pjsip/src/pjsip/sip_transport_ws.c \
	src/pjmedia/src/pjmedia/dtmfdet.c \
	src/pjmedia/src/pjmedia/bfsk_det.c \
	src/pjmedia/src/pjmedia/bfsk_det2.c \
	src/pjmedia/src/pjmedia/fax_port.c \
	src/pjmedia/src/pjmedia/flite_port.c \
	src/pjmedia/src/pjmedia/pocketsphinx_port.c \
	src/pjmedia/src/pjmedia/ws_speech_port.cpp \
	3rdParty/pjwebsock/websock/http.c \
	3rdParty/pjwebsock/websock/websock_transport_tcp.c \
	3rdParty/pjwebsock/websock/websock_transport_tls.c \
	3rdParty/pjwebsock/websock/websock.c \
	3rdParty/pjwebsock/websock/websock_transport.c

# Server-specific sources
SERVER_SRCS := $(SHARED_SRCS) src/server.cpp

SERVER_OBJS := $(patsubst %.cpp,$(OUTDIR)/%.o,$(patsubst %.c,$(OUTDIR)/%.o,$(SERVER_SRCS)))

# Include directories (same as binding.gyp all-settings)
INCLUDES := \
	-I3rdParty/pjproject/pjsip/include \
	-I3rdParty/pjproject/pjlib/include \
	-I3rdParty/pjproject/pjlib-util/include \
	-I3rdParty/pjproject/pjnath/include \
	-I3rdParty/pjproject/pjmedia/include \
	-Iinclude \
	-Isrc \
	-Isrc/pjmedia/include \
	-Isrc/pjmedia/include/pjmedia \
	-Isrc/pjsip/include \
	-I3rdParty/rapidjson/include \
	-I3rdParty/boost_1_66_0 \
	-I3rdParty/spandsp/src \
	-I3rdParty/pocketsphinx/include \
	-I3rdParty/pocketsphinx/build/include \
	-I3rdParty/pjwebsock/websock

# Compiler flags (from binding.gyp, without NAPI_VERSION)
CFLAGS_COMMON := -g -DPJ_HAS_SSL_SOCK=1 -fPIC -Wno-maybe-uninitialized
CXXFLAGS_COMMON := $(CFLAGS_COMMON) -fexceptions
CFLAGS_COMMON := $(CFLAGS_COMMON) -Wall

ifeq ($(BUILD),release)
  CFLAGS_COMMON := -O2 $(filter-out -g,$(CFLAGS_COMMON))
else
  CFLAGS_COMMON := -O0 $(CFLAGS_COMMON)
endif

# Libraries (from binding.gyp, paths relative to project root)
LIBS := \
	-Wl,--start-group \
	-L3rdParty/pjproject/pjnath/lib \
	-L3rdParty/pjproject/pjlib/lib \
	-L3rdParty/pjproject/pjlib-util/lib \
	-L3rdParty/pjproject/third_party/lib \
	-L3rdParty/pjproject/pjmedia/lib \
	-L3rdParty/pjproject/pjsip/lib \
	-lpjnath-x86_64-unknown-linux-gnu \
	-lilbccodec-x86_64-unknown-linux-gnu \
	-lwebrtc-x86_64-unknown-linux-gnu \
	-lyuv-x86_64-unknown-linux-gnu \
	-lspeex-x86_64-unknown-linux-gnu \
	-lgsmcodec-x86_64-unknown-linux-gnu \
	-lg7221codec-x86_64-unknown-linux-gnu \
	-lpjmedia-audiodev-x86_64-unknown-linux-gnu \
	-lpjmedia-x86_64-unknown-linux-gnu \
	-lresample-x86_64-unknown-linux-gnu \
	-lpjmedia-codec-x86_64-unknown-linux-gnu \
	-lpjmedia-videodev-x86_64-unknown-linux-gnu \
	-lpjsdp-x86_64-unknown-linux-gnu \
	-lpjsip-x86_64-unknown-linux-gnu \
	-lpjsua2-x86_64-unknown-linux-gnu \
	-lpjsip-ua-x86_64-unknown-linux-gnu \
	-lpjsip-simple-x86_64-unknown-linux-gnu \
	-lpjsua-x86_64-unknown-linux-gnu \
	-lpj-x86_64-unknown-linux-gnu \
	-lpjlib-util-x86_64-unknown-linux-gnu \
	3rdParty/spandsp/src/.libs/libspandsp.a \
	3rdParty/bcg729/src/libbcg729.a \
	3rdParty/pocketsphinx/build/libpocketsphinx.a \
	-lstdc++ \
	-lopus \
	-lssl \
	-lcrypto \
	-luuid \
	-lm \
	-ldl \
	-ltiff \
	-lrt \
	-lpthread \
	-lasound \
	-lavformat \
	-lavcodec \
	-lswscale \
	-lavutil \
	-lspeex \
	-lflite \
	-lflite_cmu_us_awb \
	-lflite_cmu_us_kal \
	-lflite_cmu_us_rms \
	-lflite_cmu_us_slt \
	-lflite_cmu_us_kal16 \
	-lsrtp-x86_64-unknown-linux-gnu \
	-ljpeg \
	-Wl,--end-group

# --- Targets ---

.PHONY: all server deps clean

all: server

server: $(TARGET)

deps:
	./build_deps.sh

$(TARGET): $(SERVER_OBJS)
	$(CXX) -o $@ $^ $(LIBS)

$(OUTDIR)/%.o: %.cpp | deps
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_COMMON) $(INCLUDES) -c $< -o $@

$(OUTDIR)/%.o: %.c | deps
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OUTDIR) $(TARGET)
