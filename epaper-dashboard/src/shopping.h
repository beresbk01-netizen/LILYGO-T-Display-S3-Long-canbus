#pragma once
#include <Arduino.h>
#include <vector>

void loadShoppingList();
void saveShoppingList();

const std::vector<String>& getShoppingList();
bool addShoppingItem(const String& item);
bool deleteShoppingItem(int index);
void clearShoppingList();

// True after any add/delete/clear until the display has refreshed
bool isShoppingDirty();
void clearShoppingDirty();
