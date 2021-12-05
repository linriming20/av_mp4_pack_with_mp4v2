TRAGET := mp4v2_demo
TRAGET2 := mp4v2_demo2

CXX := g++
CFLAG := -I./include

all : $(TRAGET) $(TRAGET2)

$(TRAGET) : main.c lib/libmp4v2.a
	$(CXX) $^ $(CFLAG) -o $@

$(TRAGET2) : main2.c lib/libmp4v2.a
	$(CXX) $^ $(CFLAG) -o $@

clean : 
	rm -rf $(TRAGET) $(TRAGET2) *.mp4
.PHONY := clean

