
CC	:= gcc
LD	:= gcc

LIBS	:=
DEPFLAGS	:=
LDFLAGS	:=
CFLAGS	:= -g -W -Wall -Wunused -Wmissing-prototypes \
	-Wcast-qual -Wcast-align -Werror \
	$(DEPFLAGS)




all: x-all


SRCS	:= client.c proto.c \
	user.c track.c queue.c \
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
