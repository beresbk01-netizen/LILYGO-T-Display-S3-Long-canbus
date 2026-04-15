#include "shopping.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* SHOPPING_FILE = "/shopping.json";
static const int   MAX_ITEMS     = 30;
static std::vector<String> g_list;
static bool g_dirty = false;

void loadShoppingList() {
    g_list.clear();
    if (!LittleFS.exists(SHOPPING_FILE)) return;
    File f = LittleFS.open(SHOPPING_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok)
        for (JsonVariant v : doc["items"].as<JsonArray>())
            g_list.push_back(v.as<String>());
    f.close();
}

void saveShoppingList() {
    File f = LittleFS.open(SHOPPING_FILE, "w");
    if (!f) return;
    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const String& s : g_list) arr.add(s);
    serializeJson(doc, f);
    f.close();
}

const std::vector<String>& getShoppingList() { return g_list; }

bool addShoppingItem(const String& item) {
    if ((int)g_list.size() >= MAX_ITEMS) return false;
    String trimmed = item;
    trimmed.trim();
    if (trimmed.isEmpty()) return false;
    g_list.push_back(trimmed);
    saveShoppingList();
    g_dirty = true;
    return true;
}

bool deleteShoppingItem(int index) {
    if (index < 0 || index >= (int)g_list.size()) return false;
    g_list.erase(g_list.begin() + index);
    saveShoppingList();
    g_dirty = true;
    return true;
}

void clearShoppingList() {
    g_list.clear();
    saveShoppingList();
    g_dirty = true;
}

bool isShoppingDirty()  { return g_dirty; }
void clearShoppingDirty() { g_dirty = false; }
