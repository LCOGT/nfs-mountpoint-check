# NFS Mountpoint Check

A [Linux](https://kernel.org/) utility to determine whether a
[Network File System](http://nfs.sourceforge.net/) mount point
is operating correctly.

This utility is intended to be used as part of a script which can detect and
automatically fix NFS mount points when they are not operating correctly.

## Command Line Options

| Option | Description | Default Value |
| --- | --- | --- |
| `-h`, `--help` | Print help information | N/A |
| `-m`, `--method=X,Y,Z` | Check method(s) | `stat,readdir` |
| `-t`, `--timeout=N` | Timeout (seconds) | 2 |
| `-v`, `--verbose` | Verbosity (0-3) | 0 |

## Background Information

It turns out that detecting whether an NFS mount is operating correctly is
complicated. Detecting problems in a timely manner adds even more difficulty.

This utility implements several methods of detecting whether an NFS mount is
operating correctly. Older Linux kernel versions often have surprising
behaviors when running these checks.

Further information:
- <http://nfs.sourceforge.net/>
- <https://stackoverflow.com/questions/1643347/is-there-a-good-way-to-detect-a-stale-nfs-mount>
- <https://github.com/acdha/mountstatus>

## Operating System Specific Behaviors

### CentOS 5

CentOS 5 (Linux 2.6.18) often takes up to several minutes to detect a
crashed/hung server or a stale mount. Testing has proved that this Linux kernel
version caches information for up to several minutes without re-checking with
the NFS server. No workaround has been found to make this check quicker.

### CentOS 6

CentOS 6 (Linux 2.6.32) often takes up to 30 seconds to detect a crashed/hung
server or a stale mount. Testing has shown that the readdir check will make
this Linux kernel version detect problems much more quickly.

### CentOS 7

CentOS 7 (Linux 3.10) seems to always detect a crashed/hung server within 5
seconds. It behaves very well.

## Build

This utility does not have any dependencies outside of the standard C library.
You should be able to simply use `make` to build the application. You can
override the `CFLAGS` if the defaults do not work for your system.

```
# This should work for most users on new-ish systems
$ make

# But you can override the CFLAGS if necessary:
$ make CFLAGS="-O2 -ggdb -pipe"
```

An RPM specfile is provided for Redhat / CentOS distributions. If you care
about this, you already know how to build RPMs from source.

## Installation

Put the `nfs-mountpoint-check` binary anywhere in your `PATH`.

## License

This code is licensed under the [MIT License](https://choosealicense.com/licenses/mit/).
Please see the [LICENSE](LICENSE) file for more details.

## Acknowledgements

Special thanks to [Chris Adams](https://github.com/acdha) and his
implementation of the excellent
[mountstatus](https://github.com/acdha/mountstatus/) utility.
