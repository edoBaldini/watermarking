CXX      = g++
CXXFLAGS = -I/usr/X11R6/include -L/usr/X11R6/lib -lm  -lX11 -lpthread -I$(FF_ROOT)

LDFLAGS  = -std=c++17 -O3
OBJS     =  sequential farm farm_ff map map_ff
all:	$(OBJS)

sequential.cpp : CImg.h
farm.cpp : CImg.h
farm_ff.cpp : CImg.h
map.cpp: CImg.h
map_ff.cpp: CImg.h
clean:	
	rm -f $(OBJS)
