all: vm input_gen list_test

vm: vm.c vm.h replacement.c pagetable.c pagetable.h disk.h disk.c list.c
		gcc vm.c replacement.c pagetable.c disk.c list.c -g -o vm

input_gen: input_gen.c vm.h
		gcc input_gen.c -g -o input_gen

list_test: list_test.c list.c list.h
		gcc list_test.c list.c -g -o list_test

clean:
		rm -f vm input_gen list_test
