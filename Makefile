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

ifeq ($(WITH_LIBNL1), 1)
LIBNL1FLAGS = -DWITH_LIBNL1 $(shell pkg-config --cflags --libs libnl-1)
else
LIBNL1FLAGS =
endif

ifeq ($(WITH_WL1251_NL), 1)
WL1251NLFLAGS = -DWITH_WL1251_NL
else
WL1251NLFLAGS =
endif

wl1251-cal: wl1251-cal.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o wl1251-cal wl1251-cal.c $(DBUSFLAGS) $(LIBCALFLAGS) $(LIBNL1FLAGS) $(WL1251NLFLAGS)

install:
	install -d "$(DESTDIR)/usr/bin"
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
	$(RM) wl1251-cal
