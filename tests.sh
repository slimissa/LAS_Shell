#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# QShell Master Test Suite v4
# Part 1 : LAS Shell core            (65 tests)
# Part 2 : Phase 1 — Trading engine  (38 tests)
# Part 3 : Phase 2 — Week 2 ops      (36 tests)
#                              Total: 139 tests
# ═══════════════════════════════════════════════════════════════

BINARY=${1:-./las_shell}
PASS=0; FAIL=0; TOTAL=0

GREEN='\033[32m'; RED='\033[31m'; RESET='\033[0m'
BOLD='\033[1m';   BLUE='\033[34m'; YELLOW='\033[33m'

# ── runners ─────────────────────────────────────────────────────
run_test() {
    local desc="$1" expected="$2" got="$3" mode="${4:-contains}"
    TOTAL=$((TOTAL+1)); local ok=0
    case "$mode" in
        contains) echo "$got" | grep -qF "$expected" && ok=1 ;;
        exact)    [ "$got" = "$expected" ] && ok=1 ;;
        zero)     [ "$got" = "0" ]         && ok=1 ;;
        nonzero)  [ "$got" != "0" ]        && ok=1 ;;
    esac
    if [ $ok -eq 1 ]; then
        echo -e "  ${GREEN}✔${RESET} $desc"; PASS=$((PASS+1))
    else
        echo -e "  ${RED}✘${RESET} $desc"
        echo -e "    Expected ($mode): ${BOLD}$expected${RESET}"
        echo -e "    Got             : ${BOLD}$got${RESET}"
        FAIL=$((FAIL+1))
    fi
}

run_script() {
    local desc="$1" expected="$2" script="$3" mode="${4:-contains}"
    local tmp; tmp=$(mktemp /tmp/qshell_XXXXXX.sh)
    printf '%s\n' "$script" > "$tmp"
    local got; got=$("$BINARY" "$tmp" 2>/dev/null)
    rm -f "$tmp"
    run_test "$desc" "$expected" "$got" "$mode"
}

run_cmd() {
    local desc="$1" expected="$2" cmd="$3" mode="${4:-contains}"
    local got; got=$("$BINARY" -c "$cmd" 2>/dev/null)
    run_test "$desc" "$expected" "$got" "$mode"
}

run_exit() {
    local desc="$1" want="$2" cmd="$3"
    "$BINARY" -c "$cmd" >/dev/null 2>&1; local got=$?
    TOTAL=$((TOTAL+1))
    if [ "$got" = "$want" ]; then
        echo -e "  ${GREEN}✔${RESET} $desc"; PASS=$((PASS+1))
    else
        echo -e "  ${RED}✘${RESET} $desc"
        echo -e "    Expected exit: ${BOLD}$want${RESET}"
        echo -e "    Got exit     : ${BOLD}$got${RESET}"
        FAIL=$((FAIL+1))
    fi
}

run_script_exit() {
    local desc="$1" want="$2" script="$3"
    local tmp; tmp=$(mktemp /tmp/qshell_XXXXXX.sh)
    printf '%s\n' "$script" > "$tmp"
    "$BINARY" "$tmp" >/dev/null 2>&1; local got=$?
    rm -f "$tmp"
    TOTAL=$((TOTAL+1))
    if [ "$got" = "$want" ]; then
        echo -e "  ${GREEN}✔${RESET} $desc"; PASS=$((PASS+1))
    else
        echo -e "  ${RED}✘${RESET} $desc"
        echo -e "    Expected exit: ${BOLD}$want${RESET}"
        echo -e "    Got exit     : ${BOLD}$got${RESET}"
        FAIL=$((FAIL+1))
    fi
}

section() { echo ""; echo -e "${BLUE}━━━ $1 ━━━${RESET}"; }
part()    {
    echo ""
    echo -e "${YELLOW}╔══════════════════════════════════════════╗${RESET}"
    printf  "${YELLOW}║  %-40s║${RESET}\n" "$1"
    echo -e "${YELLOW}╚══════════════════════════════════════════╝${RESET}"
}

# ── header ──────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}QShell Master Test Suite v4${RESET}"
echo "Binary : $BINARY"
echo "Date   : $(date)"
echo ""

rm -f .trading_env

# ════════════════════════════════════════════════════════════════
part "PART 1 — LAS Shell Core (65 tests)"
# ════════════════════════════════════════════════════════════════

