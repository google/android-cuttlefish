CXXFLAGS := -Wextra -Wall -pedantic-errors -std=c++17
PROG := play_audio
OBJS := $(PROG).o client_socket.o sdl_wrapper.o opuscpp/opus_wrapper.o
LINKFLAGS := -lopus -lgflags -lglog -lSDL2

$(PROG): $(OBJS)
	$(CXX) $^ -o $@ $(LINKFLAGS)

clean:
	rm -f $(OBJS) $(PROG)

