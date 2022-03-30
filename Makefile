setup:
	mkdir -p bin

client: setup
	gcc src/upload.c -o bin/send
	gcc src/download.c -o bin/get

server: setup
	gcc src/server.c --output bin/server

all: client server
