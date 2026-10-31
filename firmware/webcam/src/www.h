// 网页界面（放在 Flash 里，不占 RAM）
#pragma once

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 图传</title>
<style>
  :root{color-scheme:dark}
  *{box-sizing:border-box}
  body{margin:0;background:#111418;color:#e6e9ef;font:14px/1.5 system-ui,"Microsoft YaHei",sans-serif;
       display:flex;flex-direction:column;align-items:center;min-height:100vh}
  header{width:100%;padding:14px 18px;background:#161b22;border-bottom:1px solid #232a33;
         display:flex;gap:14px;align-items:center;flex-wrap:wrap}
  h1{font-size:15px;margin:0;font-weight:600;letter-spacing:.3px}
  .pill{padding:3px 10px;border-radius:999px;background:#1f6feb22;color:#79c0ff;
        border:1px solid #1f6feb55;font-size:12px}
  .pill.warn{background:#bb800922;color:#e3b341;border-color:#bb800955}
  main{padding:18px;display:flex;flex-direction:column;gap:14px;align-items:center;width:100%;max-width:900px}
  .viewer{position:relative;width:100%;background:#0d1117;border:1px solid #232a33;border-radius:12px;overflow:hidden}
  img{display:block;width:100%;height:auto;min-height:180px}
  .meta{display:flex;gap:18px;flex-wrap:wrap;font-size:12.5px;color:#9aa4b2}
  .meta b{color:#e6e9ef;font-weight:600}
  .row{display:flex;gap:10px;flex-wrap:wrap}
  button,a.btn{cursor:pointer;border:1px solid #2a323d;background:#1a2029;color:#e6e9ef;
         padding:8px 14px;border-radius:9px;font-size:13px;text-decoration:none}
  button:hover,a.btn:hover{background:#222a35}
  button.primary{background:#1f6feb;border-color:#1f6feb}
  button.primary:hover{background:#2d7dff}
  footer{padding:16px;color:#5c6673;font-size:12px}
  .err{color:#f85149;font-size:12px}
</style>
</head>
<body>
<header>
  <h1>ESP32-S3-LCD-2 · 网页图传</h1>
  <span class="pill" id="mode">连接中…</span>
  <span class="pill warn" id="rssi">—</span>
</header>

<main>
  <div class="viewer">
    <img id="stream" src="/stream" alt="视频流">
  </div>

  <div class="meta">
    <span>分辨率 <b id="res">—</b></span>
    <span>帧率 <b id="fps">—</b></span>
    <span>已传 <b id="frames">—</b> 帧</span>
    <span>堆 <b id="heap">—</b></span>
    <span>运行 <b id="uptime">—</b></span>
    <span class="err" id="err"></span>
  </div>

  <div class="row">
    <button class="primary" onclick="shot()">拍一张存卡</button>
    <a class="btn" href="/capture" download="snap.jpg">直接下载当前帧</a>
    <button onclick="restart()">重启板子</button>
  </div>

  <footer>速度慢先看信号强度；画面卡住会自动重连。</footer>
</main>

<script>
const $ = id => document.getElementById(id);
const img = $('stream');

img.onerror = () => { $('err').textContent = '视频流断开，2 秒后重连…';
  setTimeout(() => { img.src = '/stream?t=' + Date.now(); $('err').textContent = ''; }, 2000); };

async function status(){
  try{
    const r = await fetch('/status', {cache:'no-store'});
    const s = await r.json();
    $('mode').textContent = s.mode + (s.ip ? ' · ' + s.ip : '');
    $('rssi').textContent = s.rssi ? (s.rssi + ' dBm') : '—';
    $('res').textContent  = s.res;
    $('fps').textContent  = s.fps.toFixed(1);
    $('frames').textContent = s.frames;
    $('heap').textContent = (s.heap/1024).toFixed(0) + ' KB';
    $('uptime').textContent = Math.floor(s.uptime/1000) + ' s';
  }catch(e){ $('err').textContent = '状态获取失败'; }
}
async function shot(){
  const r = await fetch('/shoot');
  const t = await r.text();
  $('err').textContent = '';
  alert(t);
}
async function restart(){
  if(!confirm('确定重启？')) return;
  await fetch('/restart'); $('err').textContent = '已请求重启，页面稍后刷新';
  setTimeout(()=>location.reload(), 4000);
}
setInterval(status, 1000); status();
</script>
</body>
</html>
)HTML";
