
CC	:= gcc
LD	:= gcc

LIBS	:=
DEPFLAGS	:=
LDFLAGS	:=
CFLAGS	:= -g -W -Wall -Wunused -Wmissing-prototypes \
	-Wcast-qual -Wcast-align -Werror \
	$(DEPFLAGS)

# choose your database type:
DBDIR	:= pgdb





ifeq ($(DBDIR),pgdb)
LIBS	+= -lpq
endif

all: x-all

DBSRCS	:= user.c track.c queue.c db.c

SRCS	:= client.c proto.c \
	$(patsubst %,$(DBDIR)/%,$(DBSRCS)) \
	opt.c player.c main.c
OBJS	:= $(patsubst %.c,%.o,$(SRCS))

LNK += xmserv
xmserv: $(OBJS)





x-all: $(LNK)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(LNK):
	$(LD) $(LDFLADS) -o $@ $^ $(LIBS)

-include .depend
depend:
	makedepend -Y -f- $(SRCS) > .depend

clean:
	rm -f $(OBJS)
	rm -f $(LNK)
	rm -f .depend


