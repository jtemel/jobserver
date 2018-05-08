PORT = 50110
FLAGS = -DPORT=${PORT} -Wall -Werror -g -std=gnu99
DEPENDENCIES = socket.h jobprotocol.h jobcommands.h serverdata.h serverlog.h

EXECS = jobserver jobclient
SUBDIRS = jobs

.PHONY: ${SUBDIRS} clean

all: ${EXECS} ${SUBDIRS}

${EXECS}: %: %.o jobprotocol.o jobcommands.o socket.o serverdata.o serverlog.o
	gcc ${FLAGS} -o $@ $^

${SUBDIRS}:
	make -C $@

%.o: %.c ${DEPENDENCIES}
	gcc ${FLAGS} -c $<

clean:
	rm -f *.o ${EXECS}
	@for subd in ${SUBDIRS}; do \
        echo Cleaning $${subd} ...; \
        make -C $${subd} clean; \
    done
