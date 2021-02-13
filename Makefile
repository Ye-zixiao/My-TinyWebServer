CC := g++
CFLAGS := -Wall -O -g
DLFLAGS := -lpthread -lmysqlclient

SRCS := ./webserver/webserver.cpp ./http/http_task.cpp ./log/log.cpp ./mysql/sql_connpool.cpp \
		./myutils/myutils.cpp ./timer/timer_heap.cpp main.cpp
MAIN := server

.PHONY:
all: $(MAIN)

$(MAIN):$(OBJS)
	$(CC) -o $(MAIN) $(SRCS) $(DLFLAGS) $(CFLAGS)

clean:
	-rm $(MAIN)
