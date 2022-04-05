# cloud-storage
A client-server app that can send files and download them back from server.

### How to use

Client:
```bash
git clone https://github.com/s-walrus/cloud-storage/ && cd cloud-storage
make client


# usage: ./get HOST FILE [DIST] [PORT]

# download a file
bin/get 193.164.149.95 pic.jpg

# download a larger file
bin/get 193.164.149.95 piece.flac


# usage: ./send HOST FILE [DIST] [PORT]

# upload a file
bin/send 193.164.149.95 /path/to/local_file

# download it
bin/get 193.164.149.95 local_file
```

Set up a server:
```bash
make server

# usage: ./server DIR [PORT]

mkdir storage_dir
bin/server storage_dir

# server is set up at localhost
bin/send localhost /path/to/local_file
bin/get localhost local_file
```
