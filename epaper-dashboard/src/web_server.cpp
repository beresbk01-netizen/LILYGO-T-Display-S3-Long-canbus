#include "web_server.h"
#include "shopping.h"
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(80);

// ── Embedded HTML (mobile-friendly) ────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Shopping List</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
         background: #f5f5f5; max-width: 500px; margin: 0 auto; padding: 16px; }
  h1 { font-size: 1.5rem; margin-bottom: 16px; color: #333; }
  #add-form { display: flex; gap: 8px; margin-bottom: 20px; }
  #add-form input { flex: 1; padding: 10px 14px; border: 1px solid #ccc;
                    border-radius: 8px; font-size: 1rem; }
  #add-form button { padding: 10px 18px; background: #2196F3; color: #fff;
                     border: none; border-radius: 8px; font-size: 1rem; cursor: pointer; }
  #add-form button:active { background: #1565C0; }
  #list { list-style: none; }
  #list li { display: flex; align-items: center; background: #fff;
             border-radius: 8px; margin-bottom: 8px; padding: 12px 14px;
             box-shadow: 0 1px 3px rgba(0,0,0,0.1); gap: 12px; }
  #list li span { flex: 1; font-size: 1rem; color: #222; }
  #list li button { background: none; border: none; color: #e53935;
                    font-size: 1.3rem; cursor: pointer; padding: 0 4px; }
  #clear-btn { width: 100%; padding: 10px; background: #ff5252; color: #fff;
               border: none; border-radius: 8px; font-size: 1rem;
               cursor: pointer; margin-top: 8px; display: none; }
  #status { font-size: 0.85rem; color: #666; margin-top: 12px; text-align: center; }
</style>
</head>
<body>
<h1>Shopping List</h1>
<form id="add-form" onsubmit="addItem(event)">
  <input id="new-item" type="text" placeholder="Add item…" autocomplete="off">
  <button type="submit">Add</button>
</form>
<ul id="list"></ul>
<button id="clear-btn" onclick="clearAll()">Clear all</button>
<p id="status"></p>

<script>
async function loadList() {
  const res = await fetch('/api/list');
  const data = await res.json();
  const ul = document.getElementById('list');
  ul.innerHTML = '';
  data.items.forEach((item, i) => {
    const li = document.createElement('li');
    li.innerHTML = `<span>${escHtml(item)}</span><button onclick="deleteItem(${i})">&#x2715;</button>`;
    ul.appendChild(li);
  });
  document.getElementById('clear-btn').style.display = data.items.length ? 'block' : 'none';
}

async function addItem(e) {
  e.preventDefault();
  const inp = document.getElementById('new-item');
  const text = inp.value.trim();
  if (!text) return;
  const res = await fetch('/api/add', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({item: text})
  });
  const d = await res.json();
  if (d.ok) { inp.value = ''; loadList(); setStatus('Added!'); }
  else setStatus(d.error || 'Error');
}

async function deleteItem(i) {
  await fetch('/api/delete', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({index: i})
  });
  loadList();
}

async function clearAll() {
  if (!confirm('Clear all items?')) return;
  await fetch('/api/clear', { method: 'POST' });
  loadList();
}

function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function setStatus(msg) {
  const el = document.getElementById('status');
  el.textContent = msg;
  setTimeout(() => el.textContent = '', 2000);
}

loadList();
</script>
</body>
</html>
)rawhtml";

// ── Request handlers ─────────────────────────────────────────────────────────
static void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleApiList() {
    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const String& s : getShoppingList()) arr.add(s);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

static void handleApiAdd() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}"); return;
    }
    String item = doc["item"].as<String>();
    if (addShoppingItem(item)) {
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"list full or empty\"}");
    }
}

static void handleApiDelete() {
    if (!server.hasArg("plain")) { server.send(400); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) { server.send(400); return; }
    int idx = doc["index"].as<int>();
    deleteShoppingItem(idx);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiClear() {
    clearShoppingList();
    server.send(200, "application/json", "{\"ok\":true}");
}

void startWebServer() {
    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/api/list",   HTTP_GET,  handleApiList);
    server.on("/api/add",    HTTP_POST, handleApiAdd);
    server.on("/api/delete", HTTP_POST, handleApiDelete);
    server.on("/api/clear",  HTTP_POST, handleApiClear);
    server.begin();
}

void handleWebServer() {
    server.handleClient();
}
