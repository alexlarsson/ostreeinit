all: ostreeinit

LIBDIR=/usr/lib/ostreeinit

ostreeinit: ostreeinit.c
	gcc -O2 -Wall ostreeinit.c -o ostreeinit

clean:
	rm -f ostreeinit ostree-initrd.img

install: ostreeinit
	mkdir -p $(DESTDIR)$(LIBDIR)
	install -t $(DESTDIR)$(LIBDIR) ostreeinit ostreeinit_mkinitrd.sh

clang-format:
	git ls-files | grep -Ee "\\.[hc]$$" | xargs clang-format -style=file -i
