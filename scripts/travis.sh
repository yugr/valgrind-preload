#!/bin/sh

# Copyright 2022 Yury Gribov
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

set -eu
set -x

if test -n "${TRAVIS:-}" -o -n "${GITHUB_ACTIONS:-}"; then
  set -x
fi

cd $(dirname $0)/..

make "$@" clean all
make "$@" check

# Upload coverage
if test -n "${COVERAGE:-}"; then
  # Collect coverage for DLL
  # TODO: increase coverage by using -fprofile-dir=coverage.%p
  mv bin/*.gc[dn][ao] src
  gcov src/*.gcno
  # Collect coverage for tests
  for d in tests/*; do
    ! test -d "$d" || (cd $d && gcov *.gcno)
  done
  # Upload
  curl --retry 5 -s https://codecov.io/bash > codecov.bash
  bash codecov.bash -Z -X gcov
fi
