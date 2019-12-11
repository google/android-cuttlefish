CXXFLAGS := -Wextra -Wall -pedantic-errors -std=c++17
LDLIBS := -lopus -lgflags -lglog -lSDL2
LINK.o := $(LINK.cc)

PROG := play_audio
OBJS := $(PROG).o client_socket.o sdl_wrapper.o opuscpp/opus_wrapper.o

$(PROG): $(OBJS)

clean:
	rm -f $(OBJS) $(PROG)

