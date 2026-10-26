#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Digital Photo Frame</title>
<style>
:root{
  color-scheme:dark;
  --bg:#080d18;--surface:#111a2b;--surface2:#19263a;--border:#2c3b52;
  --accent:#38bdf8;--accent-hover:#7dd3fc;--accent-glow:rgba(56,189,248,0.12);
  --success:#34d399;--danger:#fb7185;--danger-hover:#f43f5e;
  --text:#e6f1ff;--text-muted:#9aabc1;--radius:10px;
  --mono:ui-monospace,SFMono-Regular,Consolas,"Liberation Mono",monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:radial-gradient(ellipse at 50% -20%,#172b46 0%,var(--bg) 56%);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;line-height:1.5;min-height:100vh}
header{background:rgba(12,20,34,.94);backdrop-filter:blur(12px);border-bottom:1px solid var(--border);padding:14px 24px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px;position:sticky;top:0;z-index:20;box-shadow:0 8px 30px rgba(0,0,0,.22)}
.brand{display:flex;align-items:center;gap:12px}
.brand-icon{background:linear-gradient(145deg,#153553,#102235);color:var(--accent);border:1px solid #245071;width:44px;height:44px;display:flex;align-items:center;justify-content:center;border-radius:10px;box-shadow:inset 0 0 18px rgba(56,189,248,.12)}
.brand h1{font-size:18px;font-weight:700;color:var(--text);letter-spacing:.2px}
.brand p{font:11px var(--mono);color:var(--accent);letter-spacing:.4px}
.hdr-actions{display:flex;align-items:center;gap:10px}
.btn{background:var(--surface2);color:var(--text);border:1px solid var(--border);padding:8px 16px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;display:inline-flex;align-items:center;gap:6px;transition:background .15s ease,border-color .15s ease,color .15s ease;min-height:38px}
.btn:hover{background:#20334c;border-color:var(--accent)}
.btn:focus-visible,select:focus-visible,input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.btn-primary{background:var(--accent);color:#07111d;border-color:var(--accent);box-shadow:0 0 18px rgba(56,189,248,.16)}
.btn-primary:hover{background:var(--accent-hover);border-color:var(--accent-hover);color:#07111d}
.btn-danger{background:rgba(251,113,133,.06);color:var(--danger);border-color:rgba(251,113,133,.55)}
.btn-danger:hover{background:var(--danger);color:#fff}
.btn-sm{padding:5px 10px;font-size:12px}

.container{max-width:1200px;margin:0 auto;padding:28px 16px;display:grid;grid-template-columns:1fr;gap:24px}
@media(min-width:960px){
  .container{grid-template-columns:340px 1fr}
}

.panel{background:linear-gradient(145deg,rgba(20,31,49,.97),rgba(13,22,37,.97));border:1px solid var(--border);border-radius:var(--radius);padding:18px;box-shadow:0 12px 28px rgba(0,0,0,.2),inset 0 1px rgba(255,255,255,.025)}
.panel-title{font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:1.1px;color:#b0c2d8;margin-bottom:14px;display:flex;align-items:center;justify-content:space-between}

.remote-box{display:flex;flex-direction:column;gap:14px}
.remote-ctrls{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.remote-ctrls button{padding:12px;font-size:14px;font-weight:bold;justify-content:center}
.now-playing{background:linear-gradient(110deg,rgba(56,189,248,.09),rgba(25,38,58,.75));padding:12px;border-radius:8px;border:1px solid rgba(56,189,248,.2);border-left:3px solid var(--accent);font-size:13px}
.now-playing b{font:600 11px var(--mono);letter-spacing:.6px;text-transform:uppercase;color:var(--accent);display:block;margin-bottom:4px}

.stat-item{margin-bottom:12px}
.stat-lbl{display:flex;justify-content:space-between;font-size:12px;color:var(--text-muted);margin-bottom:4px}
.prog-bar{height:7px;background:#0b1422;border:1px solid #26364d;border-radius:4px;overflow:hidden}
.prog-val{height:100%;background:linear-gradient(90deg,#0ea5e9,#67e8f9);border-radius:4px;transition:width .3s;box-shadow:0 0 10px rgba(56,189,248,.35)}

.form-group{margin-bottom:12px}
.form-group label{display:block;font-size:12px;color:var(--text-muted);margin-bottom:5px;font-weight:600}
select,input[type=text],input[type=password]{width:100%;padding:9px 12px;background:var(--bg);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:13px}
select:focus,input:focus{outline:none;border-color:var(--accent)}

.dropzone{border:2px dashed var(--border);border-radius:var(--radius);padding:32px 20px;text-align:center;cursor:pointer;transition:all .2s;background:rgba(30,41,59,0.5)}
.dropzone:hover,.dropzone.dragover{border-color:var(--accent);background:var(--accent-glow)}
.drop-icon{font-size:42px;margin-bottom:8px;display:block}
.drop-txt{font-weight:600;font-size:15px;color:var(--text)}
.drop-sub{font-size:12px;color:var(--text-muted);margin-top:4px}

.gallery-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px;flex-wrap:wrap;gap:10px}
.gallery-count{font-size:13px;color:var(--text-muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:16px}
.photo-card{background:linear-gradient(150deg,#17253a,#111b2c);border:1px solid var(--border);border-radius:10px;overflow:hidden;display:flex;flex-direction:column;position:relative;transition:border-color .15s,box-shadow .15s}
.photo-card:hover{border-color:#4384a7;box-shadow:0 8px 22px rgba(0,0,0,.32)}
.photo-thumb-wrap{width:100%;aspect-ratio:4/3;background:#000;display:flex;align-items:center;justify-content:center;overflow:hidden;position:relative}
.photo-thumb{width:100%;height:100%;object-fit:contain;cursor:pointer}
.photo-meta{padding:10px;font-size:12px;display:flex;flex-direction:column;gap:4px;flex:1}
.photo-name{font-weight:600;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.photo-size{color:var(--text-muted);font-size:11px}
.photo-actions{display:flex;gap:6px;margin-top:6px}
.photo-actions .btn{flex:1;justify-content:center;padding:5px 6px;font-size:11px}

#empty-state{text-align:center;padding:48px 20px;color:var(--text-muted);grid-column:1/-1}
#empty-state .icon{font-size:48px;margin-bottom:12px;display:block}

.modal-backdrop{position:fixed;inset:0;background:rgba(15,23,42,0.8);backdrop-filter:blur(4px);display:none;align-items:center;justify-content:center;z-index:100}
.modal-backdrop.show{display:flex}
.modal-card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:24px;width:min(440px,92vw);box-shadow:0 10px 25px rgba(0,0,0,0.5)}
.modal-title{font-size:16px;font-weight:700;margin-bottom:14px;color:var(--text)}

#toast{position:fixed;bottom:24px;right:24px;background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:12px 20px;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,0.4);font-size:13px;z-index:200;display:none;align-items:center;gap:10px}
#toast.err{border-left:4px solid var(--danger)}
#toast.ok{border-left:4px solid var(--success)}

.badge{background:var(--surface2);color:var(--text-muted);padding:2px 8px;border-radius:12px;font-size:11px;font-weight:600}
.badge-live{background:rgba(34,197,94,0.15);color:var(--success)}
</style>
</head>
<body>

<header>
  <div class="brand">
    <div class="brand-icon">&#128247;</div>
    <div>
      <h1>Elecrow 2.8" Photo Frame</h1>
      <p id="hdrSub">Connecting...</p>
    </div>
  </div>
  <div class="hdr-actions">
    <button class="btn btn-primary" onclick="triggerFileInput()">&#8679; Upload Photos</button>
    <button class="btn" onclick="openWifiModal()">&#128246; WiFi Setup</button>
  </div>
</header>

<div class="container">
  <!-- Left Column: Controls & System Info -->
  <aside style="display:flex;flex-direction:column;gap:18px">
    
    <!-- Remote Controls -->
    <div class="panel">
      <div class="panel-title">
        <span>Frame Remote</span>
        <span class="badge badge-live" id="playBadge">Playing</span>
      </div>
      <div class="remote-box">
        <div class="now-playing">
          <b id="npTitle">Now Showing</b>
          <div id="npName" style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">Loading...</div>
          <div id="npIdx" style="font-size:11px;color:var(--text-muted);margin-top:2px;">- / -</div>
        </div>
        <div class="remote-ctrls">
          <button class="btn" onclick="ctrlAction('prev')">&#9198; Prev</button>
          <button class="btn" id="playBtn" onclick="togglePlay()">&#9208; Pause</button>
          <button class="btn" onclick="ctrlAction('next')">&#9197; Next</button>
        </div>
        <button class="btn btn-sm" onclick="ctrlAction('refresh')" style="justify-content:center">&#128260; Re-render Current</button>
      </div>
    </div>

    <!-- Slideshow Settings -->
    <div class="panel">
      <div class="panel-title">Slideshow Settings</div>
      <div class="form-group">
        <label>Slide Duration</label>
        <select id="setDuration" onchange="saveSettings()">
          <option value="3">3 Seconds</option>
          <option value="5" selected>5 Seconds (Default)</option>
          <option value="10">10 Seconds</option>
          <option value="15">15 Seconds</option>
          <option value="30">30 Seconds</option>
          <option value="60">1 Minute</option>
          <option value="300">5 Minutes</option>
        </select>
      </div>
      <div class="form-group">
        <label>Play Order</label>
        <select id="setOrder" onchange="saveSettings()">
          <option value="0">Sequential (Alphabetical)</option>
          <option value="1">Random / Shuffle</option>
        </select>
      </div>
      <div class="form-group">
        <label>Image Fit Mode</label>
        <select id="setFit" onchange="saveSettings()">
          <option value="2" selected>Auto (Portrait: full height, landscape: fill)</option>
          <option value="0">Fit Whole Photo (may leave black space)</option>
          <option value="1">Crop &amp; Fill Always (Fill Entire Screen)</option>
        </select>
      </div>
      <div class="form-group">
        <label>Transition Effect</label>
        <select id="setTransition" onchange="saveSettings()">
          <option value="12" selected>&#127922; Auto / Random (Surprise each slide)</option>
          <option value="1">Wipe: Left to Right</option>
          <option value="2">Wipe: Right to Left</option>
          <option value="3">Wipe: Top to Bottom</option>
          <option value="4">Wipe: Bottom to Top</option>
          <option value="5">Vertical Blinds</option>
          <option value="6">Horizontal Blinds</option>
          <option value="7">Curtain: Open Outward</option>
          <option value="8">Curtain: Close Inward</option>
          <option value="9">Box Iris Shrink</option>
          <option value="10">Checkerboard Dissolve</option>
          <option value="11">Interlaced Fade</option>
          <option value="0">Instant (No transition)</option>
        </select>
      </div>
    </div>

    <!-- Storage & Device Info -->
    <div class="panel">
      <div class="panel-title">MicroSD Storage</div>
      <div class="stat-item">
        <div class="stat-lbl">
          <span>Card Usage</span>
          <span id="stoTxt">-- / --</span>
        </div>
        <div class="prog-bar"><div class="prog-val" id="stoBar" style="width:0%"></div></div>
      </div>
      <div style="font-size:12px;color:var(--text-muted);display:flex;flex-direction:column;gap:4px;margin-top:8px">
        <div>Device IP: <b id="devIp" style="color:var(--text)">--</b></div>
        <div>SSID: <span id="devSsid">--</span></div>
        <div>Uptime: <span id="devUptime">--</span></div>
      </div>
    </div>

  </aside>

  <!-- Right Column: Upload & Gallery -->
  <main style="display:flex;flex-direction:column;gap:20px">
    
    <!-- Upload Dropzone -->
    <div class="dropzone" id="dropArea" onclick="triggerFileInput()">
      <span class="drop-icon">&#128444;&#65039;</span>
      <div class="drop-txt">Drag &amp; drop photos here, or click to browse</div>
      <div class="drop-sub">Supports JPEG, PNG, BMP, &amp; WebP &mdash; automatically optimized for display</div>
      <input type="file" id="fileInput" multiple accept="image/*,.jpg,.jpeg,.png,.bmp,.webp,.heic,.heif" style="display:none" onchange="handleFiles(this.files)">
    </div>

    <!-- Optimization Toggle -->
    <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;padding:0 6px;margin-top:-10px">
      <label style="display:inline-flex;align-items:center;gap:8px;font-size:12px;color:var(--text-muted);cursor:pointer">
        <input type="checkbox" id="chkAutoConvert" checked style="width:auto;cursor:pointer">
        <span><b>Auto-convert to Baseline JPEG</b> (fixes WhatsApp &amp; high-res phone photos)</span>
      </label>
      <span style="font-size:11px;color:var(--accent)">Fast &bull; 100% Compatible</span>
    </div>

    <!-- Upload Progress Modal / Bar -->
    <div id="uploadProgressBox" class="panel" style="display:none">
      <div class="panel-title">
        <span id="upStatusTxt">Uploading 0/0...</span>
        <span id="upSpeed">0 KB/s</span>
      </div>
      <div class="prog-bar"><div class="prog-val" id="upBar" style="width:0%"></div></div>
    </div>

    <!-- Gallery Section -->
    <div class="panel">
      <div class="gallery-hdr">
        <div>
          <h2 style="font-size:16px;font-weight:700">Photo Library</h2>
          <div class="gallery-count" id="galleryCount">0 photos on card</div>
        </div>
        <div style="display:flex;gap:8px">
          <button class="btn btn-sm btn-danger" onclick="confirmDeleteAll()">&#128465; Delete All</button>
          <button class="btn btn-sm" onclick="fetchLibrary()">&#128260; Refresh</button>
        </div>
      </div>

      <div class="grid" id="photoGrid">
        <div id="empty-state">
          <span class="icon">&#128247;</span>
          <h3>No Photos Found</h3>
          <p style="font-size:13px;margin-top:4px">Upload your favorite pictures above to start your photo frame slideshow!</p>
        </div>
      </div>
    </div>

  </main>
</div>

<!-- WiFi Setup Modal -->
<div class="modal-backdrop" id="wifiModal">
  <div class="modal-card">
    <div class="modal-title">&#128246; WiFi Configuration</div>
    <div class="form-group">
      <label>WiFi SSID (Network Name)</label>
      <input type="text" id="wSSID" placeholder="Enter WiFi SSID" maxlength="32">
    </div>
    <div class="form-group">
      <label>Password</label>
      <input type="password" id="wPASS" placeholder="Enter WiFi Password" maxlength="64">
    </div>
    <div style="display:flex;justify-content:flex-end;gap:10px;margin-top:20px">
      <button class="btn" onclick="closeWifiModal()">Cancel</button>
      <button class="btn btn-primary" onclick="saveWifiConfig()">Save &amp; Connect</button>
    </div>
  </div>
</div>

<div id="toast">Message</div>

<script>
let currentStatus = {};
let photoList = [];
const THUMB_PLACEHOLDER = 'data:image/svg+xml,%3Csvg xmlns="http://www.w3.org/2000/svg" width="80" height="60"%3E%3Crect fill="%231e293b" width="80" height="60"/%3E%3Ctext fill="%2394a3b8" x="50%25" y="50%25" dominant-baseline="middle" text-anchor="middle" font-size="10"%3EIMG%3C/text%3E%3C/svg%3E';
let thumbQueue = [];
let thumbLoading = false;
let uploadBusy = false;

// Serve thumbnails one at a time. The ESP32 has limited SD file descriptors;
// parallel image requests can exhaust them, especially after page refresh.
function pumpThumbnailQueue() {
  if (thumbLoading || !thumbQueue.length) return;
  const item = thumbQueue.shift();
  const img = item.img;
  if (!img.isConnected) return pumpThumbnailQueue();
  thumbLoading = true;
  let finished = false;
  const finish = () => {
    if (finished) return;
    finished = true;
    clearTimeout(item.timeout);
    thumbLoading = false;
    pumpThumbnailQueue();
  };
  item.timeout = setTimeout(finish, 12000);
  img.onload = () => finish();
  img.onerror = () => {
    if (item.fallbackUrl && img.src !== new URL(item.fallbackUrl, location.href).href) {
      img.src = item.fallbackUrl;
      return;
    }
    finish();
  };
  img.src = item.url;
}


function $(id){return document.getElementById(id)}

function toast(msg, isErr=false){
  const t = $('toast');
  t.textContent = msg;
  t.className = isErr ? 'err' : 'ok';
  t.style.display = 'flex';
  setTimeout(()=>{ t.style.display = 'none'; }, 3200);
}

function triggerFileInput(){ $('fileInput').click(); }

// Drag & Drop
const dropArea = $('dropArea');
['dragenter','dragover'].forEach(ev => {
  dropArea.addEventListener(ev, e => { e.preventDefault(); dropArea.classList.add('dragover'); }, false);
});
['dragleave','drop'].forEach(ev => {
  dropArea.addEventListener(ev, e => { e.preventDefault(); dropArea.classList.remove('dragover'); }, false);
});
dropArea.addEventListener('drop', e => {
  const dt = e.dataTransfer;
  if(dt && dt.files && dt.files.length) handleFiles(dt.files);
});

// Image Normalization using HTML5 Canvas (converts Progressive JPEGs, WebP, etc. to Baseline JPEG)
// Scales images for display and reduces unused black space.
const DISPLAY_W = 320;
const DISPLAY_H = 240;

function calcDisplaySize(srcW, srcH, mode) {
  // mode: 'letterbox' = fit within display (no crop), 'fill' = cover display (may crop)
  const scaleX = DISPLAY_W / srcW;
  const scaleY = DISPLAY_H / srcH;
  let scale;
  if (mode === 'fill') {
    scale = Math.max(scaleX, scaleY); // scale up so both sides fill
  } else {
    scale = Math.min(scaleX, scaleY); // scale down so both sides fit
  }
  // Never upscale beyond actual source resolution
  scale = Math.min(scale, 1.0);
  return {
    w: Math.round(srcW * scale),
    h: Math.round(srcH * scale)
  };
}

function createGalleryThumbnail(file) {
  return new Promise((resolve) => {
    const url = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => {
      URL.revokeObjectURL(url);
      const maxW = 160, maxH = 120;
      const scale = Math.min(maxW / img.naturalWidth, maxH / img.naturalHeight, 1);
      const w = Math.max(1, Math.round(img.naturalWidth * scale));
      const h = Math.max(1, Math.round(img.naturalHeight * scale));
      const canvas = document.createElement('canvas');
      canvas.width = w;
      canvas.height = h;
      const ctx = canvas.getContext('2d');
      if (!ctx) return resolve(null);
      ctx.drawImage(img, 0, 0, w, h);
      canvas.toBlob(blob => {
        if (!blob) return resolve(null);
        const baseName = file.name.replace(/\.[^/.]+$/, '').replace(/[^a-zA-Z0-9_-]/g, '_');
        resolve(new File([blob], baseName + '.thumb.jpg', {type:'image/jpeg'}));
      }, 'image/jpeg', 0.72);
    };
    img.onerror = () => { URL.revokeObjectURL(url); resolve(null); };
    img.src = url;
  });
}

function normalizeImageFile(file) {
  return new Promise((resolve, reject) => {
    if (!file.type.startsWith('image/') && !/\.(jpe?g|png|bmp|webp|heic|heif)$/i.test(file.name)) {
      return resolve(file);
    }

    const img = new Image();
    const url = URL.createObjectURL(file);

    img.onload = () => {
      URL.revokeObjectURL(url);
      try {
        const srcW = img.naturalWidth || img.width;
        const srcH = img.naturalHeight || img.height;

        // Normalize to the display shape. Portrait images use all available height
        // and are center-cropped horizontally, matching Auto mode on the device.
        const fitMode = 'fill';
        const { w, h } = calcDisplaySize(srcW, srcH, fitMode);

        console.log(`[Auto-Convert] ${file.name}: ${srcW}x${srcH} → ${w}x${h} (${fitMode})`);

        const canvas = document.createElement('canvas');
        canvas.width  = w;
        canvas.height = h;
        const ctx = canvas.getContext('2d');

        // Solid black background avoids alpha channel issues in JPEG output
        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, w, h);
        if (fitMode === 'fill' && srcW / srcH !== w / h) {
          const scale = Math.max(w / srcW, h / srcH);
          const cropW = w / scale;
          const cropH = h / scale;
          const sx = (srcW - cropW) / 2;
          const sy = (srcH - cropH) / 2;
          ctx.drawImage(img, sx, sy, cropW, cropH, 0, 0, w, h);
        } else {
          ctx.drawImage(img, 0, 0, w, h);
        }

        canvas.toBlob((blob) => {
          if (blob) {
            let baseName = file.name.replace(/\.[^/.]+$/, '');
            baseName = baseName.replace(/[^a-zA-Z0-9_-]/g, '_');
            const newFile = new File([blob], baseName + '.jpg', { type: 'image/jpeg' });
            console.log(`[Auto-Convert] Saved ${newFile.name} (${(blob.size/1024).toFixed(1)} KB Baseline JPEG)`);
            resolve(newFile);
          } else {
            resolve(file);
          }
        }, 'image/jpeg', 0.92);
      } catch (err) {
        console.warn('Canvas conversion error, using raw file:', err);
        resolve(file);
      }
    };

    img.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error(`The phone browser cannot read ${file.name}. Export/share it as JPEG and try again.`));
    };

    img.src = url;
  });
}

// File Upload Stream Queue
async function handleFiles(files){
  if(!files || !files.length) return;
  if (uploadBusy) {
    toast('An upload is already in progress', true);
    return;
  }
  uploadBusy = true;
  const queue = Array.from(files).filter(f =>
    f.type.startsWith('image/') || /\.(jpe?g|png|bmp|webp|heic|heif)$/i.test(f.name)
  );
  if(!queue.length){
    uploadBusy = false;
    toast("No supported image selected. Choose a photo from the phone's photo library.", true);
    return;
  }
  
  $('uploadProgressBox').style.display = 'block';
  let successCount = 0;
  
  try {
  for(let i=0; i < queue.length; i++){
    let file = queue[i];
    $('upBar').style.width = '0%';
    
    if ($('chkAutoConvert') && $('chkAutoConvert').checked) {
      $('upStatusTxt').textContent = `Optimizing ${i+1}/${queue.length}: ${file.name}...`;
      try {
        file = await normalizeImageFile(file);
      } catch(e) {
        console.warn("Optimization fallback:", e);
      }
    }

    $('upStatusTxt').textContent = `Uploading ${i+1}/${queue.length}: ${file.name}`;
    
    try {
      await uploadSingleFile(file);
      if ($('chkAutoConvert') && $('chkAutoConvert').checked) {
        const thumb = await createGalleryThumbnail(file);
        if (thumb) {
          try { await uploadSingleFile(thumb); }
          catch (thumbErr) { console.warn('Thumbnail upload failed:', thumbErr); }
        }
      }
      successCount++;
    } catch(err) {
      toast(`Failed to upload ${file.name}: ${err.message}`, true);
    }
  }
  
  } finally {
    $('uploadProgressBox').style.display = 'none';
    uploadBusy = false;
  }
  if (successCount > 0) toast(`Upload complete: ${successCount} photo(s) saved!`);
  else toast('No photos were uploaded. Check the error message and try a JPEG copy.', true);
  $('fileInput').value = '';
  await fetchLibrary();
  await fetchStatus();
}

function uploadSingleFile(file){
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const fd = new FormData();
    fd.append('file', file, file.name);
    
    const startTime = performance.now();
    xhr.upload.onprogress = e => {
      if(e.lengthComputable){
        const pct = Math.round((e.loaded / e.total) * 100);
        $('upBar').style.width = pct + '%';
        const elapsedSec = (performance.now() - startTime) / 1000;
        if(elapsedSec > 0.3){
          const speedKB = Math.round((e.loaded / 1024) / elapsedSec);
          $('upSpeed').textContent = `${speedKB} KB/s`;
        }
      }
    };
    
    xhr.onload = () => {
      if(xhr.status === 200) resolve();
      else reject(new Error(xhr.responseText || 'Server returned error'));
    };
    xhr.onerror = () => reject(new Error('Network error during upload'));
    xhr.open('POST', '/api/upload');
    xhr.send(fd);
  });
}

// Remote Controls
async function ctrlAction(act){
  try {
    const r = await fetch('/api/control?action=' + encodeURIComponent(act), {method:'POST'});
    if(r.ok) {
      setTimeout(fetchStatus, 300);
    } else {
      toast('Action failed', true);
    }
  } catch(e) {
    toast('Device unreachable', true);
  }
}

async function togglePlay(){
  const isPaused = currentStatus.paused;
  await ctrlAction(isPaused ? 'play' : 'pause');
}

async function showPhotoNow(name){
  try {
    const r = await fetch('/api/control?action=show&name=' + encodeURIComponent(name), {method:'POST'});
    if(r.ok){
      toast('Displaying ' + name);
      setTimeout(fetchStatus, 400);
    }
  } catch(e){
    toast('Error showing photo', true);
  }
}

// Settings
async function saveSettings(){
  const dur = $('setDuration').value;
  const ord = $('setOrder').value;
  const fit = $('setFit').value;
  const tra = $('setTransition').value;
  
  const body = new URLSearchParams({
    interval: dur,
    order: ord,
    fit: fit,
    trans: tra
  });
  
  try {
    const r = await fetch('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: body.toString()
    });
    if(r.ok) toast('Settings saved');
    else toast('Error saving settings', true);
  } catch(e) {
    toast('Network error saving settings', true);
  }
}

// Delete
async function deletePhoto(name){
  if(!confirm(`Delete "${name}" from SD card?`)) return;
  try {
    const r = await fetch('/api/delete?file=' + encodeURIComponent(name), {method:'POST'});
    if(r.ok){
      toast('Photo deleted');
      await fetchLibrary();
      await fetchStatus();
    } else {
      toast('Delete failed: ' + await r.text(), true);
    }
  } catch(e) {
    toast('Error deleting photo', true);
  }
}

async function confirmDeleteAll(){
  if(!confirm('Are you sure you want to delete ALL photos? This cannot be undone!')) return;
  try {
    const r = await fetch('/api/deleteall', {method:'POST'});
    if(r.ok){
      toast('All photos deleted');
      await fetchLibrary();
      await fetchStatus();
    } else {
      toast('Error deleting all photos', true);
    }
  } catch(e) {
    toast('Network error', true);
  }
}

// Library & Status Polling
async function fetchStatus(){
  try {
    const r = await fetch('/api/status');
    if(!r.ok) return;
    const d = await r.json();
    currentStatus = d;
    
    $('hdrSub').textContent = d.ap_mode ? 'AP Setup Mode (192.168.4.1)' : `Online: http://${d.ip}`;
    $('devIp').textContent = d.ip || 'Not connected';
    $('devSsid').textContent = d.ssid || (d.ap_mode ? 'PhotoFrame-Setup (AP)' : '--');
    $('devUptime').textContent = formatUptime(d.uptime);
    
    // Storage
    const usedMB = d.sd_used_mb || 0;
    const totalMB = d.sd_total_mb || 0;
    const freeMB = d.sd_free_mb || 0;
    if(totalMB > 0){
      const pct = Math.round((usedMB / totalMB) * 100);
      $('stoBar').style.width = pct + '%';
      $('stoTxt').textContent = `${(usedMB/1024).toFixed(2)} GB used / ${(totalMB/1024).toFixed(2)} GB (${(freeMB/1024).toFixed(2)} GB free)`;
    } else {
      $('stoBar').style.width = '0%';
      $('stoTxt').textContent = d.sd_mounted ? 'SD Ready' : 'SD Not Mounted';
    }
    
    // Now playing
    if(d.current_file){
      $('npName').textContent = d.current_file;
      $('npIdx').textContent = `Photo ${d.current_index + 1} of ${d.total_photos}`;
    } else {
      $('npName').textContent = d.total_photos > 0 ? 'Ready' : 'No photos on card';
      $('npIdx').textContent = `${d.total_photos} photos loaded`;
    }
    
    $('playBadge').textContent = d.paused ? 'Paused' : 'Playing';
    $('playBadge').className = d.paused ? 'badge' : 'badge badge-live';
    $('playBtn').innerHTML = d.paused ? '&#9654; Resume' : '&#9208; Pause';
    
    // Sync settings fields if not focused
    if(document.activeElement !== $('setDuration') && d.interval) $('setDuration').value = d.interval;
    if(document.activeElement !== $('setOrder') && d.order !== undefined) $('setOrder').value = d.order;
    if(document.activeElement !== $('setFit') && d.fit !== undefined) $('setFit').value = d.fit;
    if(document.activeElement !== $('setTransition') && d.trans !== undefined) $('setTransition').value = d.trans;
    
  } catch(e) {
    // Offline or connection drop
  }
}

async function fetchLibrary(){
  try {
    const r = await fetch('/api/photos');
    if(!r.ok) return;
    const items = await r.json();
    photoList = items;
    renderGallery(items);
  } catch(e) {
    console.warn('Could not load library', e);
  }
}

function renderGallery(items){
  const grid = $('photoGrid');
  $('galleryCount').textContent = `${items.length} photo(s) on SD card`;
  
  if(!items || !items.length){
    grid.innerHTML = `
      <div id="empty-state">
        <span class="icon">&#128247;</span>
        <h3>No Photos Found</h3>
        <p style="font-size:13px;margin-top:4px">Upload your photos above to start your photo frame slideshow!</p>
      </div>`;
    return;
  }
  
  grid.innerHTML = '';
  thumbQueue = []; // cancel any queued thumbnails from a previous render
  items.forEach(p => {
    const card = document.createElement('div');
    card.className = 'photo-card';
    
    const refreshToken = '&_=' + Date.now();
    const thumbUrl = '/api/thumb?file=' + encodeURIComponent(p.name) + '&v=' + encodeURIComponent(p.size) + refreshToken;
    const fileUrl = '/api/view?file=' + encodeURIComponent(p.name) + refreshToken;
    
    card.innerHTML = `
      <div class="photo-thumb-wrap" onclick="showPhotoNow('${escapeHtml(p.name)}')">
        <img class="photo-thumb" src="${THUMB_PLACEHOLDER}" alt="${escapeHtml(p.name)}">
      </div>
      <div class="photo-meta">
        <div class="photo-name" title="${escapeHtml(p.name)}">${escapeHtml(p.name)}</div>
        <div class="photo-size">${formatBytes(p.size)}</div>
        <div class="photo-actions">
          <button class="btn btn-sm" onclick="showPhotoNow('${escapeHtml(p.name)}')">&#9654; Display</button>
          <button class="btn btn-sm btn-danger" onclick="deletePhoto('${escapeHtml(p.name)}')">&#128465;</button>
        </div>
      </div>
    `;
    const img = card.querySelector('.photo-thumb');
    thumbQueue.push({img, url: thumbUrl, fallbackUrl: fileUrl});
    grid.appendChild(card);
  });
  pumpThumbnailQueue();
}

function escapeHtml(s){
  return (s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}

function formatBytes(bytes){
  if(!bytes || bytes <= 0) return '0 B';
  const k = 1024, dm = 1;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

function formatUptime(sec){
  if(!sec) return '0s';
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if(d > 0) return `${d}d ${h}h`;
  if(h > 0) return `${h}h ${m}m`;
  return `${m}m ${s}s`;
}

// WiFi Modal
function openWifiModal(){ $('wifiModal').classList.add('show'); }
function closeWifiModal(){ $('wifiModal').classList.remove('show'); }

async function saveWifiConfig(){
  const ssid = $('wSSID').value.trim();
  const pass = $('wPASS').value;
  if(!ssid){
    toast('WiFi SSID is required', true);
    return;
  }
  
  const body = new URLSearchParams({ssid: ssid, pass: pass});
  try {
    const r = await fetch('/api/wifi', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: body.toString()
    });
    if(r.ok){
      closeWifiModal();
      toast('WiFi saved! Frame is rebooting to connect...');
    } else {
      toast('Error saving WiFi credentials', true);
    }
  } catch(e) {
    toast('Error sending WiFi configuration', true);
  }
}

// Init & loop
fetchStatus();
fetchLibrary();
setInterval(fetchStatus, 3500);
</script>
</body>
</html>


)rawliteral";

#endif // INDEX_HTML_H
