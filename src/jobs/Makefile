FLAGS = -Wall -Werror -std=gnu99

all: randprint
.PHONY: clean

randprint: randprint.o 
	gcc ${FLAGS} -o $@ $^ 

%.o: %.c 
	gcc ${FLAGS} -c $<

clean:
	rm -f *.o randprint
