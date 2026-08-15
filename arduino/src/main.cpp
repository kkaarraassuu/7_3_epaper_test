#include <Arduino.h>
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
WebServer server(80);
Preferences preferences;
uint8_t* uploadBuffer = nullptr;
size_t uploadBytes = 0;
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
const W=800,H=480,C=[[15,15,15,0],[245,242,232,1],[235,195,30,2],[200,45,38,3],[38,75,160,5],[55,135,70,6]];
const cv=document.querySelector('#canvas'),ctx=cv.getContext('2d',{willReadFrequently:true});
const fi=document.querySelector('#file'),send=document.querySelector('#send'),st=document.querySelector('#status');
let packed=null;
const clamp=(v,a=0,b=255)=>Math.max(a,Math.min(b,v));
function lab(r,g,b){const f=v=>{v/=255;return v<=.04045?v/12.92:((v+.055)/1.055)**2.4};r=f(r);g=f(g);b=f(b);let x=(r*.4124564+g*.3575761+b*.1804375)/.95047,y=(r*.2126729+g*.7151522+b*.072175),z=(r*.0193339+g*.119192+b*.9503041)/1.08883;const q=t=>t>.008856?t**(1/3):7.787*t+16/116,fx=q(x),fy=q(y),fz=q(z);return[116*fy-16,500*(fx-fy),200*(fy-fz)]}
function de(a,b){const [L1,a1,b1]=a,[L2,a2,b2]=b,C1=Math.hypot(a1,b1),C2=Math.hypot(a2,b2),C=(C1+C2)/2,G=.5*(1-Math.sqrt(C**7/(C**7+25**7))),ap1=(1+G)*a1,ap2=(1+G)*a2,cp1=Math.hypot(ap1,b1),cp2=Math.hypot(ap2,b2),ac=(cp1+cp2)/2;let h1=Math.atan2(b1,ap1)*180/Math.PI;if(h1<0)h1+=360;let h2=Math.atan2(b2,ap2)*180/Math.PI;if(h2<0)h2+=360;const dl=L2-L1,dc=cp2-cp1;let dh=h2-h1;if(cp1*cp2===0)dh=0;else if(dh>180)dh-=360;else if(dh<-180)dh+=360;const dH=2*Math.sqrt(cp1*cp2)*Math.sin(dh*Math.PI/360);let ah;if(cp1*cp2===0)ah=h1+h2;else if(Math.abs(h1-h2)<=180)ah=(h1+h2)/2;else if(h1+h2<360)ah=(h1+h2+360)/2;else ah=(h1+h2-360)/2;const T=1-.17*Math.cos((ah-30)*Math.PI/180)+.24*Math.cos(2*ah*Math.PI/180)+.32*Math.cos((3*ah+6)*Math.PI/180)-.20*Math.cos((4*ah-63)*Math.PI/180),dt=30*Math.exp(-(((ah-275)/25)**2)),rc=2*Math.sqrt(ac**7/(ac**7+25**7)),sl=1+.015*((L1+L2)/2-50)**2/Math.sqrt(20+((L1+L2)/2-50)**2),sc=1+.045*ac,sh=1+.015*ac*T,rt=-Math.sin(2*dt*Math.PI/180)*rc,A=dl/sl,B=dc/sc,D=dH/sh;return Math.sqrt(A*A+B*B+D*D+rt*B*D)}
const PL=C.map(c=>[...c,lab(c[0],c[1],c[2])]);
function skin(r,g,b){const mx=Math.max(r,g,b),mn=Math.min(r,g,b),v=mx/255,s=mx?((mx-mn)/mx):0;let h=0;if(mx!==mn){if(mx===r)h=60*((g-b)/(mx-mn)%6);else if(mx===g)h=60*((b-r)/(mx-mn)+2);else h=60*((r-g)/(mx-mn)+4);if(h<0)h+=360}const hd=h>180?Math.min(Math.abs(h-360),Math.abs(h)):Math.abs(h-25),hs=clamp(1-hd/55,0,1),ss=clamp((s-.05)/.55,0,1),vs=clamp((v-.18)/.55,0,1),cb=128-.168736*r-.331264*g+.5*b,cr=128+.5*r-.418688*g-.081312*b;return hs*ss*vs*clamp(1-Math.abs(cb-103)/45,0,1)*clamp(1-Math.abs(cr-153)/45,0,1)}
function nearest(r,g,b){const l=lab(r,g,b),br=.2126*r+.7152*g+.0722*b,sp=skin(r,g,b);let best=PL[0],bd=1e9;for(const c of PL){let d=de(l,c[4]);if(c[3]===1){d*=1.035;if(br<175)d*=1.035}if(c[3]===0){d*=1.015;if(br>120)d*=1.025}const a=sp*.22;if(c[3]===2)d*=1-.18*a;else if(c[3]===3)d*=1-.12*a;else if(c[3]===5)d*=1+.18*a;else if(c[3]===6)d*=1+.14*a;if(d<bd){bd=d;best=c}}return best}
async function convert(){st.textContent='高画質6色変換を準備中…';send.disabled=true;await new Promise(requestAnimationFrame);const im=ctx.getImageData(0,0,W,H),d=im.data,src=new Uint8ClampedArray(d),n=W*H,er=new Float32Array(n),eg=new Float32Array(n),eb=new Float32Array(n),codes=new Uint8Array(n);for(let i=0;i<n;i++){const p=i*4,r=src[p],g=src[p+1],b=src[p+2],l=.2126*r+.7152*g+.0722*b;src[p]=clamp(128+(l+(r-l)*1.08-128)*1.04);src[p+1]=clamp(128+(l+(g-l)*1.08-128)*1.04);src[p+2]=clamp(128+(l+(b-l)*1.08-128)*1.04)}const lum=(x,y)=>{const p=(y*W+x)*4;return .2126*src[p]+.7152*src[p+1]+.0722*src[p+2]};for(let y=0;y<H;y++){const rev=y&1,dir=rev?-1:1;for(let q=0;q<W;q++){const x=rev?W-1-q:q,i=y*W+x,p=i*4,r=clamp(src[p]+er[i]),g=clamp(src[p+1]+eg[i]),b=clamp(src[p+2]+eb[i]),c=nearest(r,g,b);codes[i]=c[3];d[p]=c[0];d[p+1]=c[1];d[p+2]=c[2];let edge=0;if(x>0&&x<W-1&&y>0&&y<H-1){const gx=-lum(x-1,y-1)+lum(x+1,y-1)-2*lum(x-1,y)+2*lum(x+1,y)-lum(x-1,y+1)+lum(x+1,y+1),gy=-lum(x-1,y-1)-2*lum(x,y-1)-lum(x+1,y-1)+lum(x-1,y+1)+2*lum(x,y+1)+lum(x+1,y+1);edge=Math.hypot(gx,gy)}const ef=edge<=20?1:edge>=90?.72:1-(edge-20)/70*.28,s=.92*ef,rr=clamp((r-c[0])*s,-72,72),gg=clamp((g-c[1])*s,-72,72),bb=clamp((b-c[2])*s,-72,72);function add(xx,yy,w){if(xx>=0&&xx<W&&yy<H){const j=yy*W+xx;er[j]+=rr*w;eg[j]+=gg*w;eb[j]+=bb*w}}add(x+dir,y,8/42);add(x+2*dir,y,4/42);add(x-2*dir,y+1,2/42);add(x-dir,y+1,4/42);add(x,y+1,8/42);add(x+dir,y+1,4/42);add(x+2*dir,y+1,2/42);add(x-2*dir,y+2,1/42);add(x-dir,y+2,2/42);add(x,y+2,4/42);add(x+dir,y+2,2/42);add(x+2*dir,y+2,1/42)}if(y%16===15){st.textContent='高画質変換中… '+Math.round((y+1)*100/H)+'%';await new Promise(requestAnimationFrame)}}ctx.putImageData(im,0,0);packed=new Uint8Array(n/2);for(let i=0,j=0;i<n;i+=2,j++)packed[j]=(codes[i]<<4)|codes[i+1];st.textContent='高画質変換完了。プレビューを確認してください。';send.disabled=false}
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

