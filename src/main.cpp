#include <Arduino.h>
#include <bitset>
#include <iostream>
#include <sstream>
#include <string>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "Adafruit_NeoPixel.h"

// Maybe there should be another service to add a ble status light?

/**
 * Stuctures
 * Message Structure
 * [
 *  // Message Identifier - uint8_t
 *  MESSAGE_ID,
 *  // Message Body Len - uint16_t
 *  MESSAGE_LEN_1, MESSAGE_LEN_2,
 *  // Message Body
 * ...MESSAGE(eg Cell State)
 * ]
 *
 * LED_COLOR - 8
 * [
 *   // Color Common Name - 8
 *    y  e  l l   o  w
 *   79 65 6C 6C 6F 77 00 00
 * ]
 *
 * LED_CELL_COLORS
 * [
 *  // Message Id
 *  66,
 *  // Message Len - 5 types * 10 LED_COLOR length
 *  50,
 *  ...LED_COLORs
 * ]
 *
 * CELL_STATE - Length: 8
 * v1 - Len: 8
 * [
 *  // Five bytes to hold "cell" name - ASCII - string[5]
 *   H  :  5
 *  48 3a 35 00 00
 *  // RGB Values 0-255 - uint8_t[3]
 *  RED GREEN BLUE
 *  FF  00   00
 * ]
 *
 * v2 - Len: 6
 * [
 *  // Five bytes to hold "cell" name - ASCII - string[5]
 *   H  :  5
 *  48 3a 35 00 00
 *  // LED_COLOR_ID 0-LED_COLORS.len - uint8_t
 *  orange
 *    3
 * ]
 *
 * v3 - Len: 1/4
 * Binary Rep
 * 0 = 00 / 1 = 01 / 2 = 10 / 3 = 11
 * Hex Rep for 4 cell states
 *       0  1  2  3
 * 1B = 00 01 10 11
 * the consuming application will have to extract 2 bits in cell order to get the cell value.
 * 
 * GATT - Not totally accurate. Just some thoughts. This needs to be actually documented.
 * - LED_MATRIX_SERVICE
 *  + LED_MATRIX_SIZE - READ
 *    > [ 65, 03, TOTAL_ROWS(uint8_t), TOTAL_COLUMNS(uint8_t) ]
 *    > eg -> Hex: [ 65, 03, 06, 0C ] -> Decimal: [ 41, 3, 6, 12 ]
 *  + LED_CELL_COLORS - READ
 *    > [...LED_COLOR[8][4(types)]]
 *  + LED_MATRIX_STATE - READ
 *    > v1
 *    > Fx12=72cells x 8 bytes=576 bytes
 *      * Max packet Tx 20 bytes/~20ms
 *      * 28.8 packets * 20ms = 0.576second
 *    > [67, 0C, ...CELL_STATE[8][n]]
 *    > v2
 *    > Fx12=72cells x 6 bytes =432 bytes
 *      * Max packet Tx 20 bytes/~20ms
 *      * 21.6 packets * 20ms = 0.432second
 *    > v3
 *      * 
 *  + LED_CELL_STATE - WRITE
 *    > Max Len 24
 *    > [68, ...CELL_STATE[6][n]] -> update cells
 *    > [69] -> reset board
 *
 */


/*
  Not Working:
  * Remove Serial references
  * LED Matrix support
  * Resetting Board Not Implemented in BLE
  * Get Colors Not Implemented
  * Need to add a row/column concept
    - How to connect int cell value -> R:C -> cell state?
*/
#define bleServerName "Bayer Sample Buddy"

#define LED_MATRIX_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

#define LED_MATRIX_STATE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define LED_CELL_STATE_CHARACTERISTIC_UUID "E95DD91D-251D-470A-A062-FA1922DFA9A8"
#define LED_MATRIX_SIZE_CHARACTERISTIC_UUID "E95D7B77-251D-470A-A062-FA1922DFA9A8"

// Not Implemented
#define LED_CELL_COLORS_CHARACTERISTIC_UUID "9E9BBF6C-AC45-488C-9AE2-F0074B089AE2"

