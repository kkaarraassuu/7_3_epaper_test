#include <Arduino.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
constexpr uint8_t PIN_BUSY = D7;
constexpr uint8_t PIN_RST = D8;
constexpr uint8_t PIN_DC = D9;
constexpr uint8_t PIN_CS = D10;
constexpr uint16_t EPD_WIDTH = 800;
constexpr uint16_t EPD_HEIGHT = 480;
constexpr size_t IMAGE_BYTES = EPD_WIDTH * EPD_HEIGHT / 2;

constexpr char AP_SSID[] = "Spectra6-Frame";
constexpr char AP_PASSWORD[] = "spectra6";  // 8文字以上必要
constexpr char IMAGE_PATH[] = "/image.bin";
constexpr char TEMP_PATH[] = "/upload.tmp";

WebServer server(80);
Preferences preferences;
File uploadFile;
bool uploadOk = false;
String uploadError;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Spectra 6 Photo Upload</title>
<style>
body{font-family:system-ui,sans-serif;max-width:820px;margin:auto;padding:18px;background:#f3f1eb;color:#202020}
.card{background:white;border-radius:14px;padding:18px;box-shadow:0 2px 12px #0002}
h1{font-size:1.35rem;margin-top:0}canvas{width:100%;height:auto;background:#ddd;border-radius:8px;image-rendering:auto}
input,button{font:inherit;margin-top:12px}button{padding:11px 16px;border:0;border-radius:9px;background:#1769aa;color:white;font-weight:700}
button:disabled{opacity:.45}.status{min-height:1.5em;margin:12px 0}.note{font-size:.9rem;color:#555}
</style>
</head>
<body><div class="card">
<h1>Spectra 6 フォトフレーム</h1>
<p>写真を選ぶと端末内で800×480・6色に変換します。プレビューを確認して表示してください。</p>
<input id="file" type="file" accept="image/jpeg,image/png,image/webp,image/heic,image/heif,.jpg,.jpeg,.png,.webp,.heic,.heif">
<canvas id="canvas" width="800" height="480"></canvas>
<div class="status" id="status">写真を選択してください。</div>
<button id="send" disabled>e-Paperに表示</button>
<p class="note">変換・送信中はこのページを閉じないでください。画面更新には約25秒かかります。</p>
<hr>
<h2>自宅Wi-Fi設定</h2>
<p id="network">接続状態を確認中…</p>
<form id="wifi"><input id="ssid" name="ssid" placeholder="Wi-Fi名 (SSID)" required>
<input id="password" name="password" type="password" placeholder="パスワード">
<button type="submit">保存して接続</button></form>
<p class="note">設定後に本体が再起動します。接続できない場合も、設定用Wi-Fi <b>Spectra6-Frame</b>から再設定できます。</p>
</div>
<script>
const W=800,H=480,C=[[12,12,12,0],[248,245,235,1],[238,198,28,2],[204,48,40,3],[38,76,163,5],[54,137,70,6]];
const cv=document.querySelector('#canvas'),ctx=cv.getContext('2d',{willReadFrequently:true});
const fi=document.querySelector('#file'),send=document.querySelector('#send'),st=document.querySelector('#status');
let packed=null;
function nearest(r,g,b){let best=C[0],bd=1e20;for(const c of C){const dr=r-c[0],dg=g-c[1],db=b-c[2];const d=2*dr*dr+4*dg*dg+3*db*db;if(d<bd){bd=d;best=c}}return best}
function convert(){st.textContent='6色へ変換中…';send.disabled=true;setTimeout(()=>{
 const im=ctx.getImageData(0,0,W,H),d=im.data,n=W*H,er=new Float32Array(n),eg=new Float32Array(n),eb=new Float32Array(n),codes=new Uint8Array(n);
 for(let y=0;y<H;y++){const rev=y&1;for(let q=0;q<W;q++){const x=rev?W-1-q:q,i=y*W+x,p=i*4;
  const r=Math.max(0,Math.min(255,d[p]+er[i])),g=Math.max(0,Math.min(255,d[p+1]+eg[i])),b=Math.max(0,Math.min(255,d[p+2]+eb[i]));
  const c=nearest(r,g,b);codes[i]=c[3];d[p]=c[0];d[p+1]=c[1];d[p+2]=c[2];
  const rr=(r-c[0])*.9,gg=(g-c[1])*.9,bb=(b-c[2])*.9,dir=rev?-1:1;
  function add(xx,yy,w){if(xx>=0&&xx<W&&yy<H){const j=yy*W+xx;er[j]+=rr*w;eg[j]+=gg*w;eb[j]+=bb*w}}
  add(x+dir,y,7/16);add(x-dir,y+1,3/16);add(x,y+1,5/16);add(x+dir,y+1,1/16);
 }}
 ctx.putImageData(im,0,0);packed=new Uint8Array(n/2);for(let i=0,j=0;i<n;i+=2,j++)packed[j]=(codes[i]<<4)|codes[i+1];
 st.textContent='変換完了。プレビューを確認してください。';send.disabled=false;
},40)}
fi.onchange=()=>{const f=fi.files[0];if(!f)return;packed=null;send.disabled=true;st.textContent='写真を読み込み中…';const url=URL.createObjectURL(f),img=new Image();img.onload=()=>{const sr=img.naturalWidth/img.naturalHeight,tr=W/H;let sx=0,sy=0,sw=img.naturalWidth,sh=img.naturalHeight;if(sr>tr){sw=sh*tr;sx=(img.naturalWidth-sw)/2}else{sh=sw/tr;sy=(img.naturalHeight-sh)/2}ctx.save();ctx.fillStyle='#fff';ctx.fillRect(0,0,W,H);ctx.drawImage(img,sx,sy,sw,sh,0,0,W,H);ctx.restore();URL.revokeObjectURL(url);convert()};img.onerror=()=>{URL.revokeObjectURL(url);const heic=/\.(heic|heif)$/i.test(f.name)||/hei[cf]/i.test(f.type);st.textContent=heic?'HEICを読み込めません。iOS 17以降のSafariを使うか、写真をJPEGで共有して選択してください。':'画像を読み込めません。JPEG、PNG、WebP、HEICを選択してください。'};img.src=url};
send.onclick=async()=>{if(!packed)return;send.disabled=true;st.textContent='送信中…';try{const form=new FormData();form.append('image',new Blob([packed],{type:'application/octet-stream'}),'image.bin');const r=await fetch('/upload',{method:'POST',body:form});const t=await r.text();if(!r.ok)throw new Error(t);st.textContent=t}catch(e){st.textContent='エラー: '+e.message;send.disabled=false}};
fetch('/status').then(r=>r.json()).then(s=>{document.querySelector('#network').textContent=s.connected?'接続中: '+s.ssid+' / アドレス: '+s.ip:'自宅Wi-Fiには未接続です。';document.querySelector('#ssid').value=s.ssid||''});
document.querySelector('#wifi').onsubmit=async e=>{e.preventDefault();const body=new URLSearchParams({ssid:document.querySelector('#ssid').value,password:document.querySelector('#password').value});const r=await fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});document.querySelector('#network').textContent=await r.text()};
</script></body></html>
)HTML";

String jsonEscape(const String& value) {
  String result;
  result.reserve(value.length() + 8);
  for (char c : value) {
    if (c == '\\' || c == '"') result += '\\';
    result += c;
  }
  return result;
}

void connectSavedWifi() {
  preferences.begin("wifi", true);
  const String ssid = preferences.getString("ssid", "");
  const String password = preferences.getString("password", "");
  preferences.end();
  if (ssid.isEmpty()) return;
  Serial.printf("Connecting to Wi-Fi: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Home Wi-Fi address: http://%s/\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("spectra6-frame")) Serial.println("mDNS: http://spectra6-frame.local/");
  } else {
    Serial.println("Home Wi-Fi connection failed; setup AP remains available");
  }
}

void sendCommand(uint8_t value) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(value);
  digitalWrite(PIN_CS, HIGH);
}

void sendData(uint8_t value) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(value);
  digitalWrite(PIN_CS, HIGH);
}

