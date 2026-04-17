all: pico2w

pico2w: pico2w--config pico2w--build

pico2w--config:
	cmake --preset "pico2w"

pico2w--build:
	cmake --build --preset "pico2w"

install: pico2w
	cmake --install build

clean:
	rm -rf build/
