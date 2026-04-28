.PHONY: all dev release install clean

all: dev

dev:
	cmake --preset dev
	cmake --build --preset dev

release:
	cmake --preset release
	cmake --build --preset release

install: dev
	cmake --install build/dev

clean:
	rm -rf build/
