LOCAL_PATH := $(PWD)

all: out/webRTC

include ../build/defaults

TARGET := webRTC

C++FLAGS := \
    -I../https/include                  \
    -I../new2/libandroid/include        \

C++FLAGS += -O0 -g -Wall -Wextra -DTARGET_MAC=1

C++FLAGS += -Wno-gnu-anonymous-struct -Wno-nested-anon-types
C++FLAGS += -fno-rtti

LDFLAGS += \
    -framework CoreFoundation   \
    -framework Security         \

C++FLAGS += -I/usr/local/opt/openssl/include
LDFLAGS += -L/usr/local/opt/openssl/lib -lssl -lcrypto

C++FLAGS += -I/usr/local/opt/srtp/include
LDFLAGS += -L/usr/local/opt/srtp/lib -lsrtp2

STATIC_LIBS := libandroid.a libhttps.a

SRCS := \
    DTLS.cpp                            \
    H264Assembler.cpp                   \
    H264Packetizer.cpp                  \
    MyWebSocketHandler.cpp              \
    RTPReceiver.cpp                     \
    RTPSender.cpp                       \
    RTPSession.cpp                      \
    RTPSocketHandler.cpp                \
    STUNMessage.cpp                     \
    webRTC.cpp                          \

include ../build/build_executable
include ../build/clear

include ../https/local.mak
include ../new2/libandroid/local.mak
