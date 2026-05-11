CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I../../include
LDFLAGS = -lm
BINDIR  = ../bin
BINS    = universe momentum_filter risk_filter size_positions execute

all: $(BINS)

$(BINDIR):
	mkdir -p $(BINDIR)

universe: universe.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $(BINDIR)/$@ $(LDFLAGS)

momentum_filter: momentum_filter.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $(BINDIR)/$@ $(LDFLAGS)

risk_filter: risk_filter.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $(BINDIR)/$@ $(LDFLAGS)

size_positions: size_positions.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $(BINDIR)/$@ $(LDFLAGS)

execute: execute.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $(BINDIR)/$@ $(LDFLAGS)

clean:
	rm -f $(addprefix $(BINDIR)/,$(BINS))
	rm -rf $(BINDIR)

.PHONY: all clean