CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11
CPPFLAGS += -Ivendor/chipmem/include -Ivendor/blit/include
LDLIBS += -ldrm -lm -lOpenCL

VENDOR_SRC = $(wildcard vendor/chipmem/src/*.c) \
             $(wildcard vendor/blit/src/*.c)

run-probe: tests/probe_reach
	./tests/probe_reach

tests/probe_reach: tests/probe_reach.c $(VENDOR_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/probe_reach.c $(VENDOR_SRC) \
	  $(LDLIBS) -o $@

clean:
	rm -f tests/probe_reach
.PHONY: run-probe clean

run-probe-warm: tests/probe_reach_warm
	./tests/probe_reach_warm

tests/probe_reach_warm: tests/probe_reach_warm.c $(VENDOR_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/probe_reach_warm.c $(VENDOR_SRC) \
	  $(LDLIBS) -o $@

CPPFLAGS += -Iinclude
LIB_OBJ = src/reach.o

all: libreach.a
libreach.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

src/reach.o: src/reach.c include/reach.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/reach.c -o $@

run-test: tests/t_reach
	./tests/t_reach

tests/t_reach: tests/t_reach.c libreach.a $(VENDOR_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/t_reach.c libreach.a $(VENDOR_SRC) \
	  $(LDLIBS) -o $@
