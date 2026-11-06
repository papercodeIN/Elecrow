# Tasker Companion Setup (No-Code MVP)

This is **Option A** from the README: a no-code phone side using Tasker, so you
can prove the whole chain (Google Maps → BLE → display) before building the
proper companion app (`../android/`).

## What you need (three installs)

| App | Role | Where |
|---|---|---|
| **Tasker** | Automation engine (paid, one-time) | Play Store |
| **AutoNotification** | Intercepts the Google Maps notification | Play Store |
| **Basic BLE Writer** | Tasker plugin that writes to BLE characteristics | [swankdave/Basic-Ble-Writer](https://github.com/swankdave/Basic-Ble-Writer) (open source; side-load the APK or get it via Tasker's plugin list) |

Pair the display first: **Settings → Bluetooth → pair "AA Nav Display"**, then
note its MAC address (tap the gear next to the device).

## Profile 1 — Relay navigation updates

1. **Tasker → Profiles → + → Event → Plugin → AutoNotification → Intercept**
   - Apps: **Google Maps**
   - Notification events: **Created or Updated**
2. Create a task named **NavRelay** with these actions:

   1. **Task → Wait 500 ms** — lets Maps finish composing long notifications.
   2. **Plugin → AutoNotification → AutoNotification Query** (optional but
      recommended) with Apps: Google Maps — this fills `%antitle()`, `%antext()`
      and `%anbigtext()` arrays with the current Maps notifications.
   3. **Task → JavaScriptlet** (JavaScript, 30 s timeout), code:

      ```javascript
      // Combine the Maps notification fields into one blob to parse
      var t = antitle[0] || '';
      var b = anbigtext[0] || antext[0] || '';
      var s = (t + ' ' + b);

      // Distance ("300 m", "1.2 km", "400 ft", "0.3 mi")
      var dm = s.match(/(?:in\s+)?([0-9]+(?:\.[0-9]+)?)\s*(m|km|ft|mi)\b/i);
      if (!dm) { exit(); }   // not a navigation notification

      var val = parseFloat(dm[1].replace(',', '.'));
      var unit = dm[2].toLowerCase();
      var meters = Math.round(val * (unit === 'km' ? 1000 : unit === 'mi' ? 1609.34 : unit === 'ft' ? 0.3048 : 1));

      // Maneuver keywords -> protocol codes (see README protocol table)
      var L = s.toLowerCase();
      var man = 'ST';
      if (L.indexOf('u-turn') >= 0) man = 'UT';
      else if (L.indexOf('roundabout') >= 0) man = 'RAB';
      else if (L.indexOf('sharp left') >= 0) man = 'SHL';
      else if (L.indexOf('sharp right') >= 0) man = 'SHR';
      else if (L.indexOf('slight left') >= 0) man = 'SL';
      else if (L.indexOf('slight right') >= 0) man = 'SR';
      else if (L.indexOf('turn left') >= 0 || L.indexOf('left turn') >= 0) man = 'TL';
      else if (L.indexOf('turn right') >= 0 || L.indexOf('right turn') >= 0) man = 'TR';
      else if (L.indexOf('merge left') >= 0 || L.indexOf('keep left') >= 0) man = 'MEL';
      else if (L.indexOf('merge right') >= 0 || L.indexOf('keep right') >= 0) man = 'MER';
      else if (L.indexOf('arrive') >= 0 || L.indexOf('destination') >= 0) man = 'ARR';
      else if (L.indexOf('left') >= 0) man = 'TL';
      else if (L.indexOf('right') >= 0) man = 'TR';

      // Street: text after "onto/to/exit/at", cut at "then" or punctuation
      var sm = s.match(/(?:onto|to|toward|towards|exit|at)\s+(.+?)(?=\s+then\b|\s+in\s+[0-9]|,|;|\.|$)/i);
      var street = sm ? sm[1].slice(0, 50) : '';

      var thm = s.match(/then(?:\s+in\s+[0-9.,]+\s*(?:m|km|ft|mi))?[\s:]*([^\n]{2,40})/i);
      var then = thm ? thm[1].trim() : '';

      var distText = (Math.round(val * 10) / 10) + ' ' + unit;
      var payload = JSON.stringify({
        t: 'nav', m: man, d: meters, ds: distText,
        st: street, th: then, eta: '', rt: '', v: 0, s: 'nav'
      });
      ```

   4. **Plugin → Basic BLE Write**
      - Device: your **AA Nav Display** (by MAC)
      - Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
      - Characteristic: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
      - Value: `%payload`
      - Tick "Add newline" if the plugin offers it (the display splits messages
        on `\n`).

3. Back out of Tasker so the profile is enabled.

## Profile 2 — Heartbeat (keeps the display out of "signal lost")

1. **Profiles → + → Time → every 5 minutes** (Tasker's minimum reliable period;
   the display tolerates 10 s of silence for the banner but only 2 min of
   total silence before it matters — with Profile 1 active, nav updates
   themselves keep the link fresh; this ping only covers idle periods).
2. Task: **JavaScriptlet** building `{"t":"ping"}` → **Basic BLE Write** as
   above.

## Honest limitations of the Tasker route

- **Latency**: AutoNotification intercept fires ~0.5–1.5 s after Maps updates
  the notification. Near turns Maps updates very frequently, so prompts still
  land in time — but this is why the dedicated app (`../android/`) is the
  long-term answer.
- **Battery**: Tasker + two plugins poll notifications continuously.
- **Fragility**: a Maps UI language change can alter notification wording;
  update the regexes above (the display always falls back to showing the raw
  `ds` text, so worst case the arrow/distance still work).
- **Reconnects**: Basic BLE Writer reconnects per write on some devices. If
  updates feel intermittent, open the display's GATT connection first with any
  BLE tool (e.g. nRF Connect) and keep it open.
- Tasker's time profile cannot go below ~5 minutes for the ping profile; that
  is fine because navigation updates serve as the real heartbeat while driving.

## Testing without driving

Before a road test, verify the chain on the desk: flash the firmware, run the
display, pair, then in Tasker long-press the **NavRelay** task → **Run**. The
display should show the last leg you navigated (Tasker replays the stored
variables). For a fully scripted feed, use `../tools/test_ble_sender.py` from
a PC instead.
