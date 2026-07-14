# Config Page Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the `capsuleradar.local` config page as a 6-group accordion streamed section-by-section (no fixed buffer), with live "Saved ✓" feedback, and make Theme + Time zone live settings.

**Architecture:** Replace the single `snprintf(buf, 24576, …)` in `handleRoot()` with ESP32 `WebServer` **chunked streaming** (`setContentLength(CONTENT_LENGTH_UNKNOWN)` → `send(200,…,"")` → one `sendContent(String)` per accordion card → terminating `sendContent("")`). Existing per-setting `fetch()` endpoints are reused verbatim; Theme and Time zone move out of the Save-&-restart form onto new live `GET /theme` and `GET /tz` endpoints. All existing control markup and the option-list builders (`ropts`, `topts`, `iopts`, `uopts`, `rotopts`, `tlopts`, `mxopts`, `maopts`, `aopts`, `popts`, `tzopts`, `gpsRow`) are carried over unchanged — only regrouped into cards.

**Tech Stack:** C++17, Arduino `WebServer` (ESP32), PlatformIO `esp32-s3-amoled-175`. No SDL-sim coverage (web server is device-only) → verify by firmware compile + `curl` + on-device.

**Reference — current page:** `src/main.cpp` `handleRoot()` (~lines 307-536). Read it fully before starting; it is the source of all control markup and the option builders you will reuse.

---

### Task CP-1: New live endpoints — `/theme` and `/tz`

Do this first so the streamed page can wire to them.

**Files:** `src/main.cpp`

- [ ] **Step 1: Add `handleTheme` and `handleTz`**

Place next to the other small handlers (e.g. just after `saveTheme` / near `handleUnits`). `radar::setTheme()` already fires the persist callback (`saveTheme`), so theme persistence is automatic:

```cpp
static void handleTheme() {                       // live theme change from the web (no restart)
    if (g_web.hasArg("v")) {
        int t = g_web.arg("v").toInt();
        if (t < 0) t = 0; if (t >= THEME_COUNT) t = THEME_COUNT - 1;
        radar::setTheme(t);                        // fires saveTheme cb -> persists + re-tints
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleTz() {                           // live time-zone change (no restart)
    if (g_web.hasArg("v")) {
        const int i = g_web.arg("v").toInt();
        if (i >= 0 && i < TZOPTS_N) {
            g_tz = TZOPTS[i].tz;                    // g_tz is the active POSIX TZ string
            setenv("TZ", g_tz.c_str(), 1); tzset(); // apply live; clock picks it up next tick
            Preferences p; p.begin("capsuleradar", false);
            p.putString("tz", TZOPTS[i].tz); p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}
```

Note: confirm `g_tz`'s type by grepping its declaration (`grep -n "g_tz" src/main.cpp`). If it is a `String`, `g_tz = TZOPTS[i].tz;` and `g_tz.c_str()` are correct as written. If it is a `const char*`, assign `g_tz = TZOPTS[i].tz;` and use `setenv("TZ", g_tz, 1);` instead.

- [ ] **Step 2: Register the routes**

In `setup()` where the other routes are registered (near `g_web.on("/units", handleUnits);`), add:

```cpp
    g_web.on("/theme", handleTheme);
    g_web.on("/tz", handleTz);
```

- [ ] **Step 3: Compile**

Run: `pio run -e esp32-s3-amoled-175`
Expected: build SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(web): live /theme and /tz endpoints (no restart to change)"
```

---

### Task CP-2: Convert `handleRoot()` to chunked streaming (structure only)

Rewrite the *emission mechanism* first, keeping the page content essentially as-is but split into a head chunk, a single body chunk, and a script chunk. This proves streaming works before regrouping.

**Files:** `src/main.cpp` `handleRoot()`

- [ ] **Step 1: Keep the option-builders, replace the `snprintf`/`ps_malloc` block**

Leave everything from the top of `handleRoot()` through the construction of `gpsRow` UNCHANGED (all the `ropts`/`topts`/… `String` builders). DELETE the `static const size_t BUFSZ = 24576;`, the `static char *buf = (char*)ps_malloc(BUFSZ);`, the `snprintf(buf, BUFSZ, …)` call, its argument list, and the truncation-guard `if (_pglen …)`. Replace the whole emission with chunked streaming:

```cpp
    g_web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_web.send(200, "text/html", "");

    // ---- head + CSS ----
    g_web.sendContent(F(
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Capsule Radar</title>"
        "<link rel=stylesheet href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'>"
        "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>"
        "<style>"
        /* --- carry over EVERY existing style rule from the current page verbatim --- */
        /* then append the accordion + toast rules below --- */
        ".card{background:rgba(10,20,14,.85);border:1px solid #1f3a2b;border-radius:14px;margin-bottom:10px;overflow:hidden}"
        ".chead{display:flex;align-items:center;gap:9px;padding:13px 15px;cursor:pointer;user-select:none}"
        ".chead .ic{width:20px;text-align:center}"
        ".chead .ti{color:#1dff86;font-size:11px;letter-spacing:1.3px;text-transform:uppercase;flex:1}"
        ".chead .cv{color:#5f7a6c;font-size:11px}.chead .ar{color:#5f7a6c;transition:transform .2s}"
        ".card.open .chead .ar{transform:rotate(90deg)}"
        ".cbody{padding:0 15px 15px;display:none}.card.open .cbody{display:block}"
        "#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%) translateY(20px);opacity:0;"
        "transition:.25s;background:#0c1a12;border:1px solid #1dff86;color:#1dff86;padding:8px 16px;"
        "border-radius:20px;font-size:13px;pointer-events:none;z-index:1000}"
        "#toast.show{opacity:1;transform:translateX(-50%) translateY(0)}"
        "</style></head><body>"
        "<div class=hd><div class=dot></div><div><h1>Capsule Radar</h1>"
        "<p class=sub>Live ADS-B radar &middot; configuration</p></div></div>"));

    // ---- body: for now, ONE chunk holding the existing cards (regrouped in CP-3) ----
    // (temporary: emit the existing Location/Display/Sound/Network cards here as a String
    //  so the page is complete; CP-3 replaces this with 6 accordion cards.)

    // ---- script + tail ----
    g_web.sendContent(String(F("<div id=toast>Saved &check;</div><script>")) + /* JS below */ "</script></body></html>");
    g_web.sendContent("");   // end chunked response
