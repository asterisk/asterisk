
NUBSV := $(shell gcc -Wno-unused-but-set-variable -o /dev/null -xc -c - </dev/null 2>/dev/null && echo -Wno-unused-but-set-variable)

CFLAGS += -fPIC $(NUBSV) -Wno-unused-variable -Wno-unused-label -Wno-unused-function -Wno-strict-aliasing
