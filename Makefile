CC = g++
CFLAGS = -Wall -Wextra
BIN = ./DjVu2JPG

all: build

build:
	$(CC) -o $(BIN) -I/usr/include/libdjvu DjVu2JPG.cpp toojpeg.cpp -L/usr/lib/x86_64-linux-gnu -ldjvulibre -ltiff -pthread 

run:
	$(BIN) test.djvu JPEG

clean:
	rm -f $(BIN) *.jpg *.tif

install_deps:
	sudo apt-get install libtiff5-dev libdjvulibre-dev