bool waitReady(const char* stage, uint32_t timeoutMs = 60000) {
  Serial.print(stage);
  const uint32_t start = millis();
  while (digitalRead(PIN_BUSY) == LOW) {
    if (millis() - start >= timeoutMs) {
      Serial.println(": BUSY timeout");
      return false;
    }
    delay(1);
  }
  Serial.println(": ready");
  return true;
}

void resetDisplay() {
  digitalWrite(PIN_RST, HIGH); delay(20);
  digitalWrite(PIN_RST, LOW); delay(2);
  digitalWrite(PIN_RST, HIGH); delay(20);
}

bool initDisplay() {
  resetDisplay();
  if (!waitReady("reset")) return false;
  delay(30);
  sendCommand(0xAA);
  for (uint8_t v : {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}) sendData(v);
  sendCommand(0x01); sendData(0x3F);
  sendCommand(0x00); sendData(0x5F); sendData(0x69);
  sendCommand(0x03); for (uint8_t v : {0x00, 0x54, 0x00, 0x44}) sendData(v);
  sendCommand(0x05); for (uint8_t v : {0x40, 0x1F, 0x1F, 0x2C}) sendData(v);
  sendCommand(0x06); for (uint8_t v : {0x6F, 0x1F, 0x17, 0x49}) sendData(v);
  sendCommand(0x08); for (uint8_t v : {0x6F, 0x1F, 0x1F, 0x22}) sendData(v);
  sendCommand(0x30); sendData(0x03);
  sendCommand(0x50); sendData(0x3F);
  sendCommand(0x60); sendData(0x02); sendData(0x00);
  sendCommand(0x61); sendData(0x03); sendData(0x20); sendData(0x01); sendData(0xE0);
  sendCommand(0x84); sendData(0x01);
  sendCommand(0xE3); sendData(0x2F);
  sendCommand(0x04);
  return waitReady("power on", 30000);
}

