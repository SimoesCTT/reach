CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11
CPPFLAGS += -Ivendor/chipmem/include -Ivendor/blit/include -Iinclude
LDLIBS += -ldrm -lm -lOpenCL

VENDOR_SRC = $(wildcard vendor/chipmem/src/*.c) \
             $(wildcard vendor/blit/src/*.c)

LIB_OBJ = src/reach.o

all: libreach.a

libreach.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

src/reach.o: src/reach.c include/reach.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/reach.c -o $@

tests/t_reach: tests/t_reach.c libreach.a $(VENDOR_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/t_reach.c libreach.a $(VENDOR_SRC) \
	  $(LDLIBS) -o $@

tests/probe_reach: tests/probe_reach.c $(VENDOR_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/probe_reach.c $(VENDOR_SRC) \
	  $(LDLIBS) -o $@

run-probe: tests/probe_reach
	./tests/probe_reach

run-test: tests/t_reach
	./tests/t_reach

check: tests/t_reach
	./tests/t_reach

clean:
	rm -f $(LIB_OBJ) libreach.a tests/t_reach tests/probe_reach

.PHONY: all run-probe run-test check clean
