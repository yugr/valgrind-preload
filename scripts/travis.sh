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
  # Merge DLL coverage from different tests
  scripts/gcov-tool-many merge tests/*/merged_profile
  # Get rid of complex names e.g. #home#yugr#src#my#valgrind-preload#bin#async_safe.gcda
  for f in `find -name '#*.gc[dn][ao]'`; do mv $f $(basename $f | tr \# /); done
  mv bin/*.gc[dn][ao] .
  # Generate DLL report
  gcov *.gcno
  # Generate test reports
  for t in tests/*; do
    ! test -d $t || (cd $t && gcov *.gcno)
  done
  # Upload
  curl --retry 5 -s https://codecov.io/bash > codecov.bash
  bash codecov.bash -Z -X gcov
fi
