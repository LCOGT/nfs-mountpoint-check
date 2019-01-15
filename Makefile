PREFIX := /usr
CFLAGS := -std=gnu99 -O2 -march=native -pipe -ggdb -Wall -Wextra -Werror

.PHONY: all
all: nfs-mountpoint-check

.PHONY: clean
clean:
	rm -f *.o nfs-mountpoint-check

.PHONY: install
install: nfs-mountpoint-check
	/usr/bin/install -d "$(DESTDIR)$(PREFIX)/bin"
	/usr/bin/install -m 0755 "$<" "$(DESTDIR)$(PREFIX)/bin/nfs-mountpoint-check"
