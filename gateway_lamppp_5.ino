#include <painlessMesh.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "soc/rtc_cntl_reg.h"

#define MESH_PREFIX "XirkaMesh"
#define MESH_PASSWORD "12345678"
#define MESH_PORT 5555

uint32_t nodeNumber = 1;
uint32_t myLineNumber = 2;
int receivedLineNumber;
char buffValue[100];
StaticJsonDocument<200> doc;
StaticJsonDocument<200> newDoc;
StaticJsonDocument<200> controlLedDoc;
StaticJsonDocument<200> dataForGatewayDoc;

unsigned long t1;
unsigned long tl;
uint32_t newNodes;
bool flag = 0;

Scheduler userScheduler;
painlessMesh mesh;

const int MAX_NODES_DIR = 1;
uint32_t nodeDir[MAX_NODES_DIR] = {};
int jumlahNodeDir = 0;

void ledSetup() {
  uint8_t msg = 1;
  controlLedDoc["LAMP"] = msg;
  String jsonLedOn;
  serializeJson(controlLedDoc, jsonLedOn);
  Serial2.println(jsonLedOn);
}

//function for receive data from microcontroller and send data via mesh
void receiveSerial2() {
  while (Serial2.available() > 0) {
    static uint8_t x = 0;
    buffValue[x] = (char)Serial2.read();
    x += 1;
    if (buffValue[x - 1] == '\n') {
      DeserializationError error = deserializeJson(doc, buffValue);
      if (!error) {
        float vsolar = doc["VSOLAR"];
        float vbat = doc["VBAT"];

        newDoc["Node"] = nodeNumber;
        newDoc["Line"] = myLineNumber;
        newDoc["VSOLAR"] = round(vsolar * 100.0) / 100.0;
        newDoc["VBAT"] = round(vbat * 100.0) / 100.0;  // Round vbat to two decimal places
        newDoc["LAMP"] = (controlLedDoc["LAMP"] == 1) ? "ON" : "OFF";

        String jsonString;
        serializeJson(newDoc, jsonString);
        // mesh.sendSingle(newNodes, jsonString);
        Serial.println("Data: " + jsonString);
      }
      x = 0;
      memset(buffValue, 0, sizeof(buffValue));
      break;
    }
  }
}

void sendMessage() {
  String msg = "a";
  dataForGatewayDoc["Line"] = myLineNumber;
  dataForGatewayDoc["msg"] = msg;
  String jsonLineString;
  serializeJson(dataForGatewayDoc, jsonLineString);
  mesh.sendSingle(newNodes, jsonLineString);
  // Serial.println(jsonLineString);
}

void checkConnectionStatus() {
  if (!mesh.isConnected(nodeDir[MAX_NODES_DIR - 1])) {
    Serial.println("Node Direct disconnected.");
    jumlahNodeDir = 0;
    Serial.printf("Amount of direct nodes: %d\n", jumlahNodeDir);
  }
}

void saveNode() {
  if (jumlahNodeDir < MAX_NODES_DIR) {
    nodeDir[jumlahNodeDir++] = newNodes;
    Serial.printf("number of connected direct nodes: %d\tNode Direct: %d\n", jumlahNodeDir, nodeDir[MAX_NODES_DIR - 1]);
  } else {
    Serial.print("direct node capacity is full.");
  }
}

void checkLineNumber() {
  if (receivedLineNumber != myLineNumber) {
    Serial.println("Line number not match");
    updateConnection();
  } else {
    Serial.println("Line number is match");
  }
}

void updateConnection() {
  if (jumlahNodeDir > 0) {
    jumlahNodeDir--;`
    Serial.println("Reduced node direct by one!");
    Serial.printf("Amount of direct nodes: %d\n", jumlahNodeDir);
  } else {
    Serial.println("No node direct to reduce!");
  }
}

bool lineChecked = false;
bool disconnectedNodeMessagePrinted = false;
void receivedCallback(uint32_t from, String& msg) {
  bool isSenderConnected = false;
  for (int i = 0; i < jumlahNodeDir; i++) {
    if (from == nodeDir[i]) {
      isSenderConnected = true;
      break;
    }
  }

  if (!isSenderConnected) {
    if (!disconnectedNodeMessagePrinted) {
      Serial.printf("Ignoring message from disconnected node: %u\n", from);
      disconnectedNodeMessagePrinted = true;
    }
    return;
    disconnectedNodeMessagePrinted = false;
  }
  Serial.printf("received from %u msg=%s\n", from, msg.c_str());

  StaticJsonDocument<200> receivedJsonDoc;
  DeserializationError error = deserializeJson(receivedJsonDoc, msg);
  if (!error && receivedJsonDoc.containsKey("Line")) {
    receivedLineNumber = receivedJsonDoc["Line"];
    // Serial.printf("Received Line Number: %d\n", receivedLineNumber);
  } else {
    // Serial.println("Failed to parse received JSON or missing line number.");
  }
  if (!lineChecked) {
    checkLineNumber();
    lineChecked = true;
  }
  return;
  lineChecked = false;
  sendMessage();
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("New Connection, nodeId = %u\n", nodeId);
  newNodes = nodeId;
  saveNode();
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
  checkConnectionStatus();
  // String route = mesh.subConnectionJson();
  // Serial.println("connection :" + route);
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  ledSetup();
  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD);
  mesh.setRoot(true);
  mesh.setContainsRoot(true);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
}

void loop() {
  mesh.update();
  userScheduler.execute();

  if (millis() - t1 > 1000) {
    receiveSerial2();
    t1 = millis();
  }

  if (millis() - tl > 1000) {
    sendMessage();
    tl = millis();
  }
}