
CC	:= gcc
LD	:= gcc

LIBS	:=
LDFLAGS	:=
CFLAGS	:= -g -W -Wall -Wunused -Wmissing-prototypes \
	-Wcast-qual -Wcast-align -Werror




all: x-all


SRC	:= client.c proto.c player.c main.c
OBJS	:= $(patsubst %.c,%.o,$(SRC))

LNK += xmserv
xmserv: $(OBJS)





x-all: $(LNK)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(LNK):
	$(LD) $(LDFLADS) -o $@ $^ $(LIBS)