```

The head chunk MUST include every CSS rule the current page defines (copy the whole existing `<style>` body), PLUS the accordion/toast rules shown. Do not drop any existing rule (`.hd`, `.dot`, `h1`, `.t`, `label`, `input`, `select`, `button`, `.w`, `.card`, `.ft`, `.ck`, `.sec`, `#map`, keyframes) — the `.card` rule is overridden by the accordion version above, which is fine (later rule wins).

- [ ] **Step 2: Build the body + script as regrouped cards (do the real work here)**

Emit the six cards, each as its own `sendContent`, using the accordion wrapper. Card template (fill `IC`, `TITLE`, `HINT`, and INNER = the carried-over controls for that group):

```cpp
    g_web.sendContent(String(F("<div class='card open'><div class=chead onclick='tg(this)'>"
        "<span class=ic>IC</span><span class=ti>TITLE</span><span class=cv>HINT</span>"
        "<span class=ar>&#9656;</span></div><div class=cbody>")) + INNER + F("</div></div>"));
```

Group mapping (INNER = the existing markup + option strings, carried from the current `handleRoot`, unchanged except moved):

| Card | IC / TITLE | INNER controls (reuse existing markup + option vars) |
|------|-----------|------|
| 1 | ⌖ Location &amp; Range | the `<form method=POST action=/save>`: map `<div id=map>`, lat `input`, lon `input`, `gpsRow`, range `<select name=range>`+`ropts`, `<button>Save &amp; restart</button></form>` — **remove the theme and tz selects from this form** |
| 2 | ◐ Appearance | theme `<select onchange='th(this.value)'>`+`topts` (NEW live handler), brightness range, dim `<select onchange='d(...)'>`+`iopts`, sweep checkbox, units `<select onchange='u(...)'>`+`uopts`, rotation `<select onchange='ro(...)'>`+`rotopts` |
| 3 | ✈ Traffic &amp; Filters | airports checkbox, hide-ground checkbox, military-only checkbox, min-alt `<select>`+`maopts`, trails `<select>`+`tlopts`, max-aircraft `<select>`+`mxopts` |
| 4 | ⤾ Motion | auto-rotate (`mr`), shake (`ms`), wake (`mw`) checkboxes |
| 5 | ♪ Sound &amp; Alerts | volume range, mute checkbox, alert `<select>`+`aopts`, proximity `<select>`+`popts`, `<button class=sec onclick='tst()'>Test ping</button>` |
| 6 | ⏱ Time &amp; System | tz `<select onchange='tz(this.value)'>`+`tzopts` (NEW live handler), `<form action=/wifi>`Reset WiFi, firmware-update link, `v" FW_VERSION "` |

Only the **first** card (`class='card open'`) starts open; the other five use `class=card` (closed).

- [ ] **Step 3: Script chunk — accordion + toast + all fetch handlers**

The `<script>` must contain: the existing Leaflet map init + `S()`/dragend/click/`invalidateSize`, the existing TZ-autopick IIFE, ALL existing fetch helper functions (`b,v,m,t→tst,d,sw,ap,hg,ma,mo,tl,mx,ro,mr,ms,mw,u,al,px,gp`), PLUS new `th`, `tz`, the accordion `tg`, and the `toast` helper. Every live handler calls `toast()` after firing its fetch. Emit it interpolating the two map-center coords + the TZSET flag as the current code does (via `String() + …` or a small `snprintf` into a local ~1.5 KB buffer for the map line — a LOCAL buffer is fine; the danger was the whole-page buffer):