// Pixel Array
#define PIN 5
#define SIZE 96

Adafruit_NeoPixel strip = Adafruit_NeoPixel(SIZE, PIN, NEO_GRB + NEO_KHZ800);
BLEServer *pServer;

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;

bool deviceConnected = false;

class ServerCB : public BLEServerCallbacks
{
  void onConnect(BLEServer *svr)
  {
    Serial.println("OnConnect");
    deviceConnected = true;
  }
  void onMtuChanged()
  {
    Serial.println("onMTUChanged");
  }
  void onDisconnect(BLEServer *svr)
  {
    deviceConnected = false;
    Serial.println("OnDisconnect");
    pServer->startAdvertising();
  }
};

int cells[SIZE];
uint32_t colors[4] = {
  0,
  // BLUE
  0xFF000000 | 255 | (0 << 16) | (0 << 8),
  // RED
  0xFF000000 | 0 | (255 << 16) | (0 << 8),
  // AMBER
  0xFF000000 | 0 | (255 << 16) | (100 << 8)
};

// MARK: Characteristic Declarations
BLECharacteristic *pWriteCellCharacteristic;
BLECharacteristic *pReadMatrixCharacteristic;
BLECharacteristic *pReadMatrixSizeCharacteristic;

void writeMatrixSizeCharacteristic()
{
  uint8_t v = SIZE;
  pReadMatrixSizeCharacteristic->setValue(&v, 1);
};

// Kinda working.
void writeMatrixCharacteristic()
{
  int totalBytes = (SIZE * 2) / 8;
  uint8_t out[totalBytes];
  std::fill_n(out, totalBytes, 0);
  for (int i = 0; i < SIZE; i++) {
    out[(i * 2) / 8] |= cells[i] << (6 - ((i % 4) * 2));
    strip.setPixelColor(i, colors[cells[i]]);
  }
  // Write matrix to read property
  pReadMatrixCharacteristic->setValue(out, sizeof(out));
  strip.show();
};

// FIXME: Not connected to anything yet and not actually doing anything either.
void resetBoard()
{
  Serial.println("Resetting Board!");
  std::fill_n(cells, SIZE, 0);
  writeMatrixCharacteristic();
}

class WriteCellCB : public BLECharacteristicCallbacks
{
  // Handles incoming writes
  void onWrite(BLECharacteristic *pChar)
  {
    uint8_t *data = pChar->getData();

    // Get cell to update
    int cellUpdate = data[0];
    // Get cell state
    int color = data[1];


    if (sizeof(cells) >= cellUpdate)
    {
      cells[cellUpdate] = color;
    }
    writeMatrixCharacteristic();
  }
};

void setup()
{
  // Start serial communication
  Serial.begin(115200);

  // Create BLE Device
  BLEDevice::init(bleServerName);

  // Create BLE Server
  pServer = BLEDevice::createServer();
  // Setup connection callbacks
  pServer->setCallbacks(new ServerCB());
  
  // Initialize service
  BLEService *pService = pServer->createService(LED_MATRIX_SERVICE_UUID);

  // Initialize characteristics
  pWriteCellCharacteristic = pService->createCharacteristic(
      LED_MATRIX_STATE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pWriteCellCharacteristic->setCallbacks(new WriteCellCB);

  // Initialize Matrix State Characteristics
  pReadMatrixCharacteristic = pService->createCharacteristic(
      LED_MATRIX_STATE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ);
  
  // LED_MATRIX_SIZE_CHARACTERISTIC_UUID
  pReadMatrixSizeCharacteristic = pService->createCharacteristic(
    LED_MATRIX_SIZE_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ);
  
  // Initialize LED Matrix
  strip.begin();
  strip.setBrightness(50);

  // Populate Matrix Characteristic
  writeMatrixCharacteristic();
  // Populate Matrix Size Characteristic
  writeMatrixSizeCharacteristic();

  pService->start();

  // FIXME: This should be more thought out
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(LED_MATRIX_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  Serial.println("Ready");
}


void loop()
{
  delay(2000);
}