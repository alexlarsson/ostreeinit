all: ostreeinit

ostreeinit: init.c
	gcc -O2 -Wall init.c -o ostreeinit

clean:
	rm -f ostreeinit ostree-initrd.img

clang-format:
	git ls-files | grep -Ee "\\.[hc]" | xargs clang-format -style=file -i
