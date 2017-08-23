
.phony: all

all: bin/mcast


bin/mcast: obj/mcast.o
	@mkdir -p "$(@D)";
	@echo "$< -> $@";
	@gcc "$<" -o "$@";

obj/mcast.o: src/mcast.c
	@mkdir -p "$(@D)";
	@echo "$< -> $@";
	@gcc -c -g "$<" -o "$@";

clean:
	@rm -rf obj bin libs;
