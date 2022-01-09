# Copyright 2017-2022 Yury Gribov
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

CC ?= gcc
DESTDIR ?= /usr/local

CPPFLAGS = -D_GNU_SOURCE
CFLAGS = -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror
LDFLAGS = -shared -fPIC -Wl,--warn-common
LIBS = -ldl

ifneq (,$(COVERAGE))
  DEBUG = 1
  CFLAGS += --coverage -DNDEBUG
  CFLAGS += -fprofile-dir=coverage.%p
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

bin/libpregrind.so: bin/pregrind.o bin/async_safe.o Makefile bin/FLAGS
	$(CC) $(LDFLAGS) -o $@ $(filter %.o, $^) $(LIBS)

bin/%.o: src/async_safe.h src/common.h

bin/%.o: src/%.c Makefile bin/FLAGS
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

bin/FLAGS: FORCE
	if test x"$(CFLAGS) $(CXXFLAGS) $(LDFLAGS)" != x"$$(cat $@)"; then \
		echo "$(CFLAGS) $(CXXFLAGS) $(LDFLAGS)" > $@; \
	fi

clean:
	rm -f bin/*
	find -name \*.gcov -o -name \*.gcno -o -name \*.gcda -o -name coverage.\* | xargs rm -rf

install:
	mkdir -p $(DESTDIR)
	install bin/libpregrind.so $(DESTDIR)/lib
	install scripts/pregrind $(DESTDIR)/bin

check:
	tests/exec/run.sh
	tests/system/run.sh
	tests/spawn/run.sh
	@echo SUCCESS

.PHONY: clean all check install FORCE
