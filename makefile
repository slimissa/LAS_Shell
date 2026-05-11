# ══════════════════════════════════════════════════════════════════════════════
# Las_shell — Quantitative Trading Shell
# Makefile
#
# Targets:
#   make              — build las_shell (paper mode)
#   make CURL=1       — build las_shell with live broker HTTP support
#   make pipeline     — build C pipeline stage binaries
#   make test         — run full test suite (unit + integration)
#   make test-unit    — C unit tests only
#   make test-int     — integration tests only
#   make run          — build and launch interactive shell
#   make run-sim      — build, start paper sim server, launch shell
#   make clean        — remove all build artifacts
#   make install      — install las_shell to /usr/local/bin
#   make uninstall    — remove installed files
# ══════════════════════════════════════════════════════════════════════════════

CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
TARGET  = las_shell

BUILD_DIR = build
SRC_DIR   = src

# ── OpenSSL (Phase 4.1 audit SHA-256) ─────────────────────────────────────
OPENSSL_CFLAGS  :=
OPENSSL_LDFLAGS :=
ifeq ($(shell pkg-config --exists openssl 2>/dev/null && echo yes),yes)
    OPENSSL_CFLAGS  := -DUSE_OPENSSL_SHA $(shell pkg-config --cflags openssl)
    OPENSSL_LDFLAGS := $(shell pkg-config --libs openssl)
    $(info [audit]  OpenSSL found — EVP SHA-256 enabled)
else
    $(info [audit]  OpenSSL not found — portable SHA-256 fallback)
endif

# ── libcurl (Phase 4.2 broker API) ────────────────────────────────────────
CURL_CFLAGS  :=
CURL_LDFLAGS :=
ifdef CURL
    ifeq ($(shell pkg-config --exists libcurl 2>/dev/null && echo yes),yes)
        CURL_CFLAGS  := -DLAS_SHELL_HAVE_CURL $(shell pkg-config --cflags libcurl)
        CURL_LDFLAGS := $(shell pkg-config --libs libcurl)
        $(info [broker] libcurl found — live broker HTTP enabled)
    else
        CURL_CFLAGS  := -DLAS_SHELL_HAVE_CURL
        CURL_LDFLAGS := -lcurl
        $(info [broker] libcurl fallback — live broker HTTP enabled)
    endif
else
    $(info [broker] Paper mode only. Rebuild with: make CURL=1 for live orders)
endif

CFLAGS  += $(OPENSSL_CFLAGS) $(CURL_CFLAGS)

# ── linker flags ──────────────────────────────────────────────────────────
LDFLAGS  = -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lreadline -lncursesw -lm -lpthread
LDFLAGS += $(OPENSSL_LDFLAGS) $(CURL_LDFLAGS)

# ── sources → objects ─────────────────────────────────────────────────────
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# ── default target ─────────────────────────────────────────────────────────
all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo ""
	@echo "  ╔══════════════════════════════════════════╗"
	@echo "  ║  Las_shell build complete → ./$(TARGET)      ║"
ifdef CURL
	@echo "  ║  Broker: LIVE (libcurl linked)           ║"
else
	@echo "  ║  Broker: PAPER (rebuild with CURL=1)     ║"
endif
	@echo "  ╚══════════════════════════════════════════╝"
	@echo ""

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c include/my_own_shell.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── pipeline C binaries ───────────────────────────────────────────────────
pipeline:
	$(MAKE) -C pipeline/src CC=$(CC) CFLAGS="$(CFLAGS)"

# ── test targets ─────────────────────────────────────────────────────────
test: test-unit test-int

test-unit: $(TARGET) pipeline
	@echo "══ Unit Tests ══════════════════════════════════"
	@cd tests/unit && \
	    gcc -Wall -Wextra -g -o test_parser test_parser.c && ./test_parser && \
	    gcc -Wall -Wextra -g -I../../include -o test_risk_config test_risk_config.c ../../src/risk_config.c -lm && ./test_risk_config && \
	    gcc -Wall -Wextra -g -o test_stream_sub_unit test_stream_sub_unit.c && ./test_stream_sub_unit

test-int: $(TARGET)
	@echo "══ Integration Tests ═══════════════════════"
	@bash tests/integration/tests.sh
	@bash tests/integration/test_audit.sh
	@bash tests/integration/test_broker.sh
	@bash tests/integration/test_pipeline.sh
	@./las_shell tests/integration/test_streaming_sub.sh
	@bash tests/integration/test_templates.sh
	@bash tests/integration/test_risk_config_integration.sh
	@bash tests/integration/test_crash_recovery.sh

# ── convenience targets ───────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

run-audit: $(TARGET)
	./$(TARGET) --audit

run-sim: $(TARGET)
	@echo "Starting paper simulation server on port 8080..."
	@python3 scripts/sim_server.py --port 8080 &
	@SIM_PID=$$!; sleep 0.5; \
	 BROKER_API=http://localhost:8080 ACCOUNT=PAPER ./$(TARGET); \
	 kill $$SIM_PID 2>/dev/null || true

install: $(TARGET) pipeline
	install -m 755 $(TARGET) /usr/local/bin/las_shell
	install -m 755 scripts/quote.sh /usr/local/bin/las_quote
	install -m 755 scripts/market_daemon.sh /usr/local/bin/las_shell_market_daemon
	install -d /usr/local/share/las_shell
	cp -r scripts /usr/local/share/las_shell/
	cp -r strategies /usr/local/share/las_shell/
	cp -r templates /usr/local/share/las_shell/
	cp -r pipeline/python /usr/local/share/las_shell/pipeline/
	cp -r config /usr/local/share/las_shell/
	cp -r docs /usr/local/share/las_shell/
	install -d /usr/local/share/las_shell/logs
	@echo "Installed → /usr/local/bin/las_shell + pipeline binaries + quote"
	@echo "Support files → /usr/local/share/las_shell/"

uninstall:
	rm -f /usr/local/bin/las_shell
	rm -f /usr/local/bin/las_quote
	rm -f /usr/local/bin/las_shell_market_daemon
	rm -rf /usr/local/share/las_shell
	@echo "Uninstalled → /usr/local/bin/las_shell + pipeline binaries + /usr/local/share/las_shell"

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f logs/*.csv logs/backtest_detail/*.csv
	$(MAKE) -C pipeline/src clean 2>/dev/null || true

.PHONY: all pipeline test test-unit test-int run run-audit run-sim install uninstall clean