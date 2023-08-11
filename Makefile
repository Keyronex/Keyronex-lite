
all:
	@echo "Targets are build-{target}, run-{target}"
	@echo "where target is amd64, virt-aarch64, virt-m68k, or soft"

build/soft/.configured:
	mkdir -p build
	meson setup -Dport=soft build/soft/ kernel
	touch $@

build-soft: build/soft/.configured
	ninja -C build/soft