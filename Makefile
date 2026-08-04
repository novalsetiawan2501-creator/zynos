CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -pthread -Iinclude -D_GNU_SOURCE
LDFLAGS := -lssl -lcrypto -lnghttp2 -lm -pthread

SRC     := src/main.c src/http2_client.c src/connection_pool.c src/worker.c
BIN     := h2client

.PHONY: all clean debug

all: $(BIN)

$(BIN): $(SRC) include/http2_client.h
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

debug: CFLAGS += -g -O0 -DDEBUG
debug: clean $(BIN)

clean:
	rm -f $(BIN)

# Contoh pemakaian:
#   ./h2client -u https://localhost:8443/ -c 20 -w 4 -d 30 --debug
#   ./h2client -u https://localhost:8443/api -c 10 -w 2 -d 60 -r 500 -o run1.csv