bool refreshDisplay() {
  sendCommand(0x04);
  if (!waitReady("refresh power on", 30000)) return false;
  sendCommand(0x06); for (uint8_t v : {0x6F, 0x1F, 0x17, 0x49}) sendData(v);
  sendCommand(0x12); sendData(0x00);
  if (!waitReady("display refresh", 60000)) return false;
  sendCommand(0x02); sendData(0x00);
  return waitReady("power off", 30000);
}

void sleepDisplay() {
  sendCommand(0x02); sendData(0x00);
  waitReady("sleep power off", 30000);
  sendCommand(0x07); sendData(0xA5);
}

bool displayStoredImage() {
  File image = LittleFS.open(IMAGE_PATH, "r");
  if (!image || image.size() != IMAGE_BYTES) {
    Serial.println("stored image is missing or invalid");
    if (image) image.close();
    return false;
  }
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  if (!initDisplay()) { SPI.endTransaction(); image.close(); return false; }
  sendCommand(0x10);
  uint8_t buffer[1024];
  size_t sent = 0;
  while (image.available()) {
    const size_t count = image.read(buffer, sizeof(buffer));
    for (size_t i = 0; i < count; ++i) sendData(buffer[i]);
    sent += count;
    if (sent % 19200 < sizeof(buffer)) Serial.printf("transfer: %u%%\n", unsigned(sent * 100 / IMAGE_BYTES));
    yield();
  }
  image.close();
  const bool ok = sent == IMAGE_BYTES && refreshDisplay();
  sleepDisplay();
  SPI.endTransaction();
  return ok;
}

void handleUploadData() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadOk = false;
    uploadError = "";
    LittleFS.remove(TEMP_PATH);
    uploadFile = LittleFS.open(TEMP_PATH, "w");
    if (!uploadFile) uploadError = "一時ファイルを作成できません。";
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadError.isEmpty()) {
      if (uploadFile.size() + upload.currentSize > IMAGE_BYTES ||
          uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
        uploadError = "画像データの保存に失敗しました。";
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    File check = LittleFS.open(TEMP_PATH, "r");
    const size_t size = check ? check.size() : 0;
    if (check) check.close();
    if (uploadError.isEmpty() && size == IMAGE_BYTES) {
      LittleFS.remove(IMAGE_PATH);
      uploadOk = LittleFS.rename(TEMP_PATH, IMAGE_PATH);
      if (!uploadOk) uploadError = "画像ファイルを確定できません。";
    } else if (uploadError.isEmpty()) {
      uploadError = "データサイズが不正です。";
    }
    if (!uploadOk) LittleFS.remove(TEMP_PATH);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    LittleFS.remove(TEMP_PATH);
    uploadError = "アップロードが中断されました。";
  }
}

void finishUpload() {
  if (!uploadOk) {
    server.send(400, "text/plain; charset=utf-8", uploadError.isEmpty() ? "アップロードに失敗しました。" : uploadError);
    return;
  }
  server.send(200, "text/plain; charset=utf-8", "受信完了。e-Paperを更新しています… 約25秒お待ちください。");
  delay(100);
  const bool ok = displayStoredImage();
  Serial.println(ok ? "display complete" : "display failed");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PIN_BUSY, INPUT);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, HIGH);
  SPI.begin();  // Nano ESP32: D11=MOSI, D13=SCK

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Wi-Fi access point failed");
    return;
  }
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/status", HTTP_GET, []() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    preferences.begin("wifi", true);
    const String saved = preferences.getString("ssid", "");
    preferences.end();
    const String name = connected ? WiFi.SSID() : saved;
    const String ip = connected ? WiFi.localIP().toString() : "";
    server.send(200, "application/json", "{\"connected\":" + String(connected ? "true" : "false") +
      ",\"ssid\":\"" + jsonEscape(name) + "\",\"ip\":\"" + ip + "\"}");
  });
  server.on("/wifi", HTTP_POST, []() {
    const String ssid = server.arg("ssid");
    const String password = server.arg("password");
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
      server.send(400, "text/plain; charset=utf-8", "SSIDまたはパスワードが不正です。");
      return;
    }
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    server.send(200, "text/plain; charset=utf-8", "保存しました。本体を再起動して接続します…");
    delay(500);
    ESP.restart();
  });
  server.on("/upload", HTTP_POST, finishUpload, handleUploadData);
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302); });
  server.begin();
  connectSavedWifi();
  Serial.println("Spectra 6 smartphone uploader ready");
  Serial.printf("Wi-Fi: %s\n", AP_SSID);
  Serial.printf("Open: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();
  delay(2);
}
