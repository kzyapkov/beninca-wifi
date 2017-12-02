.PHONY: all clean

all:
	mos build --local --verbose --repo ./mongoose-os

clean:
	rm -rf build