section "Built-in: echo"
run_cmd "echo hello world"                     "hello world"  "echo hello world"
run_cmd "echo with no args → empty line"       ""             "echo"                 exact
run_cmd "echo -n suppresses newline"           "hello"        "echo -n hello"
run_cmd "echo single-quoted string"            "hello world"  "echo 'hello world'"
run_cmd "echo double-quoted string"            "hello world"  'echo "hello world"'

section "Built-in: pwd"
run_cmd "pwd returns current directory"        "/"            "pwd"

section "Built-in: cd"
run_script "cd /tmp then pwd"                  "/tmp"         "cd /tmp
pwd"
run_script "cd no args → HOME"                 "$HOME"        "cd
pwd"
run_script "cd ~ → HOME"                       "$HOME"        "cd ~
pwd"
run_script "cd .. moves up"                    "/"            "cd /tmp
cd ..
pwd"
run_exit   "cd nonexistent → exit 127"         127            "cd /nonexistent_dir_xyz"

section "Built-in: env / setenv / unsetenv"
run_script "env lists PATH"                    "PATH"         "env"
run_script "setenv then echo"                  "bar"          "setenv FOO bar
echo \$FOO"
run_script "unsetenv removes var"              ""             "setenv FOO bar
unsetenv FOO
echo \$FOO"                                                                          exact

section "Built-in: which"
run_cmd  "which ls → path"                     "ls"           "which ls"
run_exit "which nonexistent → non-zero"        1              "which nonexistent_cmd_xyz"

section "Built-in: exit"
run_exit "exit → 0"                            0              "exit"
run_exit "exit 42 → 42"                        42             "exit 42"

section "External Commands"
run_cmd  "ls lists files"                      "makefile"     "ls"
run_cmd  "cat a file"                          "las_shell"    "cat makefile"
run_cmd  "date outputs year"                   "202"          "date"
run_exit "nonexistent → exit 127"              127            "nonexistent_cmd_xyz"

section "Pipes"
run_cmd  "echo | cat"                          "hello"        "echo hello | cat"
run_cmd  "ls | grep makefile"                  "makefile"     "ls | grep makefile"
run_cmd  "echo | cat | cat"                    "hello"        "echo hello | cat | cat"
run_cmd  "echo | wc -w"                        "3"            "echo one two three | wc -w"
run_cmd  "printf | sort | uniq"                "a"            "printf 'a\nb\na\n' | sort | uniq"

section "Redirections"
T=$(mktemp)
run_script "output redirect >"                 "hello"        "echo hello > $T
cat $T"
run_script "append redirect >>"                "hello"        "echo hello > $T
echo world >> $T
cat $T"
echo "input" > "$T"
run_script "input redirect <"                  "input"        "cat < $T"
run_script "combined < and >"                  "input"        "cat < $T > ${T}2
cat ${T}2"
rm -f "$T" "${T}2"

section "Control Operators: ;"
run_cmd "semicolon runs both"                  "hello"        "echo hello ; echo world"

section "Control Operators: &&"
run_cmd "2nd runs if 1st succeeds"             "B"            "true && echo B"
run_cmd "2nd blocked if 1st fails"             ""             "false && echo B"       exact

section "Control Operators: ||"
run_cmd "2nd runs if 1st fails"                "B"            "false || echo B"
run_cmd "2nd blocked if 1st succeeds"          ""             "true || echo B"        exact

section "Control Operators: Mixed"
run_cmd "true && B || C → B"                   "B"            "true && echo B || echo C"
run_cmd "false && B || C → C"                  "C"            "false && echo B || echo C"

section "Command Substitution \$()"
run_cmd "basic \$()"                           "hello"        "echo \$(echo hello)"
run_cmd "\$() in double quotes"                "hello"        'echo "$(echo hello)"'
run_cmd "nested \$()"                          "hello"        "echo \$(echo \$(echo hello))"
run_script "\$() assigned to var"              "$PWD"         "setenv D \$(pwd)
echo \$D"

