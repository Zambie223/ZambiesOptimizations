"""Compare TickProfiler captures of stress runs (stdlib only).

Usage: compare_runs.py <run>[@<segment>] [<run>[@<segment>] ...] [--top N] [--objects]
A run folder holds session/ (a TickProfiler session copy). The capture window is taken from the marks
"... capture start" / "... capture end"; without them the whole session after the first 120 s is used.
<run>@<segment> uses the marks "seg <segment> start" / "seg <segment> end" instead, so one server run can hold
several A/B segments.
"""
import csv, glob, os, sys
from collections import defaultdict


def rows(folder, base):
    out = []
    for path in sorted(glob.glob(os.path.join(folder, base + '.*.csv'))):
        with open(path, newline='', encoding='utf-8', errors='replace') as f:
            out.extend(csv.DictReader(f))
    return out


def fnum(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return 0.0


def window(session, segment=None):
    start = end = None
    for m in rows(session, 'marks'):
        t = m.get('text', '').strip()
        if segment is None:
            if t.endswith('capture start'):
                start = fnum(m['uptime_s'])
            elif t.endswith('capture end'):
                end = fnum(m['uptime_s'])
        elif t == 'seg %s start' % segment:
            start = fnum(m['uptime_s'])
        elif t == 'seg %s end' % segment:
            end = fnum(m['uptime_s'])
    if segment is not None and (start is None or end is None):
        sys.exit('segment %r: marks not found' % segment)
    return start, end


def in_window(r, start, end):
    u = fnum(r.get('uptime_s'))
    return (start is None or u >= start) and (end is None or u <= end)


def summarize(spec, top, show_objects):
    run, _, segment = spec.partition('@')
    session = os.path.join(run, 'session')
    start, end = window(session, segment or None)
    frames = rows(session, 'frames')
    if start is None and frames:
        start = fnum(frames[0]['uptime_s']) + 120
    sel = [r for r in frames if in_window(r, start, end) and fnum(r.get('frames')) > 0]
    res = {'run': os.path.basename(os.path.normpath(run)) + ('@' + segment if segment else ''), 'seconds': len(sel)}
    if not sel:
        return res, [], []
    n = sum(fnum(r['frames']) for r in sel)
    res['tick_hz'] = n / len(sel)
    # per-frame averages weighted by frame count; *_ms columns in frames.csv are ms per frame (avg) unless noted
    for col in sel[0].keys():
        if col.endswith('_avg_ms') or col in ('idle_avg_ms', 'untimed_avg_ms'):
            res[col] = sum(fnum(r[col]) * fnum(r['frames']) for r in sel) / n
    for col in ('work_p50_ms', 'work_p95_ms'):
        res[col] = sum(fnum(r[col]) for r in sel) / len(sel)
    res['work_max_ms'] = max(fnum(r['work_max_ms']) for r in sel)
    # frames.csv: actor/comp/repl times are per frame; gc_ms and the counts are totals for the second
    for col in ('actor_gt_ms', 'actor_worker_ms', 'comp_gt_ms', 'comp_worker_ms', 'repl_ms'):
        res[col + '_per_frame'] = sum(fnum(r.get(col)) * fnum(r['frames']) for r in sel) / n
    for col in ('gc_ms', 'actor_ticks', 'comp_ticks', 'repl_actors', 'hitches', 'players'):
        res[col + '_per_s'] = sum(fnum(r.get(col)) for r in sel) / len(sel)

    agg = defaultdict(lambda: [0.0, 0.0, 0.0, 0.0, 0, ''])
    cls = [r for r in rows(session, 'classes') if in_window(r, start, end)]
    windows = len({r['uptime_s'] for r in cls}) or 1
    for r in cls:
        a = agg[(r['kind'], r['class'])]
        a[0] += fnum(r['total_ms_per_s']); a[1] += fnum(r['gt_ms_per_s']); a[2] += fnum(r['worker_ms_per_s']); a[3] += fnum(r['calls_per_s']); a[4] += 1; a[5] = r['origin']
    classes = sorted(((k, [v[0] / windows, v[1] / windows, v[2] / windows, v[3] / windows, v[5]]) for k, v in agg.items()), key=lambda x: -x[1][0])[:top]

    objects = []
    if show_objects:
        oagg = defaultdict(lambda: [0.0, 0.0, 0, ''])
        # level 3 usually runs after the capture window; only a segment restricts the objects to its own window
        obj = [r for r in rows(session, 'objects') if not segment or in_window(r, start, end)]
        owin = len({r['uptime_s'] for r in obj}) or 1
        for r in obj:
            a = oagg[(r['kind'], r['object'])]
            a[0] += fnum(r['total_ms_per_s']); a[1] += fnum(r['calls_per_s']); a[2] += 1; a[3] = r['class']
        objects = sorted(((k, [v[0] / owin, v[1] / owin, v[3]]) for k, v in oagg.items()), key=lambda x: -x[1][0])[:top]
    return res, classes, objects


def main():
    args = sys.argv[1:]
    top = 25
    show_objects = '--objects' in args
    if '--top' in args:
        i = args.index('--top'); top = int(args[i + 1]); del args[i:i + 2]
    runs = [a for a in args if not a.startswith('--')]
    results = [summarize(r, top, show_objects) for r in runs]
    keys = []
    for res, _, _ in results:
        for k in res:
            if k not in keys:
                keys.append(k)
    print('%-28s' % 'metric' + ''.join('%16s' % res.get('run', '')[:15] for res, _, _ in results))
    for k in keys[1:]:
        vals = [res.get(k) for res, _, _ in results]
        if all(v in (None, 0, 0.0) for v in vals):
            continue
        print('%-28s' % k + ''.join('%16s' % ('-' if v is None else ('%.3f' % v if isinstance(v, float) else v)) for v in vals))
    for res, classes, objects in results:
        print('\n== %s: top classes (ms per second: total / game thread / workers, calls/s)' % res['run'])
        for (kind, name), (tot, gt, wk, calls, origin) in classes:
            print('  %8.2f %8.2f %8.2f %9.0f  %-15s %s [%s]' % (tot, gt, wk, calls, kind, name, origin))
        if objects:
            print('== %s: top objects at level 3 (ms per second, calls/s)' % res['run'])
            for (kind, name), (tot, calls, klass) in objects:
                print('  %8.2f %9.0f  %-15s %s (%s)' % (tot, calls, kind, name, klass))


if __name__ == '__main__':
    main()
