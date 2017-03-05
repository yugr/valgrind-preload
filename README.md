# What's this?

Valgrind-preload (AKA Pregrind) is a simple `LD_PRELOAD`-able library
which will cause all spawned processes to be started under Valgrind.

It's functionality is similar to Valgrind's standard `--trace-children=yes`
but the latter has a few disadvantage:
* it has problems with setuid processes (Valgrind
  [doesn't handle them](http://stackoverflow.com/questions/1701752/how-do-i-run-valgrind-to-a-process-which-has-super-user-bit-on)
  so you have to laboriously blacklist all of them via `--trace-children-skip`
  which is unreliable and disables instrumentation of grandchildren)
* it's hard to enable for whole system (you have to find `init` or it's
  equivalent and replace it with Valgrind wrapper)

The tool is in development but all major functionality is there
(e.g. I was able to instrument complete build process of Debian packages via pbuilder).

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

# Build

To build the tool, simply run make from top directory.

# Known issues

Various TODO and FIXME are scattered all over the codebase
but tool should be quite robust. Ping me if you need more features.
