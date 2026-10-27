#ifndef WEBUI_H
#define WEBUI_H

#include <Arduino.h>

// Simple, self-contained tech UI. No external fonts/CDNs: renders identically
// in AP mode (no internet) and station mode.

const char WEBUI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CrowPanel Text Display</title>
<style>
  :root { --bg:#0d1117; --panel:#161b22; --border:#2b3440; --cyan:#00e0ff; --dim:#8b98a5; --text:#e6edf3; }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: var(--bg); color: var(--text); font-family: ui-monospace, Consolas, "Courier New", monospace; padding: 16px; max-width: 720px; margin: 0 auto; }
  h1 { font-size: 18px; letter-spacing: 2px; color: var(--cyan); border-bottom: 1px solid var(--border); padding-bottom: 10px; }
  .status { font-size: 12px; color: var(--dim); padding: 8px 0; }
  .status b { color: var(--cyan); font-weight: normal; }
  section { background: var(--panel); border: 1px solid var(--border); border-radius: 4px; padding: 14px; margin: 14px 0; }
  h2 { font-size: 12px; letter-spacing: 2px; color: var(--cyan); margin-bottom: 10px; }
  .row { display: grid; grid-template-columns: 1fr 90px 90px; gap: 10px; padding: 8px 0; border-bottom: 1px dashed var(--border); }
  .row:last-child { border-bottom: none; }
  .row .ch { font-size: 11px; color: var(--dim); grid-column: 1 / -1; }
  label { display: block; font-size: 10px; color: var(--dim); margin-bottom: 3px; letter-spacing: 1px; }
  input[type="text"], input[type="password"], select {
    width: 100%; padding: 8px; background: var(--bg); border: 1px solid var(--border);
    border-radius: 3px; color: var(--text); font: inherit; font-size: 13px;
  }
  input:focus, select:focus { border-color: var(--cyan); outline: none; }
  input[type="color"] { width: 100%; height: 34px; background: var(--bg); border: 1px solid var(--border); border-radius: 3px; cursor: pointer; padding: 2px; }
  button { width: 100%; padding: 12px; margin-top: 12px; background: transparent; border: 1px solid var(--cyan); color: var(--cyan); border-radius: 3px; font: inherit; font-size: 13px; letter-spacing: 2px; cursor: pointer; }
  button:active { background: var(--cyan); color: var(--bg); }
  button.warn { border-color: #f85149; color: #f85149; }
  .bgrow { display: flex; align-items: center; gap: 12px; }
  .bgrow input { flex: 1; }
  #msg { font-size: 12px; color: #3fb950; text-align: center; min-height: 16px; margin-top: 8px; }
  .half { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  @media (max-width: 520px) { .row { grid-template-columns: 1fr 80px 80px; } .half { grid-template-columns: 1fr; } }
</style>
</head>
<body>
  <h1>CROWPANEL TEXT DISPLAY</h1>
  <div class="status">STATUS: <b>%WIFI_STATUS%</b> &nbsp;|&nbsp; MODE: <b>%MODE%</b> &nbsp;|&nbsp; THIS PAGE: <b>%AP_IP%</b></div>

  <form id="textForm">
    <section>
      <h2>BACKGROUND</h2>
      <div class="bgrow">
        <input type="color" id="bgcolor" name="bgcolor" value="%BGCOLOR%">
      </div>
    </section>
    <section>
      <h2>TEXT LINES</h2>
      %LINES%
      <button type="submit">UPDATE DISPLAY</button>
      <div id="msg"></div>
    </section>
  </form>

  <section>
    <h2>WIFI SETUP</h2>
    <form id="wifiForm">
      <div class="half">
        <div><label>SSID</label><input type="text" id="wssid" name="ssid" placeholder="network name" required></div>
        <div><label>PASSWORD</label><input type="password" id="wpass" name="pass" placeholder="password"></div>
      </div>
      <button type="submit">SAVE WIFI &amp; RESTART</button>
    </form>
  </section>
  %RESET_SECTION%
  <script>
    document.getElementById('textForm').addEventListener('submit', function(e) {
      e.preventDefault();
      const fd = new FormData();
      fd.append('bgcolor', document.getElementById('bgcolor').value);
      for (let i = 0; i < 5; i++) {
        fd.append('text' + i, document.getElementById('text' + i).value);
        fd.append('color' + i, document.getElementById('color' + i).value);
        fd.append('size' + i, document.getElementById('size' + i).value);
      }
      fetch('/update', { method: 'POST', body: fd })
        .then(r => r.text())
        .then(d => { document.getElementById('msg').textContent = '>> DISPLAY UPDATED'; })
        .catch(err => { document.getElementById('msg').textContent = '>> ERROR: ' + err; });
    });
    document.getElementById('wifiForm').addEventListener('submit', function(e) {
      e.preventDefault();
      const fd = new FormData();
      fd.append('ssid', document.getElementById('wssid').value);
      fd.append('pass', document.getElementById('wpass').value);
      fetch('/api/wifi', { method: 'POST', body: fd })
        .then(r => r.text())
        .then(d => { document.body.innerHTML = d; })
        .catch(err => { alert('>> ERROR: ' + err); });
    });
    const resetForm = document.getElementById('resetForm');
    if (resetForm) {
      resetForm.addEventListener('submit', function(e) {
        e.preventDefault();
        if (!confirm('Purge saved WiFi credentials?\nThe device will restart in access point mode.')) return;
        fetch('/api/wifi/reset', { method: 'POST' })
          .then(r => r.text())
          .then(d => { document.body.innerHTML = d; })
          .catch(err => { alert('>> ERROR: ' + err); });
      });
    }
  </script>
</body>
</html>
)rawliteral";

// Injected by handleRoot() in station mode only; empty in AP mode
const char WEBUI_RESET_SECTION[] PROGMEM = R"rawliteral(
  <section>
    <h2>CREDENTIAL PURGE</h2>
    <form id="resetForm">
      <button type="submit" class="warn">FORGET WIFI &amp; RESTART IN AP MODE</button>
    </form>
  </section>
)rawliteral";

#endif
