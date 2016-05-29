
.phony: all

all: bin/mcast


bin/mcast: obj/mcast.o
	@mkdir -p "$(@D)";
	@gcc "$<" -o "$@";

obj/mcast.o: src/mcast.c
	@mkdir -p "$(@D)";
	@gcc -c -g "$<" -o "$@";

clean:
	@rm -rf obj bin libs;
