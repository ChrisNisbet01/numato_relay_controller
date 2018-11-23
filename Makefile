#Makefile to build numato relay controller

LIB_PREFIX?=/usr/local
INCLUDES = -I/src -I$(LIB_PREFIX)/include -I$(LIB_PREFIX)/include/ioutils
DEFINES = -D_GNU_SOURCE
LIBS=\
	-ljson-c \
	-lubus \
	-lubox \
	-lioutils

LDFLAGS ?= -L$(LIB_PREFIX)/lib -Wl,-rpath $(LIB_PREFIX)/lib
SRC_DIR=src
OBJ_DIR=obj
DEP_DIR=dep
C_DEFINES=-g
CFLAGS =$(C_DEFINES) -Wall -Werror $(INCLUDES) $(DEFINES)

TARGET = numato

vpath %.c src
vpath %.h src


SRCS=$(wildcard $(SRC_DIR)/*.c)

OBJS=$(notdir ${SRCS:.c=.o})

.PHONY: all clean

all: pre_build ${TARGET}

pre_build:
	mkdir -p $(DEP_DIR)

# pull in dependency info for *existing* .o files
-include $(addprefix $(DEP_DIR)/,$(OBJS:.o=.d))

${TARGET}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS} ${LIBS}

# compile and generate dependency info;
# more complicated dependency computation, so all prereqs listed
# will also become command-less, prereq-less targets
#   sed:    strip the target (everything before colon)
#   sed:    remove any continuation backslashes
#   fmt -1: list words one per line
#   sed:    strip leading spaces
#   sed:    add trailing colons
%.o: %.c
	${CC} $(CFLAGS) -c $< -o $@
	@# auto-dependency stuff
	@${CC} -MM $(CFLAGS) $< > $(DEP_DIR)/$*.d
	@mv -f $(DEP_DIR)/$*.d $(DEP_DIR)/$*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $(DEP_DIR)/$*.d.tmp > $(DEP_DIR)/$*.d
	@cp -f $(DEP_DIR)/$*.d $(DEP_DIR)/$*.d.tmp
	@sed -e 's/.*://' -e 's/\\$$//' < $(DEP_DIR)/$*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $(DEP_DIR)/$*.d
	@rm -f $(DEP_DIR)/$*.d.tmp


clean:
	rm -rf ${TARGET} $(OBJS) $(DEP_DIR)/*

