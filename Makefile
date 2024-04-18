all: ostreeinit

VERSION=0.1

LIBDIR?=/usr/lib/ostreeinit
BINDIR?=/usr/bin
CFLAGS?=-O2 -Wall

ostreeinit: ostreeinit.c
	gcc $(CFLAGS) ostreeinit.c -o ostreeinit

clean:
	rm -f ostreeinit ostree-initrd.img

install: ostreeinit
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -t $(DESTDIR)$(LIBDIR) ostreeinit
	mkdir -p $(DESTDIR)/usr/lib/dracut/modules.d/50ostreeinit
	install -t $(DESTDIR)/usr/lib/dracut/modules.d/50ostreeinit dracut/module-setup.sh

dist:
	git archive --prefix=ostreeinit-${VERSION}/ --output=ostreeinit-${VERSION}.tar.gz HEAD

clang-format:
	git ls-files | grep -Ee "\\.[hc]$$" | xargs clang-format -style=file -i
