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
#   make install      — install las_shell to /usr/local
#   make uninstall    — remove installed files
# ══════════════════════════════════════════════════════════════════════════

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
SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
HEADERS := $(wildcard include/*.h)

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

# MK8-FIX: depend on ALL headers so any header change triggers recompilation
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── pipeline C binaries ───────────────────────────────────────────────────
pipeline:
	$(MAKE) -C pipeline/src CC=$(CC) CFLAGS="$(CFLAGS)"
	@cp pipeline/bin/universe pipeline/universe_c

# ── test targets ─────────────────────────────────────────────────────────

# MK4-FIX: track failures across all test suites, exit non-zero if any fail
test: test-unit test-int
	@echo ""
	@echo "══ All Tests Complete ═════════════════════"

test-unit: $(TARGET) pipeline
	@echo "══ Unit Tests ══════════════════════════════════"
	@FAIL=0; \
	cd tests/unit && { \
	    gcc -Wall -Wextra -g -o test_parser test_parser.c && ./test_parser || FAIL=1; \
	    gcc -Wall -Wextra -g -I../../include -o test_risk_config test_risk_config.c ../../src/risk_config.c -lm && ./test_risk_config || FAIL=1; \
	    gcc -Wall -Wextra -g -o test_stream_sub_unit test_stream_sub_unit.c && ./test_stream_sub_unit || FAIL=1; \
	    exit $$FAIL; \
	}

test-int: $(TARGET) pipeline
	@echo "══ Integration Tests ═══════════════════════"
	@FAIL=0; \
	bash tests/integration/tests.sh                        || FAIL=1; \
	bash tests/integration/test_audit.sh                   || FAIL=1; \
	bash tests/integration/test_broker.sh                  || FAIL=1; \
	bash tests/integration/test_pipeline.sh                || FAIL=1; \
	./las_shell tests/integration/test_streaming_sub.sh    || FAIL=1; \
	bash tests/integration/test_templates.sh               || FAIL=1; \
	bash tests/integration/test_risk_config_integration.sh || FAIL=1; \
	bash tests/integration/test_crash_recovery.sh          || FAIL=1; \
	bash tests/integration/test_live_feed.sh               || FAIL=1; \
	exit $$FAIL

# ── convenience targets ───────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

run-audit: $(TARGET)
	./$(TARGET) --audit

# MK5-FIX: trap EXIT/INT/TERM so sim_server is killed even if shell crashes
run-sim: $(TARGET)
	@echo "Starting paper simulation server on port 8080..."
	@python3 scripts/sim_server.py --port 8080 & \
	SIM_PID=$$!; \
	trap "kill $$SIM_PID 2>/dev/null || true" EXIT INT TERM; \
	sleep 0.5; \
	BROKER_API=http://localhost:8080 ACCOUNT=PAPER ./$(TARGET); \
	kill $$SIM_PID 2>/dev/null || true

# ── install ───────────────────────────────────────────────────────────────
# MK1-FIX: install C pipeline binaries alongside Python stages
# MK2-FIX: use install (not cp -r) with explicit permissions
# MK8-FIX: warn before overwriting existing installation
install: $(TARGET) pipeline
	@if [ -f /usr/local/bin/las_shell ] && [ -z "$(FORCE)" ]; then \
		echo ""; \
		echo "  ╔══════════════════════════════════════════════╗"; \
		echo "  ║  WARNING: las_shell is already installed    ║"; \
		echo "  ║  at /usr/local/bin/las_shell                 ║"; \
		echo "  ║                                              ║"; \
		echo "  ║  Use 'make install FORCE=1' to overwrite     ║"; \
		echo "  ║  or 'make uninstall' first to remove it      ║"; \
		echo "  ╚══════════════════════════════════════════════╝"; \
		echo ""; \
		exit 1; \
	fi
	install -m 755 $(TARGET) /usr/local/bin/las_shell
	install -m 755 scripts/quote.sh /usr/local/bin/las_quote
	install -m 755 scripts/market_daemon.sh /usr/local/bin/las_shell_market_daemon
	install -d /usr/local/share/las_shell
	install -d /usr/local/share/las_shell/scripts
	install -m 644 scripts/*.py /usr/local/share/las_shell/scripts/
	install -m 755 scripts/*.sh /usr/local/share/las_shell/scripts/
	install -d /usr/local/share/las_shell/strategies
	install -m 644 strategies/*.sh /usr/local/share/las_shell/strategies/
	install -d /usr/local/share/las_shell/templates
	install -m 644 templates/*.sh /usr/local/share/las_shell/templates/
	install -d /usr/local/share/las_shell/pipeline/python
	install -m 644 pipeline/python/*.py /usr/local/share/las_shell/pipeline/python/
	install -d /usr/local/share/las_shell/pipeline/bin
	install -m 755 pipeline/bin/* /usr/local/share/las_shell/pipeline/bin/ 2>/dev/null || true
	install -d /usr/local/share/las_shell/config
	install -m 644 config/*.example /usr/local/share/las_shell/config/
	install -d /usr/local/share/las_shell/docs
	install -m 644 docs/*.md /usr/local/share/las_shell/docs/
	install -d /usr/local/share/las_shell/logs
	@echo "Installed → /usr/local/bin/las_shell + pipeline binaries + quote"
	@echo "Support files → /usr/local/share/las_shell/"

uninstall:
	rm -f /usr/local/bin/las_shell
	rm -f /usr/local/bin/las_quote
	rm -f /usr/local/bin/las_shell_market_daemon
	rm -rf /usr/local/share/las_shell
	@echo "Uninstalled → /usr/local/bin/las_shell + /usr/local/share/las_shell"

# ── clean ─────────────────────────────────────────────────────────────────
# MK9-FIX: also remove test binaries and stale FIFOs
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f logs/*.csv logs/backtest_detail/*.csv
	rm -f tests/unit/test_parser tests/unit/test_risk_config tests/unit/test_stream_sub_unit
	rm -f /tmp/las_shell_stream_* /tmp/las_shell_unit_* 2>/dev/null || true
	$(MAKE) -C pipeline/src clean 2>/dev/null || true

.PHONY: all pipeline test test-unit test-int run run-audit run-sim install uninstall clean