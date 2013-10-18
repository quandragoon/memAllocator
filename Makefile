TARGETS := mdriver

LOCKER=/afs/csail/proj/courses/6.172
CC := gcc
# You can add -Werr to GCC to force all warnings to turn into errors
CFLAGS := -std=gnu99 -g -Wall -Wno-write-strings
LDFLAGS := -lpthread
# Macros defined by the user or OpenTuner
PARAMS :=

HEADERS := \
	allocator_interface.h \
	config.h \
	fsecs.h \
	mdriver.h \
	memlib.h \
	validator.h

# Blank line ends list.

# If you add a new file called "filename.c", you should
# add "filename.o \" to this list.
OBJS := \
	memlib.o

MDRIVER_OBJS:= \
	allocator.o \
	bad_allocator.o \
	clock.o \
	fcyc.o \
	fsecs.o \
	ftimer.o \
	libc_allocator.o \
	mdriver.o


# Blank line ends list.

OLDMODE := $(shell cat .buildmode 2> /dev/null)
ifeq ($(DEBUG),1)
CFLAGS := -DDEBUG -O0 $(CFLAGS)
ifneq ($(OLDMODE),debug)
$(shell echo debug > .buildmode)
endif
else
CFLAGS := -DNDEBUG -O3 $(CFLAGS)
ifneq ($(OLDMODE),nodebug)
$(shell echo nodebug > .buildmode)
endif
endif

# make all targets specified
all: $(TARGETS)

.PHONY: pintool
pintool:
	$(MAKE) -C pintool

mdriver: $(OBJS) $(MDRIVER_OBJS)
	$(CC) $(PARAMS) $(LDFLAGS) $(OBJS) $(MDRIVER_OBJS) -o $@

# compile objects

# pattern rule for building objects
%.o: %.c
	$(CC) $(PARAMS) $(CFLAGS) -c $< -o $@


# run each of the targets
run: $(TARGETS)
	for X in $(TARGETS) ; do \
		echo $$X -v ; \
		./$$X -v ; \
		echo ; \
	done

partial_clean:
	$(RM) -R $(TARGETS) $(OBJS) $(MDRIVER_OBJS) *.std*
	$(RM) -R tmp/*.out

# remove targets and .o files as well as output generated by CQ
clean: partial_clean
	$(RM) -R *.db* *.log