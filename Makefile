
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


LNK	:=
SRC	:=
OBJ	:=

############################################################
# db

ifeq ($(DBDIR),pgdb)
src		:=  db.c user.c track.c random.c history.c queue.c \
		tag.c
SRC_db		:= $(patsubst %,$(DBDIR)/%,$(src))
OBJ_db		:= $(patsubst %.c,%.o,$(SRC_db))
LIBS		+= -lpq
SRC += $(SRC_db)
OBJ += $(OBJ_db)
endif


############################################################
# xmserv

SRC_xmserv	:= client.c proto.c \
		opt.c player.c sleep.c main.c
OBJ_xmserv	:= $(patsubst %.c,%.o,$(SRC_xmserv))
LNK += xmserv
SRC += $(SRC_xmserv)
OBJ += $(OBJ_xmserv)
xmserv: $(OBJ_xmserv) $(OBJ_db)

# generate array from CMD() macros
cmd.list: proto.c
	cat $^ | sh mkcmdlist.sh  > $@


############################################################
# testdb

#SRC_testdb	:= testdb.c 
#OBJ_testdb	:= $(patsubst %.c,%.o,$(SRC_testdb))
#LNK += testdb
#SRC += $(SRC_testdb)
#OBJ += $(OBJ_testdb)
#testdb: $(OBJ_testdb) $(OBJ_db) opt.o


############################################################

x-all: $(LNK)

DEPS	:= $(OBJ:.o=.d)
include $(DEPS)

%.d: %.c
	@echo mkdep $<
	@$(CC) $(DEPFLAGS) -M -MG $< | \
		sed -e 's@ /[^ ]*@@g' -e 's@^\(.*\)\.o:@\1.d \1.o:@' > $@


.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(LNK):
	$(LD) $(LDFLADS) -o $@ $^ $(LIBS)

todo:
	@find . -type f | grep '\.[ch]$$' | xargs grep -i todo 

clean:
	rm -f $(OBJ)
	rm -f $(LNK)
	rm -f $(DEPS)


