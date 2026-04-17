#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include "esp_task_wdt.h"
#include <NeoPixelBus.h>

#define LED_PIN 5
#define NUM_LEDS 240

const char* ssid     = "your-network";
const char* password = "your-password";

AsyncWebServer server(80);
NeoPixelBus<NeoGrbwFeature, NeoEsp32I2s0800KbpsMethod> strip(NUM_LEDS, LED_PIN);

File imageFile;
File animFile;

uint16_t animWidth  = 0;
uint16_t animHeight = 0;
uint16_t speedMs    = 50;
uint16_t currentCol = 0;
unsigned long lastUpdate = 0;
bool newAnimationReady = false;
uint8_t* animData = nullptr;

uint8_t columnBuffer[NUM_LEDS * 3];
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LED Animator</title>

<style>
:root {
  --bg: #0f1115;
  --panel: #171a21;
  --panel2: #1f2430;
  --text: #e8ecf1;
  --muted: #9aa4b2;
  --accent: #4f8cff;
  --border: #2a3140;
}

body {
  margin: 0;
  font-family: Arial;
  background: var(--bg);
  color: var(--text);
}

.wrap {
  padding: 16px;
}

/* 🔥 MAIN LAYOUT */
.layout {
  display: flex;
  gap: 20px;
  align-items: flex-start;
}

/* LEFT PANEL */
.controls {
  width: 320px;
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 16px;
}

/* RIGHT PANEL */
.preview {
  flex: 1;
  display: flex;
  gap: 20px;
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 16px;
}

/* CANVAS */
canvas {
  background: black;
  image-rendering: pixelated;
  border: 1px solid #333;
  max-width: 800px;
}

/* LED STRIP */
#ledStrip {
  display: flex;
  flex-direction: column;
  height: 720px;
  overflow: auto;
}

.led {
  width: 10px;
  height: 10px;
  margin: 1px;
  border-radius: 50%;
  background: black;
}

/* INPUTS */
input, button {
  margin: 6px 0;
}

input[type="number"] {
  width: 80px;
}

button {
  padding: 8px 10px;
  border-radius: 8px;
  border: none;
  background: var(--accent);
  color: white;
  cursor: pointer;
}

button:hover {
  filter: brightness(1.1);
}

h2 {
  margin-top: 0;
}

.label {
  color: var(--muted);
  font-size: 14px;
}
</style>
</head>

<body>
<div class="wrap">

<h2>LED Animator</h2>

<div class="layout">

  <div class="controls">

    <div class="label">Image</div>
    <input type="file" id="file">

    <div class="label">Frames</div>
    <input type="number" id="frameWidth" value="150" min="1" max="150">

    <div class="label">Speed</div>
    <input type="range" id="speed" min="10" max="300" value="50">
    <span id="speedValue">50</span> ms

    <div class="label">Color</div>
    <input type="color" id="color" value="#ff0000">

    <div class="label">Brush</div>
    <input type="range" id="brush" min="1" max="10" value="1">
    <span id="brushValue">1</span>

    <br>

    <button onclick="startPreview()">Preview</button>
    <button onclick="stopPreview()">Stop</button>
    <button onclick="uploadAnimation()">Upload</button>
    <button onclick="clearCanvas()">Clear</button>
    <button onclick="downloadImage()">Download</button>

    <p id="status">Ready</p>

  </div>

  <div class="preview">

    <canvas id="canvas"></canvas>

    <div>
      <div class="label">LED Preview</div>
      <div id="ledStrip"></div>
    </div>

  </div>

</div>

</div>

<script>
const NUM_LEDS = 240;

const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const ledStrip = document.getElementById("ledStrip");

let animWidth = 150;
let animHeight = NUM_LEDS;

let drawing = false;
let color = "#ff0000";
let brush = 1;
let previewTimer = null;
let currentCol = 0;

function initCanvas() {
  canvas.width = animWidth;
  canvas.height = animHeight;
  canvas.style.width = animWidth * 3 + "px";
  canvas.style.height = "720px";

  ctx.fillStyle = "black";
  ctx.fillRect(0,0,canvas.width,canvas.height);
}

function initLEDs() {
  ledStrip.innerHTML = "";
  for (let i=0;i<NUM_LEDS;i++) {
    const d = document.createElement("div");
    d.className = "led";
    ledStrip.appendChild(d);
  }
}

