# Copyright 2017-2019 Yury Gribov
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

CC ?= gcc
CPPFLAGS = -D_GNU_SOURCE
CFLAGS = -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror
LDFLAGS = -shared
LIBS = -ldl

ifneq (,$(COVERAGE))
  DEBUG = 1
  CFLAGS += --coverage -DNDEBUG
  LDFLAGS += --coverage
endif
ifeq (,$(DEBUG))
  CFLAGS += -O2
  LDFLAGS += -Wl,-O2
else
  CFLAGS += -O0
endif

$(shell mkdir -p bin)

all: bin/libpregrind.so bin/pregrind

bin/%: scripts/% Makefile
	cp $< $@

bin/libpregrind.so: bin/pregrind.o Makefile
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

bin/%.o: src/%.c Makefile
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $^

clean:
	rm -f bin/*

check:
	tests/basic/run.sh
	echo SUCCESS

.PHONY: clean all
