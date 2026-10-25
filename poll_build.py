import json, subprocess, sys, time

TOKEN = "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"
RUN_ID = "40198210238"
REPO = "agenttaozi/gki-droidspaces-build"
HEADERS = ["-H", f"Authorization: Bearer {TOKEN}", "-H", "Accept: application/vnd.github+json"]

def api(path):
    out = subprocess.run(["curl","-s","--max-time","40"] + HEADERS +
                          [f"https://api.github.com/repos/{REPO}{path}"],
                         capture_output=True, text=True).stdout
    try:
        return json.loads(out)
    except Exception:
        return {}

for i in range(560):
    d = api(f"/actions/runs/{RUN_ID}")
    st, concl = d.get("status"), d.get("conclusion")
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] poll#{i} status={st} conclusion={concl}", flush=True)
    if st in ("completed", "failure", "cancelled", "timed_out"):
        print("TERMINAL", st, concl, flush=True)
        jobs = api(f"/actions/runs/{RUN_ID}/jobs")
        for j in jobs.get("jobs", []):
            print("JOB", j.get("name"), j.get("status"), j.get("conclusion"), flush=True)
        arts = api(f"/actions/runs/{RUN_ID}/artifacts")
        for a in arts.get("artifacts", []):
            print("ARTIFACT", a.get("name"), "size=", a.get("size_in_bytes"), "id=", a.get("id"), flush=True)
        sys.exit(0)
    time.sleep(30)

print("POLL_TIMEOUT", flush=True)
