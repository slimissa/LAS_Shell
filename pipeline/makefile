# QShell Pipeline — C stages build
CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lm

BINS = universe risk_filter

all: $(BINS)
	@echo "Built: $(BINS)"
	@echo "Test:  ./universe | python3 momentum_filter.py | ./risk_filter | python3 size_positions.py | python3 execute.py"

universe:    universe.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

risk_filter: risk_filter.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(BINS)

test: all
	@echo "=== Full pipeline test ==="
	./universe --top 5 | python3 momentum_filter.py --threshold 0.1 | \
	    ./risk_filter | python3 size_positions.py --model signal | \
	    python3 execute.py --mode paper --dry_run

.PHONY: all clean test