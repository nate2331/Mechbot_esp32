"""Audit a Maker_PWM_Frequency_Test serial log without third-party packages."""
import argparse, csv, hashlib, io, json, math, re, statistics
from pathlib import Path
from collections import defaultdict, Counter

parser = argparse.ArgumentParser()
parser.add_argument("log")
parser.add_argument("output")
parser.add_argument("--sequence", type=int, required=True)
args = parser.parse_args()
src = Path(args.log)
out = Path(args.output)
out.mkdir(parents=True, exist_ok=True)
raw = src.read_bytes()
text = raw.decode("utf-8-sig")
summaries, samples, plans = [], defaultdict(list), {}
headers = {}
for line_number, line in enumerate(text.splitlines(), 1):
    line = line.strip()
    if line.startswith("# PLAN "):
        kv = dict(re.findall(r"(\w+)=([^\s]+)", line))
        plans[int(kv["sequence"])] = kv
    if not line.startswith(("summary,", "sample,")):
        continue
    fields = next(csv.reader([line]))
    kind = fields[0]
    if fields[1] == "sequence":
        if kind in headers:
            assert headers[kind] == fields, ("header changed", line_number)
        headers[kind] = fields
        continue
    assert len(fields) == len(headers[kind]), ("column mismatch", line_number)
    row = dict(zip(headers[kind], fields))
    row["_line"] = line_number
    if kind == "summary":
        summaries.append(row)
    else:
        samples[(int(row["sequence"]), int(row["trial"]))].append(
            {k:int(v) for k,v in row.items() if k not in ("sample",)}
        )

keys = [(int(r["sequence"]),int(r["trial"])) for r in summaries]
assert len(keys) == len(set(keys)), "duplicate trial summaries"
assert set(keys) == set(samples), "sample/summary trial mismatch"
metrics = []
for r in summaries:
    seq, trial = int(r["sequence"]), int(r["trial"])
    pts = samples[(seq,trial)]
    cpr = float(plans[seq]["cpr"])
    assert len(pts) == 81, (seq,trial,"unexpected sample count",len(pts))
    assert pts[0]["t_us"] == 0 and pts[0]["ticks"] == 0
    assert all(b["t_us"]>a["t_us"] for a,b in zip(pts,pts[1:]))
    assert all(b["valid_edges"]>=a["valid_edges"] and b["invalid"]>=a["invalid"]
               for a,b in zip(pts,pts[1:]))
    last = pts[-1]
    assert last["ticks"] == int(r["ticks"])
    assert last["valid_edges"] == int(r["valid_edges"])
    assert last["invalid"] == int(r["invalid"])
    assert last["t_us"] == int(r["elapsed_us"])
    five = next(p for p in pts if p["t_us"]>=5_000_000)
    full = last["ticks"]*60_000_000/(cpr*last["t_us"])
    early = five["ticks"]*60_000_000/(cpr*five["t_us"])
    tail = (last["ticks"]-five["ticks"])*60_000_000/(cpr*(last["t_us"]-five["t_us"]))
    for key, expected in [("full_rpm",full),("first5_rpm",early),("tail_rpm",tail)]:
        assert abs(float(r[key])-expected)<=0.00000051, (seq,trial,key)
    assert r["hz_requested"] == r["hz_readback"]
    # Valid transitions can include an occasional opposite-direction step.
    excess = last["valid_edges"]-abs(last["ticks"])
    assert excess >= 0 and excess % 2 == 0, "inconsistent edge/tick counters"
    assert last["ticks"]*int(r["dir"])>0
    def window(a,b):
        start = next(p for p in pts if p["t_us"]>=a*1_000_000)
        stop = next(p for p in pts if p["t_us"]>=b*1_000_000)
        return abs(stop["ticks"]-start["ticks"])*60_000_000/(cpr*(stop["t_us"]-start["t_us"]))
    item = {k:r[k] for k in headers["summary"]}
    item.update({
        "rpm_1_to_2":window(1,2), "rpm_5_to_6":window(5,6), "rpm_7_to_8":window(7,8),
        "sample_count":len(pts), "source_line":r["_line"],
        "opposite_steps":excess//2,
        "first_excess_observed_us":next((p["t_us"] for p in pts if p["valid_edges"]>abs(p["ticks"])),None),
        "max_sample_gap_us":max(b["t_us"]-a["t_us"] for a,b in zip(pts,pts[1:]))
    })
    metrics.append(item)

sweep = [r for r in metrics if int(r["sequence"])==args.sequence]
assert len(sweep)==int(plans[args.sequence]["trials"])
frequencies=sorted({int(r["hz_requested"]) for r in sweep})
block_ids=sorted({int(r["block"]) for r in sweep})
assert block_ids==list(range(1,max(block_ids)+1))
assert len(sweep)==len(frequencies)*2*len(block_ids)
conditions = Counter((int(r["block"]),int(r["hz_requested"]),int(r["dir"])) for r in sweep)
assert len(conditions)==len(sweep) and set(conditions.values())=={1}
assert set(conditions)=={(b,h,d) for b in block_ids for h in frequencies for d in [1,-1]}
expected_profile=plans[args.sequence]["profile"]
expected_bits=8 if expected_profile=="history" else 9
expected_clock={"history":"AUTO","apb":"APB","ref":"REF_TICK"}[expected_profile]
expected_duty=int(plans[args.sequence]["duty256"])
assert all(r["wheel"]==plans[args.sequence]["wheel"] and
           r["profile"]==expected_profile and r["clock_requested"]==expected_clock and
           int(r["bits"])==expected_bits and
           int(r["duty_counts"])==expected_duty*(2**(expected_bits-8)) and
           int(r["duty_denominator"])==2**expected_bits for r in sweep)
