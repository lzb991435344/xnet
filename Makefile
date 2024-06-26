SRC_C = src/xnet.c src/xnet_socket.c src/xnet_main.c src/util_time.c \
		src/xnet_timeheap.c

all : test

client.exe : src/xnet.c src/xnet_socket.c src/util_time.c src/xnet_timeheap.c\
		test/test_client.c
	gcc -o $@ $^ -std=gnu99 -lws2_32 -pthread

test : test/test.c src/xnet_timeheap.c src/xnet_config.c
	gcc -o $@ $^ -std=gnu99

win : xnet_main.exe
linux : xnet_main
bsd : xnet_main_bsd
xnet_main.exe : $(SRC_C)
	gcc -o $@ $^ -std=gnu99 -lws2_32 -pthread

xnet_main : $(SRC_C)
	gcc -o $@ $^ -std=gnu99 -pthread

xnet_main_bsd : $(SRC_C)
	gcc -o $@ $^ -std=gnu99 -lkqueue -pthread