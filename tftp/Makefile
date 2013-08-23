CC = gcc
TARGETS = $(SERVER)
FLAGS = 
INCLUDES =
LIBS =
LIBPATH =


SERVER = server
TFTP   = tftp

OBJS = tftp.o

#pattern -> target ':' prerequisites
#	command
#	command

all: $(TARGETS)
	@echo $(TARGETS)

$(SERVER): server.o $(OBJS)
	$(CC) -g -o $@ $^ $(LIBPATH) $(LIBS)

$(TFTP): tftp.o $(OBJS)
	$(CC) -o $@ $^ $(LIBPATH) $(LIBS)

# Simple compile of all *.cc files
%.o : %.c
	$(CC) $(FLAGS) -c $^ $(LIBPATH) $(LIBS)

clean:
	rm -f *.o
	rm -f $(TARGETS) 
