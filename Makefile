# Copyright 2017-2022 Yury Gribov
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

CC ?= gcc
CPPFLAGS = -D_GNU_SOURCE
CFLAGS = -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror
LDFLAGS = -shared -fPIC
LIBS = -ldl

ifneq (,$(COVERAGE))
  DEBUG = 1
	# TODO: should we use `gcov-tool merge' and then `gcov-tool merge' ?
	# (see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=47618)
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

bin/libpregrind.so: bin/pregrind.o bin/async_safe.o Makefile
	$(CC) $(LDFLAGS) -o $@ $(filter %.o, $^) $(LIBS)

bin/%.o: src/async_safe.h src/common.h

bin/%.o: src/%.c Makefile
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f bin/*
	find -name \*.gcov -o -name \*.gcno -o -name \*.gcda -o -name coverage.\* | xargs rm -rf

check:
	tests/exec/run.sh
	tests/system/run.sh
	@echo SUCCESS

.PHONY: clean all check
