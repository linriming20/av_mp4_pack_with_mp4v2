TRAGET := mp4v2_pack_demo
TRAGET2 := mp4v2_pack_demo2

CXX := g++
CFLAG := -I./include

# 控制程序里的DEBUG打印开关
ifeq ($(DEBUG), 1)
CFLAG += -DENABLE_DEBUG
endif

all : $(TRAGET) $(TRAGET2)

$(TRAGET) : main.c lib/libmp4v2.a
	$(CXX) $^ $(CFLAG) -o $@

$(TRAGET2) : main2.c lib/libmp4v2.a
	$(CXX) $^ $(CFLAG) -o $@

clean : 
	rm -rf $(TRAGET) $(TRAGET2) *.mp4
.PHONY := clean

