// 网页界面（分块发送，关键内容由服务端渲染 → 浏览器禁用 JS 也能用）
#pragma once

// ---- 1) 文档头 + 样式 + 顶栏（不含具体数值） ----
static const char PAGE_HEAD[] PROGMEM = R"HTML(
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
  .pill.ok{background:#23863622;color:#7ee787;border-color:#23863655}
  main{padding:18px;display:flex;flex-direction:column;gap:14px;align-items:center;width:100%;max-width:900px}
  .viewer{position:relative;width:100%;background:#0d1117;border:1px solid #232a33;border-radius:12px;overflow:hidden}
  img{display:block;width:100%;height:auto;min-height:180px}
  .meta{display:flex;gap:14px;flex-wrap:wrap;font-size:12.5px;color:#9aa4b2;
        justify-content:center;padding:2px 0}
  .meta span{white-space:nowrap}
  .meta b{color:#e6e9ef;font-weight:600}
  .row{display:flex;gap:10px;flex-wrap:wrap;justify-content:center}
  .btn{cursor:pointer;border:1px solid #2a323d;background:#1a2029;color:#e6e9ef;
       padding:9px 16px;border-radius:9px;font-size:13px;text-decoration:none;display:inline-block}
  .btn:hover{background:#222a35}
  .btn.primary{background:#1f6feb;border-color:#1f6feb;color:#fff}
  .btn.primary:hover{background:#2d7dff}
  footer{padding:16px;color:#5c6673;font-size:12px;text-align:center}
  .note{color:#8b949e;font-size:12px}
</style>
</head>
<body>
<header>
  <h1>ESP32-S3-LCD-2 · 网页图传</h1>
)HTML";

// ---- 2) 中间：图片 + 状态行（数值由服务端填充） ----
static const char PAGE_MID[] PROGMEM = R"HTML(
</header>
<main>
  <div class="viewer"><img id="stream" src="/stream" alt="视频流"></div>
  <div class="meta">
)HTML";

// ---- 3) 尾部：按钮 + 说明 + 可选 JS ----
static const char PAGE_TAIL[] PROGMEM = R"HTML(
  </div>
  <div class="row">
    <a class="btn primary" href="/shoot">拍一张存卡</a>
    <a class="btn" href="/capture" download="snap.jpg">下载当前帧</a>
    <a class="btn" href="/restart" onclick="return confirm('确定重启板子？')">重启板子</a>
    <a class="btn" href="/">刷新状态</a>
  </div>
  <p class="note">数值是打开本页那一刻的快照，点"刷新状态"更新；上方画面是实时流。</p>
</main>
<footer>画质/分辨率调整接口开发中</footer>
<script>
/* 有 JS 就顺便每秒刷新一次数值；没有也能正常用上面所有按钮 */
(function(){
  if(!window.fetch) return;
  var ids=['m_mode','m_rssi','m_res','m_fps','m_frames','m_heap','m_uptime','m_sd'];
  function set(id,val){var e=document.getElementById(id); if(e) e.textContent=val;}
  function tick(){
    fetch('/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){
      set('m_mode', s.mode + (s.ip ? ' · ' + s.ip : ''));
      set('m_rssi', s.rssi ? s.rssi + ' dBm' : '—');
      set('m_res', s.res);
      set('m_fps', Number(s.fps).toFixed(1));
      set('m_frames', s.frames);
      set('m_heap', Math.round(s.heap/1024) + ' KB');
      set('m_uptime', Math.floor(s.uptime/1000) + ' s');
      set('m_sd', s.sd ? '已挂载' : '未插卡');
    }).catch(function(){});
  }
  setInterval(tick, 1000); tick();
})();
</script>
</body>
</html>
)HTML";

// ---- 4) /shoot 结果页 ----
static const char RESULT_HEAD[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>结果</title>
<style>body{background:#111418;color:#e6e9ef;font:16px/1.6 system-ui,"Microsoft YaHei",sans-serif;
padding:40px 20px;text-align:center}a{color:#79c0ff}</style></head><body><p>
)HTML";

static const char RESULT_TAIL[] PROGMEM = R"HTML(
</p><p><a href="/">← 返回</a></p></body></html>
)HTML";
