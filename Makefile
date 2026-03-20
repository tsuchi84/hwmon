CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Isrc
LDFLAGS = -lcurl -lm

TARGET  = hwmon
SRCDIR  = src
SRCS    = $(SRCDIR)/hwmon.c \
          $(SRCDIR)/display.c \
          $(SRCDIR)/claude.c \
          $(SRCDIR)/cJSON.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/cJSON.o: $(SRCDIR)/cJSON.c
	$(CC) -O2 -Isrc -c -o $@ $<

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
