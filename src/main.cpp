#include <Arduino.h>
#include <bitset>
#include <iostream>
#include <sstream>
#include <string>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Maybe there should be another service to add a ble status light?

#define LED_MATRIX_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

#define LED_MATRIX_STATE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define LED_CELL_STATE_CHARACTERISTIC_UUID "E95DD91D-251D-470A-A062-FA1922DFA9A8"
#define LED_MATRIX_SIZE_CHARACTERISTIC_UUID "E95D7B77-251D-470A-A062-FA1922DFA9A8"

// Not Implemented
#define LED_CELL_COLORS_CHARACTERISTIC_UUID "9E9BBF6C-AC45-488C-9AE2-F0074B089AE2"

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
  * uint8_t[] -> hex
    - Shouldn't be hard. I'm just inexperienced
  * Remove Serial references
  * LED Matrix support
  * Resetting Board Not Implemented
    - Needs to be more thought out as well. Not 100% sure if it should be part of cell state characteristic
  * Get Colors Not Implemented
  * Need to add a row/column concept
    - How to connect int cell value -> R:C -> cell state?
*/

#define bleServerName "Bayer Sample Buddy"

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

// FIXME: This probably doesn't need to be a map. Just a use the index and the identifier;
//  - Also, the words are not important to us. We'll need an uint8[3] -> [ red, green, blue ]
//  - Also, maybe we need 4 values so we can also control the brightness, but that can be a stretch goal.
std::map<int, std::string> colors = {
    {0, "off"},
    {1, "blue"},
    {2, "red"},
    {3, "orange"}};


// int cls[16] = {3, 3, 3, 3, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2};

// FIXME: This should be better. Maybe it should also be an int[cell.len]
std::map<int, int> cells = {
    {0, 0},
    {1, 1},
    {2, 2},
    {3, 3},
    {4, 0},
    {5, 1},
    {6, 2},
    {7, 3},
    {8, 0},
    {9, 1},
    {10, 2},
    {11, 3},
    {12, 0},
    {13, 1},
    {14, 2},
    {15, 3},
};

// FIXME: Not connected to anything yet and not actually doing anything either.
void resetBoard()
{
  Serial.println("Resetting Board!");
}

// FIXME: I'm not sure this is working. It's return the correct hexString, but we may need to do something to get the hex[] out of it.
bool to_hex(char *dest, size_t dest_len, const uint8_t *values, size_t val_len)
{
  if (dest_len < (val_len * 2 + 1)) /* check that dest is large enough */
    return false;
  *dest = '\0'; /* in case val_len==0 */
  while (val_len--)
  {
    /* sprintf directly to where dest points */
    sprintf(dest, "%02X", *values);
    dest += 2;
    ++values;
  }
  return true;
}

// MARK: Characteristic Declarations
BLECharacteristic *pWriteCellCharacteristic;
BLECharacteristic *pReadMatrixCharacteristic;
BLECharacteristic *pReadMatrixSizeCharacteristic;

void writeMatrixSizeCharacteristic()
{
  uint8_t v = cells.size();
  pReadMatrixCharacteristic->setValue(&v, 1);
};

// Kinda working.
void writeMatrixCharacteristic()
{
  std::map<int, int>::iterator itr;
  int totalCells = cells.size();
  Serial.printf("totalCells: %d\n", totalCells);
  int totalBytes = (totalCells * 2) / 8;
  uint8_t out[totalBytes];
  for (itr = cells.begin(); itr != cells.end(); itr++)
  {
    int key = itr->first;
    int value = itr->second;
    int idx = (key * 2) / 8;

    out[idx] |= value << (6 - ((key % 4) * 2));
  }

  // Write matrix to read property
  uint8_t *data = out;
  size_t len = sizeof(out);
  // FIXME: This setValue call isn't producing the value I expect in the client. How do we test this?
  pReadMatrixCharacteristic->setValue(out, len);
  
  // Logging Help
  // int bufSize = sizeof(out) * 2 + 1;
  // char buf[bufSize]; /* one extra for \0 */
  // if (to_hex(buf, sizeof(buf), out, sizeof(out)))
  // {
  //   Serial.printf("%s\n", buf);
  // }
};



class WriteCellCB : public BLECharacteristicCallbacks
{
  // TODO: Remove
  void onRead(BLECharacteristic *pChar)
  {
    Serial.println("onRead");
  }

  // Handles incoming writes
  void onWrite(BLECharacteristic *pChar)
  {
    // int charLen = pChar->getLength();
    uint8_t *data = pChar->getData();

    // Get cell to update
    int cellUpdate = data[0];
    // Get cell state
    int color = data[1];

    std::map<int, int>::iterator it = cells.find(cellUpdate);
    // if cell and color exists, then set value in cell map
    if (it != cells.end() && colors.find(color) != colors.end())
    {
      Serial.printf("Found cell: %i\n", cellUpdate);
      Serial.printf("Found color: %i\n", color);
      it->second = color;
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
  
  // Populate Matrix Characteristic
  writeMatrixCharacteristic();

  // LED_MATRIX_SIZE_CHARACTERISTIC_UUID
  pReadMatrixSizeCharacteristic = pService->createCharacteristic(
    LED_MATRIX_SIZE_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ);
  
  // Populate Matrix Size Characteristic
  writeMatrixSizeCharacteristic();

  // TODO: Initialize LED Matrix

  pService->start();

  // FIXME: This should be more thought out
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(LED_MATRIX_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  Serial.println("Characteristic defined! Now you can read it in your phone!");
}

void loop()
{
  // TODO: Set LED Matrix

  delay(2000);
}