```js
function toast(){var t=document.getElementById('toast');t.classList.add('show');
  clearTimeout(window._tt);window._tt=setTimeout(function(){t.classList.remove('show')},1200);}
function tg(h){h.parentElement.classList.toggle('open');}
function th(v){fetch('/theme?v='+v+'&save=1').then(toast);}
function tz(v){fetch('/tz?v='+v+'&save=1').then(toast);}
// existing handlers gain a .then(toast) or a trailing toast(); e.g.:
function b(v,s){fetch('/bright?v='+v+(s?'&save=1':'')).then(function(){if(s)toast();});}
function sw(c){fetch('/sweep?v='+(c?1:0)+'&save=1').then(toast);}
// …apply the same .then(toast) to d,ap,hg,ma,mo,tl,mx,ro,mr,ms,mw,u,al,px,gp,v(on change),m
function tst(){fetch('/vol?test=1');}   // test ping: no toast
```

Rename the old `t()` test-ping function to `tst()` (and its button `onclick`) to avoid colliding with nothing — `t` is free, but `tst` is clearer; ensure the Sound card button calls `tst()`.

- [ ] **Step 4: Compile**

Run: `pio run -e esp32-s3-amoled-175`
Expected: build SUCCESS. (Watch for `F()`/`String` concatenation type errors — wrap flash-string literals used in `+` with `String(F("…"))` on the left-most operand.)

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(web): chunked-streamed 6-group accordion config page + live theme/tz + save toast"
```

---

### Task CP-3: Wire the moved theme/tz + drop them from the restart form

Verify (from CP-2) that the `/save` form no longer contains `<select name=theme>` or `<select name=tz>` (they moved to live selects in Appearance / Time & System). `handleSave` may keep its now-dormant `theme`/`tz` arg handling — harmless (those args are no longer submitted). No edit to `handleSave` required.

- [ ] **Step 1: Grep-confirm the form is clean**

Run: `grep -n "name=theme\|name=tz" src/main.cpp`
Expected: **no matches** (both are now `onchange` live selects, not named form fields).

- [ ] **Step 2: Compile (no-op safety)**

Run: `pio run -e esp32-s3-amoled-175`
Expected: SUCCESS.

*(No commit if nothing changed; this task is a verification gate.)*

---

### Task CP-4: Version bump, flash, verify, ship

**Files:** `src/config.h`

- [ ] **Step 1: Bump version**

In `src/config.h`, change `#define FW_VERSION "1.8.3"` to `#define FW_VERSION "1.9.0"`.

- [ ] **Step 2: Firmware compile**

Run: `pio run -e esp32-s3-amoled-175`
Expected: SUCCESS.

- [ ] **Step 3: Flash the S3**

Run: `pio run -e esp32-s3-amoled-175 -t upload --upload-port /dev/cu.usbmodem111201`
Expected: upload SUCCESS. (Auto-detect grabs the C3 and errors "This chip is ESP32-C3" — always pass the explicit S3 port.)

- [ ] **Step 4: Page-completeness check via curl**

Resolve mDNS to an IP first (it is flaky under rapid lookups): `ping -c1 capsuleradar.local`, then:
Run: `curl -s http://<device-ip>/ | tail -c 200`
Expected: output ends with `</script></body></html>` (proves the chunked stream produced a COMPLETE page — the old truncation symptom was a cut mid-`<script>`).
Also: `curl -s http://<device-ip>/ | grep -c "class=card"` → expected `6`.

- [ ] **Step 5: Commit, push, redeploy the web flasher**

```bash
git add src/config.h
git commit -m "release: v1.9.0 config page redesign (accordion + chunked backend)"
git push
gh workflow run webflasher.yml
```

Expected: workflow dispatched; once green the flasher serves v1.9.0.

---

## Self-Review

- **Spec coverage:** 6-group accordion (CP-2) ✓; chunked streaming / no fixed buffer (CP-2 Step 1) ✓; save toast (CP-2 Step 3) ✓; theme+tz live + moved out of restart form (CP-1, CP-2, CP-3) ✓; only lat/lon/range restart (CP-2 card-1) ✓; version bump + flash + push + flasher (CP-4) ✓; per-theme seam = untouched (no per-theme storage added) ✓; curl completeness test for the truncation fix (CP-4 Step 4) ✓.
- **Placeholder scan:** the one deliberate "carry over existing markup" instruction is a reuse directive with an exact source reference (current `handleRoot`) + a full group-mapping table + the exact new wrapper/CSS/JS — not a vague TODO. All *new* code (endpoints, streaming skeleton, accordion/toast CSS+JS, card wrapper) is shown in full.
- **Type consistency:** `handleTheme`/`handleTz` names match their `g_web.on` registrations; `tg`/`th`/`tz`/`toast`/`tst` JS names match their call sites; `sendContent`/`setContentLength` are the real ESP32 `WebServer` API; `TZOPTS`/`TZOPTS_N`/`g_tz` verified present.
- **Render/stream risk:** the streaming refactor is exactly the thing to review — Opus reviews CP-2 for a dropped/duplicated chunk, a missing terminating `sendContent("")`, `String` lifetime passed to `sendContent`, and any CSS rule dropped from the carried-over `<style>`.
