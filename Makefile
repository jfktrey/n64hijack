.PHONY: src u64asm-src

all: src u64asm-src

src:
	$(MAKE) -C $@
	mv $@/n64hijack ./n64hijack

u64asm-src:
	$(MAKE) -C $@
	mv $@/u64asm ./u64asm
