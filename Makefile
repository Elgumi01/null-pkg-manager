CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lcjson
PREFIX = /usr/local
TARGET = npkg

NPKG_PACKAGES  = /etc/npkg/packages
NPKG_INSTALLED = /var/lib/npkg/installed
NPKG_BUILD     = /var/cache/npkg/build

all:
	$(CC) $(CFLAGS) src/*.c -Iinclude -Iconfig -o $(TARGET) $(LDFLAGS)

.PHONY: clean install uninstall

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -d $(PREFIX)/bin

	install -d -m 755 $(NPKG_PACKAGES)
	install -d -m 755 $(NPKG_INSTALLED)
	install -d -m 755 $(NPKG_BUILD)

	for f in packages/*.json; do \
		dest=$(NPKG_PACKAGES)/$$(basename $$f); \
		[ -f $$dest ] || install -m644 $$f $$dest; \
	done

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
