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

PJ_LIB := 3rdParty/pjproject
PJ_SUFFIX := -x86_64-unknown-linux-gnu.a

# Include directories
INCLUDES := \
	-I$(PJ_LIB)/pjsip/include \
	-I$(PJ_LIB)/pjlib/include \
	-I$(PJ_LIB)/pjlib-util/include \
	-I$(PJ_LIB)/pjnath/include \
	-I$(PJ_LIB)/pjmedia/include \
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
	-I3rdParty/pjwebsock/websock \
	-I3rdParty/alsa-lib-1.2.6.1/include

# Compiler flags
CFLAGS_COMMON := -g -DPJ_HAS_SSL_SOCK=1 -fPIC -Wno-maybe-uninitialized
CXXFLAGS_COMMON := $(CFLAGS_COMMON) -fexceptions
CFLAGS_COMMON := $(CFLAGS_COMMON) -Wall

ifeq ($(BUILD),release)
  CFLAGS_COMMON := -O2 $(filter-out -g,$(CFLAGS_COMMON))
else
  CFLAGS_COMMON := -O0 $(CFLAGS_COMMON)
endif

# Static 3rdParty libraries (direct .a paths, no -L/-l indirection)
STATIC_LIBS := \
	$(PJ_LIB)/pjnath/lib/libpjnath$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libilbccodec$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libwebrtc$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libyuv$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libspeex$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libgsmcodec$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libg7221codec$(PJ_SUFFIX) \
	$(PJ_LIB)/pjmedia/lib/libpjmedia-audiodev$(PJ_SUFFIX) \
	$(PJ_LIB)/pjmedia/lib/libpjmedia$(PJ_SUFFIX) \
	$(PJ_LIB)/third_party/lib/libresample$(PJ_SUFFIX) \
	$(PJ_LIB)/pjmedia/lib/libpjmedia-codec$(PJ_SUFFIX) \
	$(PJ_LIB)/pjmedia/lib/libpjmedia-videodev$(PJ_SUFFIX) \
	$(PJ_LIB)/pjmedia/lib/libpjsdp$(PJ_SUFFIX) \
	$(PJ_LIB)/pjsip/lib/libpjsip$(PJ_SUFFIX) \
	$(PJ_LIB)/pjsip/lib/libpjsua2$(PJ_SUFFIX) \
	$(PJ_LIB)/pjsip/lib/libpjsip-ua$(PJ_SUFFIX) \
	$(PJ_LIB)/pjsip/lib/libpjsip-simple$(PJ_SUFFIX) \
	$(PJ_LIB)/pjsip/lib/libpjsua$(PJ_SUFFIX) \
	$(PJ_LIB)/pjlib/lib/libpj$(PJ_SUFFIX) \
	$(PJ_LIB)/pjlib-util/lib/libpjlib-util$(PJ_SUFFIX) \
	3rdParty/spandsp/src/.libs/libspandsp.a \
	3rdParty/bcg729/src/libbcg729.a \
	3rdParty/pocketsphinx/build/libpocketsphinx.a \
	3rdParty/alsa-lib-1.2.6.1/src/.libs/libasound.a \
	3rdParty/pjproject/third_party/lib/libsrtp$(PJ_SUFFIX)

# System libraries (linked statically via -static flag)
SYSTEM_LIBS := \
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
	-lavformat \
	-lavcodec \
	-lswscale \
	-lavutil \
	-lspeex \
	-lflite \
	-lflite_usenglish \
	-lflite_cmulex \
	-lflite_cmu_us_awb \
	-lflite_cmu_us_kal \
	-lflite_cmu_us_rms \
	-lflite_cmu_us_slt \
	-lflite_cmu_us_kal16 \
	-ljpeg \
	-lz \
	-lzstd \
	-llzma \
	-lwebp \
	-ljbig \
	-ldeflate \
	-lpthread \
	-ldl

LIBS := -static -Wl,--start-group $(STATIC_LIBS) $(SYSTEM_LIBS) -Wl,--end-group

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