assert all(r["reason"]=="complete" for r in sweep), "incomplete sweep"
groups = []
for hz in frequencies:
    for direction in [1,-1]:
        rows=[r for r in sweep if int(r["hz_requested"])==hz and int(r["dir"])==direction]
        v=[abs(float(r["tail_rpm"])) for r in rows]
        groups.append(dict(hz=hz,direction=direction,n=len(v),tail_mean=statistics.mean(v),
          tail_sd=statistics.stdev(v) if len(v)>1 else None,tail_min=min(v),tail_max=max(v),
          first5_mean=statistics.mean(abs(float(r["first5_rpm"])) for r in rows),
          tail_rise_5_6_to_7_8=statistics.mean(r["rpm_7_to_8"]-r["rpm_5_to_6"] for r in rows),
          tail_by_block=v))
means = {hz:statistics.mean(abs(float(r["tail_rpm"])) for r in sweep if int(r["hz_requested"])==hz)
         for hz in frequencies}
paired = []
comparisons=[(248,20000),(248,1000),(20000,5000),(1000,20000)]
comparisons += [(h,248) for h in frequencies if h not in [248,1000,5000,20000]]
for low, high in comparisons:
    if low not in frequencies or high not in frequencies:
        continue
    differences=[]
    for block in block_ids:
        for direction in [1,-1]:
            a=next(r for r in sweep if int(r["hz_requested"])==low and int(r["block"])==block and int(r["dir"])==direction)
            b=next(r for r in sweep if int(r["hz_requested"])==high and int(r["block"])==block and int(r["dir"])==direction)
            differences.append(abs(float(a["tail_rpm"]))-abs(float(b["tail_rpm"])))
    paired.append(dict(a_hz=low,b_hz=high,mean_difference=statistics.mean(differences),
                       min_difference=min(differences),max_difference=max(differences),
                       differences_by_block_direction=differences))
audit = dict(source=str(src),sha256=hashlib.sha256(raw).hexdigest(),
             selected_sequence=args.sequence,profile=expected_profile,
             frequencies=frequencies,blocks=block_ids,
             summaries=len(summaries),sweep_trials=len(sweep),total_samples=sum(map(len,samples.values())),
             status_counts=dict(Counter(r["reason"] for r in summaries)),
             invalid_total=sum(int(r["invalid"]) for r in summaries),
             opposite_step_trials=[{"sequence":r["sequence"],"trial":r["trial"],
                                    "opposite_steps":r["opposite_steps"],
                                    "first_excess_observed_us":r["first_excess_observed_us"]}
                                   for r in metrics if r["opposite_steps"]],
             sample_counts=sorted({len(v) for v in samples.values()}),
             powered_us_range=[min(int(r["elapsed_us"]) for r in sweep),max(int(r["elapsed_us"]) for r in sweep)],
             off_us_range=[min(int(r["off_before_us"]) for r in sweep),max(int(r["off_before_us"]) for r in sweep)],
             first_observed_edge_us_range=[min(int(r["first_edge_observed_us"]) for r in sweep),max(int(r["first_edge_observed_us"]) for r in sweep)],
             maximum_5_6_to_7_8_rise=max(r["rpm_7_to_8"]-r["rpm_5_to_6"] for r in sweep),
             maximum_absolute_late_window_change=max(abs(r["rpm_7_to_8"]-r["rpm_5_to_6"]) for r in sweep),
             balanced_complete_blocks=True,raw_summary_recalculation="all match to printed rounding",
             groups=groups,pooled_tail_means=means,paired_comparisons=paired,
             percent_248_above_20k=(means[248]/means[20000]-1)*100 if 248 in means and 20000 in means else None,
             percent_5k_below_20k=(1-means[5000]/means[20000])*100 if 5000 in means and 20000 in means else None,
             percent_248_above_1k=(means[248]/means[1000]-1)*100 if 248 in means and 1000 in means else None)
(out/"analysis.json").write_text(json.dumps(audit,indent=2),encoding="utf-8")
with (out/"trials.csv").open("w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=list(metrics[0])); w.writeheader();w.writerows(metrics)
with (out/"samples.csv").open("w",newline="",encoding="utf-8") as f:
    w=csv.writer(f);w.writerow(["sequence","trial","t_us","ticks","valid_edges","invalid"])
    for (seq,trial),pts in samples.items():
        for p in pts: w.writerow([seq,trial,p["t_us"],p["ticks"],p["valid_edges"],p["invalid"]])
(out/"source-log.txt").write_bytes(raw)
print(json.dumps(audit,indent=2))
