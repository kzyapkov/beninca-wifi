.PHONY: all clean

all:
	mos build --local --verbose

clean:
	rm -rf build