bool displayUploadedImage(const uint8_t* image, size_t imageSize) {
  if (image == nullptr || imageSize != IMAGE_BYTES) {
    Serial.println("uploaded image is missing or invalid");
    return false;
  }
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  if (!initDisplay()) { SPI.endTransaction(); return false; }
  sendCommand(0x10);
  for (size_t i = 0; i < imageSize; ++i) {
    sendData(image[i]);
    if (i % 19200 == 0) Serial.printf("transfer: %u%%\n", unsigned(i * 100 / IMAGE_BYTES));
    if (i % 4096 == 0) yield();
  }
  const bool ok = refreshDisplay();
  sleepDisplay();
  SPI.endTransaction();
  return ok;
}

void handleUploadData() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadOk = false;
    uploadError = "";
    uploadBytes = 0;
    if (uploadBuffer != nullptr) free(uploadBuffer);
    uploadBuffer = static_cast<uint8_t*>(ps_malloc(IMAGE_BYTES));
    if (uploadBuffer == nullptr) uploadError = "画像用PSRAMを確保できません。";
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadBuffer != nullptr && uploadError.isEmpty()) {
      if (uploadBytes + upload.currentSize > IMAGE_BYTES) uploadError = "画像データが大きすぎます。";
      else {
        memcpy(uploadBuffer + uploadBytes, upload.buf, upload.currentSize);
        uploadBytes += upload.currentSize;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadError.isEmpty() && uploadBytes == IMAGE_BYTES) uploadOk = true;
    else if (uploadError.isEmpty()) {
      uploadError = "データサイズが不正です。";
    }
    if (!uploadOk && uploadBuffer != nullptr) { free(uploadBuffer); uploadBuffer = nullptr; }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadBuffer != nullptr) { free(uploadBuffer); uploadBuffer = nullptr; }
    uploadBytes = 0;
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
  const bool ok = displayUploadedImage(uploadBuffer, uploadBytes);
  free(uploadBuffer);
  uploadBuffer = nullptr;
  uploadBytes = 0;
  Serial.println(ok ? "display complete" : "display failed");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serialStarted = millis();
  while (!Serial && millis() - serialStarted < 3000) delay(10);
  Serial.println();
  Serial.println("Booting Spectra 6 photo frame...");
  pinMode(PIN_BUSY, INPUT);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, HIGH);
  SPI.begin();  // Nano ESP32: D11=MOSI, D13=SCK

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Wi-Fi access point failed");
    return;
  }
  Serial.printf("Setup Wi-Fi ready: %s\n", AP_SSID);
  Serial.printf("Setup page: http://%s/\n", WiFi.softAPIP().toString().c_str());
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
