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

CFLAGS='-g -O2 -Wall -Wextra -Werror -shared -fPIC'

if test -n "${COVERAGE:-}"; then
  CFLAGS="$CFLAGS --coverage -DNDEBUG"
fi

ROOT=$PWD/../..

${CC:-gcc} --coverage parent.c -o parent
${CC:-gcc} --coverage child.c -o child

export PREGRIND_FLAGS='-q --error-exitcode=1'

! LD_PRELOAD=$ROOT/bin/libpregrind.so ./parent >test.log 2>&1
grep 'Invalid read of size 4' test.log
