FROM gcc:latest

COPY . /usr/src/drive
WORKDIR /usr/src/drive
RUN make server
RUN mkdir storage_dir

CMD [ "bin/server", "storage_dir" ]

EXPOSE 3490/tcp
