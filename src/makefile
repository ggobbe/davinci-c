###
# Projet Unix 2012 : Code de Vinci
#
# Makefile
###
CC = gcc
CFLAGS = -g

### Compilateur C89 ###
# CC = c89
# CFLAGS = -g -D_SVID_SOURCE

all :
	make server
	make client
	@make -s clean

server : server.o
	@$(CC) -o server server.o

server.o : server.c server.h project.h
	@$(CC) -c server.c $(CFLAGS)

client : client.o
	@$(CC) -o client client.o

client.o : client.c client.h project.h
	@$(CC) -c client.c $(CFLAGS)

clean :
	-rm -f *.o
	-rm -f *~