canvas.addEventListener("mousedown", e=>{
  drawing = true;
  paint(e);
});

canvas.addEventListener("mousemove", e=>{
  if (drawing) paint(e);
});

window.addEventListener("mouseup", ()=> drawing=false);

function paint(e){
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor((e.clientX-rect.left)*canvas.width/rect.width);
  const y = Math.floor((e.clientY-rect.top)*canvas.height/rect.height);

  ctx.fillStyle = color;
  ctx.fillRect(x,y,brush,brush);
}

document.getElementById("color").oninput = e=> color=e.target.value;

document.getElementById("brush").oninput = e=>{
  brush = parseInt(e.target.value);
  document.getElementById("brushValue").textContent = brush;
};

document.getElementById("speed").oninput = e=>{
  document.getElementById("speedValue").textContent = e.target.value;
};

document.getElementById("frameWidth").onchange = e=>{
  let value = parseInt(e.target.value);
  value = Math.max(1, Math.min(150, value));
  e.target.value = value;
  animWidth = value;
  initCanvas();
};

document.getElementById("file").onchange = e=>{
  const file = e.target.files[0];
  const img = new Image();

  img.onload = ()=>{
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
  };

  img.src = URL.createObjectURL(file);
};

function getRGB(){
  const data = ctx.getImageData(0,0,animWidth,animHeight).data;
  const rgb = new Uint8Array(animWidth*animHeight*3);
  let j=0;
  for(let i=0;i<data.length;i+=4){
    rgb[j++]=data[i];
    rgb[j++]=data[i+1];
    rgb[j++]=data[i+2];
  }
  return rgb;
}

function getColumn(col,rgb){
  let arr=[];
  for(let y=0;y<animHeight;y++){
    let i=(y*animWidth+col)*3;
    arr.push({r:rgb[i],g:rgb[i+1],b:rgb[i+2]});
  }
  return arr;
}

function updateLEDs(colors){
  const leds=document.querySelectorAll(".led");
  for(let i=0;i<leds.length;i++){
    const c=colors[i]||{r:0,g:0,b:0};
    leds[i].style.background=`rgb(${c.r},${c.g},${c.b})`;
  }
}

function startPreview(){
  stopPreview();
  const rgb=getRGB();
  const speed=parseInt(document.getElementById("speed").value);

  previewTimer=setInterval(()=>{
    updateLEDs(getColumn(currentCol,rgb));
    currentCol++;
    if(currentCol>=animWidth) currentCol=0;
  },speed);
}

function stopPreview(){
  if(previewTimer) clearInterval(previewTimer);
}

function downloadImage() {
  const link = document.createElement("a");
  link.download = "led-animation.png";
  link.href = canvas.toDataURL("image/png");
  link.click();
}

function clearCanvas(){
  ctx.fillStyle="black";
  ctx.fillRect(0,0,canvas.width,canvas.height);
}

async function uploadAnimation(){

  const rgb = getRGB();
  const speed = parseInt(document.getElementById("speed").value);

  const buffer = new Uint8Array(6 + rgb.length);

  buffer[0] = animWidth & 255;
  buffer[1] = animWidth >> 8;
  buffer[2] = animHeight & 255;
  buffer[3] = animHeight >> 8;
  buffer[4] = speed & 255;
  buffer[5] = speed >> 8;

  buffer.set(rgb, 6);

  const blob = new Blob([buffer], {type: "application/octet-stream"});

  const form = new FormData();
  form.append("file", blob, "anim.bin");

  document.getElementById("status").textContent = "Uploading...";

  try {
    await fetch("/upload", {
      method: "POST",
      body: form
    });

    document.getElementById("status").textContent = "Upload complete";
  } catch (e) {
    document.getElementById("status").textContent = "Upload failed";
  }
}

initCanvas();
initLEDs();
</script>
</body>
</html>
)rawliteral";

extern const char* htmlPage;

void closeAnimFile() {
  if (animFile) animFile.close();
}