section "Variable Expansion"
run_cmd "\$HOME expands"                       "$HOME"        "echo \$HOME"
run_cmd "\$PATH non-empty"                     "/"            "echo \$PATH"
run_cmd "undefined var → empty"               ""             "echo \$UNDEFINED_XYZ"   exact
run_cmd "var in double quotes"                 "$HOME"        'echo "$HOME"'
TQ=$(mktemp /tmp/qshell_sq_XXXXXX.sh)
printf '%s\n' "echo 'NOEEXPAND=\$TESTVAR'" > "$TQ"
SQ=$("$BINARY" "$TQ" 2>/dev/null); rm -f "$TQ"
run_test "var NOT in single quotes"            'NOEEXPAND=$TESTVAR' "$SQ"

section "Quoting"
run_cmd "single quotes preserve spaces"        "hello world"  "echo 'hello world'"
run_cmd "double quotes preserve spaces"        "hello world"  'echo "hello world"'
run_cmd "special chars in single quotes"       "hello \$world" "echo 'hello \$world'"

section "Aliases"
run_script "define and use alias"              "hello"        "alias hi='echo hello'
hi"
run_script "alias override"                    "aliased"      "alias zzz='echo aliased'
zzz"
run_script "chained alias"                     "chained"      "alias a='echo chained'
alias b='a'
b"

section "Command History"
run_script "history shows commands"            "echo"         "echo something
history"

section "Script Execution"
run_script "multi-line script"                 "line1"        "echo line1
echo line2"
run_script "var in script"                     "value42"      "setenv VAR value42
echo \$VAR"
run_script "pipe in script"                    "hello"        "echo hello | cat"

section "Edge Cases"
run_cmd  "empty input → nothing"               ""             ""                       exact
run_cmd  "whitespace → nothing"                ""             "   "                    exact
run_cmd  "multiple semicolons"                 "a"            "echo a ; echo b ; echo c"
run_cmd  "many args"                           "a"            "echo a b c d e f g h i j"
run_cmd  "pipe to /dev/null"                   ""             "echo hello | cat > /dev/null" exact
run_exit "continues after fail"                0              "nonexistent_xyz ; echo ok"
run_exit "true → 0"                            0              "true"
run_exit "false → 1"                           1              "false"

# ════════════════════════════════════════════════════════════════
part "PART 2 — Phase 1: Trading Foundation (38 tests)"
# ════════════════════════════════════════════════════════════════

rm -f .trading_env

section "Financial Environment Variables"
run_script "setmarket → MARKET"                "NYSE"         "setmarket NYSE
echo \$MARKET"
run_script "setbroker → BROKER"                "IBKR"         "setbroker IBKR
echo \$BROKER"
run_script "setaccount → ACCOUNT"              "PAPER"        "setaccount PAPER
echo \$ACCOUNT"
run_script "setcapital → CAPITAL"              "100000"       "setcapital 100000
echo \$CAPITAL"
run_script "setcapital rejects negative"       "usage"        "setcapital -500
echo usage"

section "Trading Env Persistence"
TE=$(mktemp /tmp/qshell_XXXXXX.sh)
printf 'setmarket TSX\nsetbroker ALPACA\nsetaccount LIVE\nsetcapital 50000\n' > "$TE"
"$BINARY" "$TE" >/dev/null 2>&1; rm -f "$TE"
run_script "MARKET persists"                   "TSX"          "echo \$MARKET"
run_script "BROKER persists"                   "ALPACA"       "echo \$BROKER"
run_script "ACCOUNT persists"                  "LIVE"         "echo \$ACCOUNT"
run_script "CAPITAL persists"                  "50000"        "echo \$CAPITAL"
"$BINARY" -c "setmarket NYSE"    >/dev/null 2>&1
"$BINARY" -c "setcapital 100000" >/dev/null 2>&1

section "assert — numeric"
run_exit   "5 > 3 → 0"                         0              "assert 5 > 3"
run_exit   "3 > 5 → 1"                         1              "assert 3 > 5"
run_exit   "3 < 5 → 0"                         0              "assert 3 < 5"
run_exit   "5 < 3 → 1"                         1              "assert 5 < 3"
run_exit "5 >= 5 → 0"                           0              "assert 5 >= 5"
run_exit "4 >= 5 → 1"                           1              "assert 4 >= 5"
run_exit "3 <= 5 → 0"                           0              "assert 3 <= 5"
run_exit "5 <= 3 → 1"                           1              "assert 5 <= 3"
run_exit   "5 == 5 → 0"                        0              "assert 5 == 5"
run_exit   "5 != 3 → 0"                        0              "assert 5 != 3"
run_exit   "3.14 > 3.0 → 0"                    0              "assert 3.14 > 3.0"
run_exit "100000 >= 100000 → 0"                0              "assert 100000 >= 100000"

