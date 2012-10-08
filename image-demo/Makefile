CFLAGS=-g -I$(HOME)/opt/mesa/include
CXXFLAGS=$(CFLAGS)
COMMON_OBJECTS = cl_simple.o cl_util.o
LDFLAGS = -L/usr/local/lib -L$(HOME)/opt/mesa/lib -lOpenCL


all: mono

%.o: %.cc
	g++ -o $@ -c $(CXXFLAGS) $^

mono: mono.o imgsupport.o $(COMMON_OBJECTS)
	g++ -o $@ $^ $(LDFLAGS) -lpng
.PHONY: clean
clean:
	-rm *.o mono mono.png
