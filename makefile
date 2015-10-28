wl1251-cal:
	gcc -g -o wl1251-cal wl1251-cal.c -lcal -lnl -std=gnu99

install:
	install -d $(DESTDIR)/etc/event.d
	install script/wl1251-cal $(DESTDIR)/etc/event.d
	install -d $(DESTDIR)/usr/sbin
	install wl1251-cal $(DESTDIR)/usr/sbin

clean:
	rm wl1251-cal