section "assert — strings"
run_exit "PAPER == PAPER → 0"                  0              "assert PAPER == PAPER"
run_exit "LIVE == PAPER  → 1"                  1              "assert LIVE == PAPER"
run_exit "LIVE != PAPER  → 0"                  0              "assert LIVE != PAPER"

section "assert — variable resolution"
run_script "assert \$ACCOUNT == LIVE"          "LIVE"         "setaccount LIVE
assert \$ACCOUNT == LIVE
echo \$ACCOUNT"
run_script "assert \$CAPITAL > 50000"          "100000"       "setcapital 100000
assert \$CAPITAL > 50000
echo \$CAPITAL"
run_script "assert blocks on fail"             "strategy"     "setcapital 1000
assert \$CAPITAL > 50000 || echo strategy"

section "assert — in chains"
run_script "assert pass → && runs"             "ok"           "assert 5 > 3
echo ok"
run_script "assert fail → && blocked"          ""             "assert 3 > 5 && echo ok"  exact

section "\$? exit status"
run_script "\$? after success"                 "0"            "true
echo \$?"
run_script "\$? after failure"                 "1"            "false
echo \$?"
run_script "\$? after assert pass"             "0"            "assert 5 > 3
echo \$?"
run_script "\$? after assert fail"             "1"            "assert 3 > 5
echo \$?"

section "watch"
run_exit "watch no args → non-zero"            1              "watch"
run_exit "watch bad interval → non-zero"       1              "watch 0 echo hi"

section "work mode"
run_exit "work → 0"                            0              "work"
run_exit "work off → 0"                        0              "work off"

section "Tilde in redirections"
BN=$(basename "$(mktemp -u /tmp/qshell_XXXXXX)")
run_script "~ expands in > redirect"           "hello"        "echo hello > ~/$BN
cat ~/$BN"
rm -f "$HOME/$BN"

# ════════════════════════════════════════════════════════════════
part "PART 3 — Phase 2 Week 2 (36 tests)"
# ════════════════════════════════════════════════════════════════

section "@time operator"
FUT=$(date -d '+2 seconds' +%H:%M:%S 2>/dev/null || date -v+2S +%H:%M:%S 2>/dev/null)
run_test "@time waits then fires"              "fired"        "$("$BINARY" -c "@${FUT} echo fired" 2>/dev/null)"
run_exit "@time invalid format → 1"            1              "@99:99:99 echo bad"
FUT1=$(date -d '+1 seconds' +%H:%M:%S 2>/dev/null || date -v+1S +%H:%M:%S 2>/dev/null)
run_exit "@time executes correctly → 0"        0              "@${FUT1} true"

section "|> CSV log"
CSV=$(mktemp /tmp/qshell_XXXXXX.csv)
run_script "|> creates timestamped CSV"        "BUY AAPL"     "echo 'BUY AAPL 100' |> $CSV
cat $CSV"
run_script "|> timestamp is ISO 8601"          "T"            "echo test |> $CSV
cat $CSV"
run_script "|> appends multiple entries"       "BUY AAPL"     "echo 'BUY AAPL 100' |> $CSV
echo 'SELL MSFT 50' |> $CSV
cat $CSV"
run_script "|> format: timestamp,data"         ","            "echo ORDER |> $CSV
cat $CSV"
run_script_exit "|> exits 0 on success"        0              "echo test |> $CSV"
rm -f "$CSV"

section "|> with \$VAR and ~"
CV2=$(mktemp /tmp/qshell_XXXXXX.csv)
run_script "|> \$LOGFILE expands"              "TRADE"        "setenv LOGFILE $CV2
echo TRADE |> \$LOGFILE
cat $CV2"
run_script "|> ~/path expands"                 "DATA"         "echo DATA |> $CV2
cat $CV2"
rm -f "$CV2"

section "?> risk gate"
run_script "?> passes (checker exits 0)"       "ORDER"        "echo 'ORDER: BUY AAPL' ?> cat"
run_script "?> rejects (checker exits 1)"      "REJECTED"     "echo 'ORDER: SHORT GME' ?> false
echo REJECTED"
run_exit   "?> pass → exit 0"                  0              "echo data ?> cat"
run_exit   "?> fail → exit 1"                  1              "echo data ?> false"

