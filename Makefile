ifeq ($(WITH_DBUS), 1)
DBUSFLAGS = -DWITH_DBUS $(shell pkg-config --cflags --libs dbus-1)
else
DBUSFLAGS =
endif

ifeq ($(WITH_LIBCAL), 1)
LIBCALFLAGS = -DWITH_LIBCAL $(shell pkg-config --cflags --libs libcal)
else
LIBCALFLAGS = cal.c
endif

ifeq ($(WITH_LIBNL3), 1)
LIBNLFLAGS = -DWITH_LIBNL $(shell pkg-config --cflags --libs libnl-3.0 libnl-genl-3.0)
else ifeq ($(WITH_LIBNL2), 1)
LIBNLFLAGS = -DWITH_LIBNL $(shell pkg-config --cflags --libs libnl-2.0 libnl-genl-2.0)
else ifeq ($(WITH_LIBNL1), 1)
LIBNLFLAGS = -DWITH_LIBNL -DWITH_LIBNL1 $(shell pkg-config --cflags --libs libnl-1)
else
LIBNLFLAGS =
endif

ifeq ($(WITH_WL1251_NL), 1)
WL1251NLFLAGS = -DWITH_WL1251_NL
else
WL1251NLFLAGS =
endif

wl1251-cal: wl1251-cal.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o wl1251-cal wl1251-cal.c $(DBUSFLAGS) $(LIBCALFLAGS) $(LIBNLFLAGS) $(WL1251NLFLAGS)

install:
	install -d "$(DESTDIR)/usr/bin"
	install -m 755 wl1251-wrap "$(DESTDIR)/usr/bin"
	install -m 755 wl1251-cal "$(DESTDIR)/usr/bin"
ifeq ($(WITH_UPSTART), 1)
	install -d "$(DESTDIR)/etc/event.d"
	install -m 644 script/wl1251-cal "$(DESTDIR)/etc/event.d"
endif
ifeq ($(WITH_UDEV), 1)
	install -d "$(DESTDIR)/lib/udev/rules.d"
	install -m 644 49-firmware-wl1251.rules "$(DESTDIR)/lib/udev/rules.d"
endif

clean:
	$(RM) -f wl1251-cal
