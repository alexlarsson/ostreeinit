all: ostreeinit

LIBDIR=/usr/lib/ostreeinit
BINDIR=/usr/bin

ostreeinit: ostreeinit.c
	gcc -O2 -Wall ostreeinit.c -o ostreeinit

clean:
	rm -f ostreeinit ostree-initrd.img

install: ostreeinit
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -t $(DESTDIR)$(LIBDIR) ostreeinit
	install -t $(DESTDIR)$(BINDIR) ostreeinit-mkinitrd

clang-format:
	git ls-files | grep -Ee "\\.[hc]$$" | xargs clang-format -style=file -i