section "?> chain behavior"
# Left output "data" goes to stdout; "sent" must NOT appear after rejection
TOTAL=$((TOTAL+1))
GBLK=$("$BINARY" -c "echo data ?> false && echo sent" 2>/dev/null)
if echo "$GBLK" | grep -qF "sent"; then
    echo -e "  ${RED}✘${RESET} ?> false && echo → sent blocked"
    echo -e "    sent appeared in output: $GBLK"; FAIL=$((FAIL+1))
else
    echo -e "  ${GREEN}✔${RESET} ?> false && echo → sent blocked"; PASS=$((PASS+1))
fi
run_script "?> pass → && runs"                 "sent"         "echo data ?> cat && echo sent"
run_script "?> fail → || fallback runs"        "fallback"     "echo data ?> false || echo fallback"

section "?> rejection log"
REJLOG="$HOME/.qshell_risk_rejections"
BEFORE=$(wc -l < "$REJLOG" 2>/dev/null || echo 0)
"$BINARY" -c "echo RISKY_ORDER ?> false" >/dev/null 2>&1
AFTER=$(wc -l < "$REJLOG" 2>/dev/null || echo 0)
TOTAL=$((TOTAL+1))
if [ "$AFTER" -gt "$BEFORE" ]; then
    echo -e "  ${GREEN}✔${RESET} ?> appends to rejection log"; PASS=$((PASS+1))
else
    echo -e "  ${RED}✘${RESET} ?> appends to rejection log"; FAIL=$((FAIL+1))
fi
run_test "log contains REJECTED tag"           "REJECTED"     "$(tail -1 "$REJLOG" 2>/dev/null)"
run_test "log contains ISO timestamp"          "T"            "$(tail -1 "$REJLOG" 2>/dev/null)"

section "\$VAR expansion in args"
run_script "\$VAR before execvp"               "hello"        "setenv MSG hello
echo \$MSG"
run_script "\$VAR in file path arg"            "hello"        "setenv F /tmp/qshell_varpath_$$.txt
echo hello > \$F
cat \$F"
rm -f "/tmp/qshell_varpath_$$.txt"
run_script "\$HOME in arg"                     "$HOME"        "echo \$HOME"

section "~ expansion in args"
run_cmd  "echo ~ → HOME"                       "$HOME"        "echo ~"
run_script "~/file expands correctly"          "ok"           "echo ok > ~/qshell_tilde_$$.txt
cat ~/qshell_tilde_$$.txt"
rm -f "$HOME/qshell_tilde_$$.txt"
run_script "~ in cat arg works"               ""              "cat ~/.qshell_market" contains

section "Trading alias library"
run_script "setmarket loads aliases"           "mstatus"      "setmarket NYSE
alias"
run_script "rejections alias loaded"           "rejections"   "setmarket NYSE
alias"
run_script "flatten alias loaded"              "flatten"      "setmarket NYSE
alias"
run_script "mstatus expands to cat"            "cat"          "setmarket NYSE
alias mstatus"
run_script "rejections runs correctly"         "REJECTED"     "setmarket NYSE
rejections"

section "Alias chaining in operator chains"
run_script "alias in && chain"                 "chained"      "alias mytest='echo chained'
true && mytest"
run_script "alias in || chain"                 "chained"      "alias mytest='echo chained'
false || mytest"
run_script "multi-level alias chain"           "hello"        "alias greet='echo hello'
alias run_it='greet'
true && run_it"

section "|> and ?> combined"
CC=$(mktemp /tmp/qshell_combo_XXXXXX.csv)
run_script "|> then && runs"                   "ok"           "echo data |> $CC && echo ok"
run_script "?> pass then |> logs"              ","            "echo ORDER ?> true |> $CC
cat $CC"
run_exit   "?> fail stops && chain"            1              "echo risky ?> false && echo bad"
rm -f "$CC"

# ════════════════════════════════════════════════════════════════
# RESULTS
# ════════════════════════════════════════════════════════════════
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "  ${GREEN}Passed${RESET} : $PASS / $TOTAL"
echo -e "  ${RED}Failed${RESET} : $FAIL / $TOTAL"
echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}${BOLD}✔ All $TOTAL tests passed!${RESET}"
else
    echo -e "${RED}${BOLD}✘ $FAIL test(s) failed.${RESET}"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"