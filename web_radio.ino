#include <VS1053.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> 
#include <EEPROM.h>

// Define pin numbers for VS1053
#define VS1053_CS     14
#define VS1053_DCS    33
#define VS1053_DREQ   35

// Define pin numbers for TFT display
#define TFT_CS     15
#define TFT_RST    4
#define TFT_DC     2
#define TFT_MOSI   12
#define TFT_SCK    27
#define TFT_MISO   26

// Define pin numbers for buttons
#define BUTTON_PREVIOUS 21 
#define BUTTON_NEXT 32

// EEPROM settings
#define EEPROM_SIZE 2

// Initialize the TFT display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// Define the default volume level
#define DEFAULT_VOLUME  1

// Initialize the VS1053 player
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);
WiFiClient client;

// Wi-Fi credentials
char ssid[] = "*"; // Your network SSID (name) 
char password[] = "*";   // Your network password

int currentStation = 0;
int previousStation = -1;
const long interval = 1000; 
const int SECONDS_TO_AUTOSAVE = 30;
long seconds = 0;
unsigned long previousMillis = 0; 

// Radio station information
const char* host[] = {
    "31.192.216.10", "mp3.polskieradio.pl", "ic1.smcdn.pl", "ic1.smcdn.pl", 
    "31.192.216.7", "ic1.smcdn.pl", "217.74.72.10", "stream2.technologicznie.net", 
    "194.181.177.253", "ns3021008.ip-151-80-24.eu", "srv0.streamradiowy.eu", "stream.open.fm"
};
const char* path[] = {
    "/RMFFM48", "/;", "/4070-2.aac", "/2070-2.aac", "/RMFMAXXX48", "/3990-2.aac", 
    "/RMFCLASSIC48", "/muzyczne_radio_96.mp3", "/;", "/stream.aac", "/radioalex-aac", "/201"
};
const int httpPort[] = {80, 8908, 80, 80, 80, 80, 80, 80, 8004, 80, 80, 80};

// Buffer for MP3 data
uint8_t mp3Buffer[128]; // Increased buffer size

// Interrupt service routines for buttons
void IRAM_ATTR onPreviousButtonPress() {
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    
    if (interruptTime - lastInterruptTime > 200) {
        currentStation = (currentStation > 0) ? (currentStation - 1) : 11;
    }

    lastInterruptTime = interruptTime;
}

void IRAM_ATTR onNextButtonPress() {
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    
    if (interruptTime - lastInterruptTime > 200) {
        currentStation = (currentStation < 11) ? (currentStation + 1) : 0;
    }

    lastInterruptTime = interruptTime;
}

void setup() {
    Serial.begin(9600);
    delay(3000);

    pinMode(BUTTON_PREVIOUS, INPUT_PULLUP);
    pinMode(BUTTON_NEXT, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(BUTTON_PREVIOUS), onPreviousButtonPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_NEXT), onNextButtonPress, FALLING);

    Serial.println("\n\nSimple Radio Node WiFi Radio");
    SPI.begin();

    player.begin();
    player.loadDefaultVs1053Patches(); 
    player.switchToMp3Mode();
    player.setVolume(0);

    Serial.print("Connecting to SSID ");
    Serial.println(ssid);
    connectToWiFi();

    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("Connecting to station 0");

    connectToStation(0);
    Serial.print("Requesting stream: ");
    sendHttpRequest(0);

    previousStation = -1;  // Ensure this is different from the initial currentStation
}

void loop() {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis > interval) {
        handleStationChange();
        previousMillis = currentMillis;
        Serial.println("loop(): " + String(seconds) + " seconds, Station: " + String(currentStation));
    }

    // Handle reconnection and streaming
    if (!client.connected()) {
        Serial.println("Reconnecting...");
        if (client.connect(host[currentStation], httpPort[currentStation])) {
            sendHttpRequest(currentStation);
        }
    }

    if (client.available() > 0) {
        uint8_t bytesRead = client.read(mp3Buffer, sizeof(mp3Buffer));
        player.playChunk(mp3Buffer, bytesRead);
    }
}

void connectToWiFi() {
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected. IP address: " + WiFi.localIP().toString());
}

void handleStationChange() {
    if (currentStation != previousStation) {
        connectToStation(currentStation);
        previousStation = currentStation;
        seconds = 0;   
        Serial.println("Station changed to: " + String(currentStation));
        
        tft.init(240, 320); 
        tft.fillScreen(ST77XX_BLACK);
        drawStationInfo();

    } else {
        seconds++;
        if (seconds == SECONDS_TO_AUTOSAVE) {
            int savedStation = readStationFromEEPROM();
            if (savedStation != currentStation) {
                Serial.println("loop(): Saving new station to EEPROM");
                writeStationToEEPROM(currentStation);
            }
        }
    }
}

void connectToStation(int stationIndex) {
    Serial.println("Connecting to station: " + String(stationIndex));
    if (client.connect(host[stationIndex], httpPort[stationIndex])) {
        Serial.println("Connected to " + String(host[stationIndex]));
        sendHttpRequest(stationIndex);
    } else {
        Serial.println("Failed to connect to " + String(host[stationIndex]));
    }
}

void sendHttpRequest(int stationIndex) {
    client.print(String("GET ") + path[stationIndex] + " HTTP/1.1\r\n" +
                 "Host: " + host[stationIndex] + "\r\n" +
                 "Connection: close\r\n\r\n");
}

int readStationFromEEPROM() {
    int station;
    byte byteArray[2];
    for (int x = 0; x < 2; x++) {
        byteArray[x] = EEPROM.read(x);    
    }
    memcpy(&station, byteArray, 2);
    Serial.println("readStationFromEEPROM(): " + String(station));
    return station;
}

void drawStationInfo() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setRotation(0);
    tft.setCursor(0, 0);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextWrap(true);
    tft.println("Host: " + String(host[currentStation]));
    tft.println("Path: " + String(path[currentStation]));
    tft.println("Port: " + String(httpPort[currentStation]));
}

void writeStationToEEPROM(int station) {
    byte byteArray[2];
    memcpy(byteArray, &station, 2);
    for (int x = 0; x < 2; x++) {
        EEPROM.write(x, byteArray[x]);
    }  
    EEPROM.commit();
    Serial.println("writeStationToEEPROM(): " + String(station));
}
