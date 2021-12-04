TRAGET := mp4v2_demo

CXX := g++
CFLAG := -I./include

all : $(TRAGET) 

$(TRAGET) : main.c lib/libmp4v2.a
	$(CXX) $^ $(CFLAG) -o $@

clean : 
	rm -rf $(TRAGET)
.PHONY := clean