bool loadHeader() {
  closeAnimFile();

  animFile = LittleFS.open("/image.bin", FILE_READ);
  if (!animFile) return false;

  if (animFile.size() < 6) return false;

  uint8_t header[6];
  animFile.read(header, 6);

  animWidth  = header[0] | (header[1] << 8);
  animHeight = header[2] | (header[3] << 8);
  speedMs    = header[4] | (header[5] << 8);

  if (animHeight > NUM_LEDS) animHeight = NUM_LEDS;

  size_t dataSize = animWidth * animHeight * 3;

  if (animData) free(animData);
  animData = (uint8_t*)malloc(dataSize);

  if (!animData) {
    Serial.println("RAM allocation failed!");
    return false;
  }

  animFile.read(animData, dataSize);
  animFile.close();

  currentCol = 0;

  Serial.printf("Loaded into RAM: %d x %d\n", animWidth, animHeight);

  return true;
}

bool loadColumn(uint16_t col) {
  if (!animData) return false;

  for (uint16_t y = 0; y < animHeight; y++) {
    uint32_t idx = (y * animWidth + col) * 3;

    columnBuffer[y*3+0] = animData[idx];
    columnBuffer[y*3+1] = animData[idx+1];
    columnBuffer[y*3+2] = animData[idx+2];
  }

  for (uint16_t y = animHeight; y < NUM_LEDS; y++) {
    columnBuffer[y*3+0] = 0;
    columnBuffer[y*3+1] = 0;
    columnBuffer[y*3+2] = 0;
  }

  return true;
}

void drawCurrentBuffer() {

  for (uint16_t y = 0; y < NUM_LEDS; y++) {

    uint8_t r = columnBuffer[y * 3 + 0];
    uint8_t g = columnBuffer[y * 3 + 1];
    uint8_t b = columnBuffer[y * 3 + 2];
    uint8_t w = min(r, min(g, b));
    
    r -= w;
    g -= w;
    b -= w;
    
    strip.SetPixelColor(y, RgbwColor(r, g, b, w));
  }

  strip.Show();
}

void setup() {
  Serial.begin(115200);
  
  strip.Begin();
  strip.Show(); // initialize strip (all off)

  LittleFS.begin(true);
  if (LittleFS.exists("/image.bin")) {
    Serial.println("Found saved animation, loading...");
  
    if (loadHeader()) {
      Serial.println("Animation loaded from flash");
    } else {
      Serial.println("Failed to load animation");
    }
  } else {
    Serial.println("No saved animation found");
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi OK");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage);
  });

  server.on(
    "/upload",
    HTTP_POST,
    [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", "OK");
    },
    [](AsyncWebServerRequest *request,
       String filename,
       size_t index,
       uint8_t *data,
       size_t len,
       bool final){

      if (index == 0) {
        Serial.println("Upload start");

        if (LittleFS.exists("/image.bin"))
          LittleFS.remove("/image.bin");

        imageFile = LittleFS.open("/image.bin", FILE_WRITE);
      }

      if (imageFile) {
        imageFile.write(data, len);
      }

      if (final) {
        Serial.println("Upload done");

        if (imageFile) imageFile.close();

        newAnimationReady = true;
      }
    }
  );

  server.begin();
}

void loop() {
  delay(2);

  if (newAnimationReady) {
    Serial.println(">>> NEW ANIMATION FLAG");
    Serial.println("Loading animation safely...");
    
    bool ok = loadHeader();
    Serial.println(ok ? "Header OK" : "Header FAIL");

    newAnimationReady = false;
  }

  if (!animData) {
    Serial.println("No animData yet");
    delay(1000);
    return;
  }

  unsigned long now = millis();

  if (now - lastUpdate >= speedMs) {

    Serial.println("---- FRAME START ----");

    Serial.print("Col: ");
    Serial.println(currentCol);

    if (!loadColumn(currentCol)) {
      Serial.println("loadColumn FAILED");
      return;
    }

    Serial.println("Column loaded");

    Serial.printf("Pixel0: %d %d %d\n",
      columnBuffer[0],
      columnBuffer[1],
      columnBuffer[2]
    );

    Serial.println("Calling draw...");

    drawCurrentBuffer();

    Serial.println("Draw done");

    currentCol++;
    if (currentCol >= animWidth) currentCol = 0;

    lastUpdate = now;

    Serial.println("---- FRAME END ----");

    delay(2);
  }
}
