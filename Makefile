
CC		:= gcc
LD		:= gcc

LIBS		:=
DEPFLAGS	:= -I.
LDFLAGS		:=
CFLAGS		:= -g -W -Wall -Wunused -Wmissing-prototypes \
		-Wcast-qual -Wcast-align -Werror \
		$(DEPFLAGS)

# choose your database type:
DBDIR		:= pgdb
# whoops - nothing else to choose from *g*




all: x-all


SRCS		:= client.c proto.c \
		opt.c player.c main.c

ifeq ($(DBDIR),pgdb)
DBSRCS		:= user.c track.c queue.c db.c
SRCS		+= $(patsubst %,$(DBDIR)/%,$(DBSRCS))
LIBS		+= -lpq
endif

OBJS		:= $(patsubst %.c,%.o,$(SRCS))

LNK += xmserv
xmserv: $(OBJS)





x-all: $(LNK)

include $(OBJS:.o=.d)
%.d: %.c
	@echo mkdep $<
	@$(CC) $(DEPFLAGS) -M -MG $< | \
		sed -e 's@ /[^ ]*@@g' -e 's@^\(.*\)\.o:@\1.d \1.o:@' > $@


.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(LNK):
	$(LD) $(LDFLADS) -o $@ $^ $(LIBS)

todo:
	@grep -i todo $(SRCS)

clean:
	rm -f $(OBJS)
	rm -f $(LNK)
	rm -f .depend


