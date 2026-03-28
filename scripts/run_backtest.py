#!/usr/bin/env python3
"""Run multi-period backtest simulation."""
import os, sys, random, hashlib
from datetime import datetime

qhome  = os.environ.get("QSHELL_HOME", ".")
logdir     = os.environ.get("LOGDIR", os.path.join(qhome, "logs"))
bt_capital = float(os.environ.get("BT_CAPITAL", 100000))
min_sharpe = float(os.environ.get("MIN_SHARPE", 0.5))
max_dd_pct = float(os.environ.get("MAX_DRAWDOWN_PCT", 20))
strategy   = os.environ.get("STRATEGY", "templates/momentum_daily.sh")
threshold  = int(os.environ.get("PASS_THRESHOLD", 3))

os.makedirs(logdir, exist_ok=True)
detail_dir = f"{logdir}/backtest_detail"
os.makedirs(detail_dir, exist_ok=True)

periods = [
    ("2023-Q1","2023-01-01","2023-03-31","BULL",  0.08),
    ("2023-Q2","2023-04-01","2023-06-30","BEAR", -0.05),
    ("2023-Q3","2023-07-01","2023-09-30","BULL",  0.12),
    ("2023-Q4","2023-10-01","2023-12-31","CHOP",  0.02),
    ("2024-Q1","2024-01-01","2024-03-31","BULL",  0.15),
    ("2024-Q2","2024-04-01","2024-06-30","BEAR", -0.08),
    ("2024-Q3","2024-07-01","2024-09-30","CHOP",  0.01),
    ("2024-Q4","2024-10-01","2024-12-31","BULL",  0.10),
]

def sim(label, regime, mkt_return, capital):
    seed = int(hashlib.md5(f"{label}{strategy}".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    if regime == "BULL":   base_alpha = random.gauss(0.03, 0.04)
    elif regime == "BEAR": base_alpha = random.gauss(0.01, 0.06)
    else:                  base_alpha = random.gauss(-0.02, 0.03)
    strat_return = mkt_return + base_alpha
    days = 63
    vol  = abs(strat_return) * random.uniform(0.8,1.5) / 10
    dr   = [random.gauss(strat_return/days, vol) for _ in range(days)]
    mean_d = sum(dr)/days
    std_d  = (sum((r-mean_d)**2 for r in dr)/days)**0.5
    sharpe = (mean_d*252)/(std_d*(252**0.5)) if std_d > 0 else 0
    cum = [1.0]
    for r in dr: cum.append(cum[-1]*(1+r))
    peak = 1.0; max_dd = 0.0
    for v in cum:
        if v > peak: peak = v
        dd = (peak-v)/peak
        if dd > max_dd: max_dd = dd
    return round(strat_return,4), round(sharpe,2), round(max_dd*100,1), round(capital*strat_return,2)

results_file = f"{logdir}/backtest_results.csv"
with open(results_file, "w") as f:
    f.write("PERIOD,START,END,REGIME,STRAT_RETURN,MKT_RETURN,ALPHA,SHARPE,MAX_DD,RESULT\n")

print(f"{'PERIOD':<12} {'REGIME':<6} {'STRAT':>8} {'MKT':>7} {'ALPHA':>7} {'SHARPE':>7} {'MAXDD':>7} {'RESULT'}")
print("─" * 72)

passed = failed = 0
all_ret = []; all_sh = []

for label,start,end,regime,mkt_return in periods:
    sr, sharpe, max_dd, pnl = sim(label, regime, mkt_return, bt_capital)
    alpha = round(sr - mkt_return, 4)
    all_ret.append(sr); all_sh.append(sharpe)
    pp = sharpe >= min_sharpe and max_dd <= max_dd_pct
    result = "PASS" if pp else "FAIL"
    tag = "✔" if pp else "✘"
    if pp: passed += 1
    else: failed += 1
    print(f"{label:<12} {regime:<6} {sr:>+7.1%} {mkt_return:>+6.1%} {alpha:>+6.1%} {sharpe:>7.2f} {max_dd:>6.1f}% {result} {tag}")
    with open(results_file, "a") as f:
        f.write(f"{label},{start},{end},{regime},{sr:.4f},{mkt_return:.4f},{alpha:.4f},{sharpe:.2f},{max_dd:.1f},{result}\n")
    with open(f"{detail_dir}/{label.replace('-','_')}.csv","w") as f:
        f.write(f"period,{label}\nregime,{regime}\nstrat_return,{sr:.4f}\n"
                f"market_return,{mkt_return:.4f}\nalpha,{alpha:.4f}\n"
                f"sharpe,{sharpe:.2f}\nmax_drawdown_pct,{max_dd:.1f}\n"
                f"pnl_usd,{pnl:.2f}\nresult,{result}\n")

n = len(all_ret)
avg_ret   = sum(all_ret)/n
avg_sh    = sum(all_sh)/n
total_ret = 1.0
for r in all_ret: total_ret *= (1+r)
total_ret -= 1

print("─" * 72)
print(f"\n── Backtest Summary ──")
print(f"  Periods: {n}  Passed: {passed}  Failed: {failed}  Pass rate: {passed/n:.0%}")
print(f"  Avg Return: {avg_ret:+.2%}  Total: {total_ret:+.2%}  Avg Sharpe: {avg_sh:.2f}")

with open(f"{logdir}/backtest_summary.csv","w") as f:
    f.write(f"periods_tested,{n}\nperiods_passed,{passed}\nperiods_failed,{failed}\n"
            f"pass_rate,{passed/n:.3f}\navg_period_return,{avg_ret:.4f}\n"
            f"total_return,{total_ret:.4f}\navg_sharpe,{avg_sh:.2f}\n"
            f"strategy,{strategy}\nrun_timestamp,{datetime.now().isoformat()}\n")

sys.exit(0 if passed >= threshold else 1)