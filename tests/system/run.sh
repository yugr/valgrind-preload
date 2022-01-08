#!/bin/sh

# Copyright 2022 Yury Gribov
#
# The MIT License (MIT)
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

# This is a simple test for valgrind-preload functionality.

set -eu

cd $(dirname $0)

if test -n "${GITHUB_ACTIONS:-}"; then
  set -x
fi

CFLAGS='-g -O0 -Wall -Wextra -Werror'

if test -n "${COVERAGE:-}"; then
  CFLAGS="$CFLAGS --coverage -DNDEBUG"
  CFLAGS="$CFLAGS -fprofile-dir=coverage.%p"
fi

ROOT=$PWD/../..

${CC:-gcc} $CFLAGS parent.c -o parent
${CC:-gcc} $CFLAGS child.c -o child

export PREGRIND_FLAGS='-q --error-exitcode=1'

if ! LD_PRELOAD=$ROOT/bin/libpregrind.so ./parent >test.log 2>&1 \
    || ! grep -q 'Invalid read of size 4' test.log; then
  echo "system: test failed" >&2
  cat test.log >&2
fi

if test -n "${COVERAGE:-}"; then
  # Merge DLL coverage from both processes
  gcov-tool merge coverage.*
  rm -rf coverage.*
fi
