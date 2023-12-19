#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define LED_MATRIX_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define LED_MATRIX_STATE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

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
 * LED_COLOR - 10
 * [
 *   // Color Identifier - 1
 *   00
 *   // Color Common Name - 8
 *    y  e  l l   o  w
 *   79 65 6C 6C 6F 77 00 00
 *   // Empty
 *   00
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
 * GATT
 * - LED_MATRIX_SERVICE
 *  + LED_MATRIX_SIZE - READ
 *    > [ 65, 03, TOTAL_ROWS(uint8_t), TOTAL_COLUMNS(uint8_t) ]
 *    > eg -> Hex: [ 65, 03, 06, 0C ] -> Decimal: [ 41, 3, 6, 12 ]
 *  + LED_CELL_COLORS
 *    > [66, 50, ...LED_COLOR[10][5(types)]]
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
 *  + LED_CELL_STATE - WRITE
 *    > Max Len 24
 *    > [68, ...CELL_STATE[6][n]] -> update cells
 *    > [69] -> reset board
 *
 */

// #define LEDMATRIX_SERVICE_SERVICE_UUID  "E95DD91D251D470AA062FA1922DFA9A8";
// #define LEDMATRIXSTATE_CHARACTERISTIC_UUID  "E95D7B77251D470AA062FA1922DFA9A8";

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

void resetBoard() {
  Serial.println("Resetting Board!");
}



class WriteCellCB: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pChar) {
    Serial.println("onRead");
  }
  void onWrite(BLECharacteristic* pChar) {
    int charLen = pChar->getLength();
    uint8_t* data = pChar->getData();
    uint8_t cellData[] = {data[0], data[1], data[2], data[3], data[4]};
    // memcpy? 
    Serial.printf("onWrite-> cell:%s\n", cellData);
    Serial.printf("onWrite->red:%i\n", data[5]);
    Serial.printf("onWrite->green:%i\n", data[6]);
    Serial.printf("onWrite->blue:%i\n", data[7]);
  }
};

struct CellStruct {
  std::string id;
  int r = 0;
  int g = 0;
  int b = 0;
};

std::map<int, std::string> colors = {
  {0, "blue"},
  {1, "white"},
  {2, "red"},
  {3, "orange"},
  {4, "yellow"}
};

std::map<std::string, int> cells = {
    {"A:1", 0},
    {"A:2", 0},
    {"A:3", 0},
    {"A:4", 0},
    {"A:5", 0},
    {"B:1", 0},
    {"B:2", 0},
    {"B:3", 0},
    {"B:4", 0},
    {"B:5", 0},
    {"C:1", 0},
    {"C:2", 0},
    {"C:3", 0},
    {"C:4", 0},
    {"C:5", 0}
};


// std::map<std::string, uint8_t[]> cells;

// // CellStruct temp = {"A:1"};

// std::map<std::string, int> cells = {
//   {"A:1", 0},
//   {"A:2", 0}
// };

void setup()
{
  // Start serial communication
  Serial.begin(115200);

  // Setup Cell Map
  CellStruct t = {};
  // for(int i = 0; i < sizeof(cellNames) + 1; i++) {
  //   std::string id = cellNames[i];
  //   struct Cell c = { id };
  //   cells[c.id] = c;
  // }

  // Create BLE Device
  BLEDevice::init(bleServerName);

  // Create BLE Server
  pServer = BLEDevice::createServer();

  // Setup connection callbacks
  pServer->setCallbacks(new ServerCB());

  // Initialize service
  BLEService *pService = pServer->createService(LED_MATRIX_SERVICE_UUID);

  // Initialize characteristics
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
      LED_MATRIX_STATE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE);

  pCharacteristic->setValue("Hello World says Neil");
  pCharacteristic->setCallbacks(new WriteCellCB);



  pService->start();

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
  // if (deviceConnected)
  // {
  //   if ((millis() - lastTime) > timerDelay)
  //   {
  //     // Get sensor value
  //     // Serial.println("Getting sensor value");
  //   }
  // }

  delay(2000);
  // int cnt = pServer->getConnectedCount();
  // Serial.printf("Connected Devices: %i\n", cnt);
}
