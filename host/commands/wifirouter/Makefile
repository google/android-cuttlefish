# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

BINARY=wifirouter
SRCS=router.cc
OBJS=$(SRCS:.cc=.o)

CXXFLAGS+=-std=c++11
CXXFLAGS+=-I../../..
CXXFLAGS+=$(shell pkg-config --cflags libnl-3.0)
CXXFLAGS+=$(shell pkg-config --cflags libnl-genl-3.0)
CXXFLAGS+=$(shell pkg-config --cflags libglog)
CXXFLAGS+=$(shell pkg-config --cflags libgflags)
LDFLAGS+=$(shell pkg-config --libs libnl-3.0)
LDFLAGS+=$(shell pkg-config --libs libnl-genl-3.0)
LDFLAGS+=$(shell pkg-config --libs libglog)
LDFLAGS+=$(shell pkg-config --libs libgflags) -lgflags

$(BINARY): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm $(OBJS) $(BINARY)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<
