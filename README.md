# What's this?

Valgrind-preload (AKA Pregrind) is a simple `LD_PRELOAD`-able library
which will cause all spawned processes to be started under Valgrind.

It's functionality is similar to Valgrind's standard `--trace-children=yes`
but fixes few disadvantages:
* avoids intrumenting setuid processes (Valgrind
  [doesn't handle them](http://stackoverflow.com/questions/1701752/how-do-i-run-valgrind-to-a-process-which-has-super-user-bit-on)
  so you have to laboriously blacklist all of them via `--trace-children-skip`
  which is unreliable and disables instrumentation of grandchildren)
* can easily be enabled for the whole distro or chroot via `ld.so.preload`
  (no need to search for `init` and replace it with a wrapper)

The tool seems to be pretty stable now, e.g. I was able to
[instrument complete Debian package builds](https://github.com/yugr/debian_pkg_test/tree/master/examples/valgrind-preload)
and even found [several new errors](https://github.com/yugr/valgrind-preload#trophies).

# Usage

To use, just preload `libpregrind.so` to your app:

    $ LD_PRELOAD=path/to/libpregrind.so app arg1 ...

You can also use a simple wrapper script:

    $ path/to/pregrind app arg1 ...

Finally, if you want to instrument _whole system_, preload `libpregrind.so`
globally:

    $ echo path/to/libpregrind.so >> /etc/ld.so.preload

Note that in this mode `libpregrind.so` will be preloaded to
_all newly started processes_ so any malfunction may permanently break your
system. It's thus highly recommended to only do this in a chroot or VM.

Library can be customized through environment variables:
* PREGRIND\_LOG\_PATH - log to files inside this directory, rather than to stderr
* PREGRIND\_FLAGS - additional flags for Valgrind (e.g. `--track-origins=yes`)
* PREGRIND\_VERBOSE - print diagnostic info
* PREGRIND\_DISABLE - disable instrumentation
* PREGRIND\_BLACKLIST - name of file with wildcard patterns of files
  which should not be instrumented

# Build

To build the tool, simply run make from top directory.

# Trophies

* [acl: Uninitialized value in lt-setfacl](http://savannah.nongnu.org/bugs/index.php?50566) (fixed)
* [libsndfile: Buffer overflow in string\_test](https://github.com/erikd/libsndfile/issues/208)

# Known issues

Various TODO and FIXME are scattered all over the codebase
but tool should be quite robust. Ping me if you need more features.
