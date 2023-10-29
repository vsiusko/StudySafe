#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <OneButton.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <RTClib.h> // for DateTime type
#include <UrlEncode.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_CCS811.h>

// Define the size of emulated EEPROM
#define EEPROM_SIZE 512

const uint8_t CLIENT_NUMBER = 0;            // Starts from 0 index
const char* CLIENT_NAME = "ROOM 1";
const char* ap_ssid = "ESP8266_CLIENT";     //Access Point SSID
const char* ap_password = "0123456789";     //Access Point Password
const uint8_t max_connections = 60;          //Maximum Connection Limit for AP
bool connection_defined = false;
String saved_ssid = "";
String saved_pass = "";
String saved_server_ip = "";

const int buttonPin = D3;    // Define the pin for the button
const int ledPin = LED_BUILTIN; // Built-in LED on most ESP8266 boards
OneButton mainButton(
    buttonPin,
    true,               // true - the button is active LOW
    false               // Disable internal pull-up resistor
);

ESP8266WebServer server(80);
HTTPClient http;
WiFiClient client;
// Define NTP Client to get time
WiFiUDP ntpUDP;
const short utcOffsetInSeconds = (1*3600);   // +1 hour UK offset to UTC
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", utcOffsetInSeconds);
unsigned long epochTime = 0;

unsigned long nowMilliSeconds;              // 1 sec == 1000 micros, to count loop events
unsigned long displayStartTime;             // loop counter

const long loopTimer = 60000;              // every 60 sec;

// Create objects for the sensors
Adafruit_BME280 bme280;
Adafruit_CCS811 ccs811;

const int SENSOR_STRING_LENGTH = 6; // Max length for sensor data - 6 symbols - 23.459, 1400.5, etc..

// actual data
struct {
    DateTime time;
    String timeString;

    float bme280_temp = 0;
    float bmp280_press = 0;
    float bme280_hum = 0;

    float ccs811_eco2 = 0;
    float ccs811_tvoc = 0;
} CurrentData;

// AP WEB view
const char* wifiConfigPage = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Wi-Fi Configuration</title>
</head>
<body>
    <h2>Wi-Fi Configuration</h2>
    <form action="/configure" method="post">
        <label for="ssid">SSID:</label><br>
        <input type="text" id="ssid" name="ssid"><br><br>
        <label for="password">Password:</label><br>
        <input type="text" id="password" name="password"><br><br>
        <label for="server_ip">SERVER IP:</label><br>
        <input type="text" id="server_ip" name="server_ip"><br><br>
        <input type="submit" value="Connect">
    </form>
</body>
</html>
)";

void defineWiFi() {
    server.send(200, "text/html", wifiConfigPage);
}

void saveDataAndConnectToWifi()
{
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String server_ip = server.arg("server_ip");

    Serial.println("Received Wi-Fi Configuration:");
    Serial.println("SSID: " + ssid);
    Serial.println("Password: " + password);
    Serial.println("SERVER IP: " + server_ip);

    // Write data to a JSON file
    DynamicJsonDocument
    jsonDoc(512); // virtual file size according to data
    jsonDoc["ssid"] = ssid;
    jsonDoc["pass"] = password;
    jsonDoc["server_ip"] = server_ip;
    // write to file
    File configFile = SPIFFS.open("/config.json", "w");
    if (configFile) {
        serializeJson(jsonDoc, configFile);
        configFile.close();
    } else {
        Serial.println("Failed to open config file for writing");
    }

    // Connect to Wi-Fi
    connectToWifi(ssid, password);
}

void resetSavedData(){    
    // Delete the file if it exists
    if (SPIFFS.exists("/config.json")) {
        SPIFFS.remove("/config.json");
        Serial.println("Config deleted");
    } else {
        Serial.println("Config does not exist");
    }
}

void connectToWifi(String ssid, String password) {
    Serial.print("Connecting to Wi-Fi");

    WiFi.begin(ssid, password);
    int8_t connections = 0;
    while (WiFi.status() != WL_CONNECTED && connections < max_connections) {
        delay(1000);
        Serial.print(".");
        connections++;
    }

    Serial.print("\nConnected to Wi-Fi! ");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (!connection_defined) {
        server.send(200, "text/html", "Connected to Wi-Fi!");
    }
}

void defineTimeClient() {
    timeClient.begin();
    delay(100);
    timeClient.update();
    delay(100);
    setCurrentTime(0);
}

String getTimeString(DateTime time) {
    return String(time.year()) + "/" +
        String(time.month()) + "/" +
        String(time.day()) + " " +
        String(time.hour()) + ":" +
        String(time.minute()) + ":" +
        String(time.second());
}

void setCurrentTime(short incrementMillisecond) {
    if (incrementMillisecond == 0) {
        timeClient.update();
        epochTime = timeClient.getEpochTime();
        CurrentData.time = DateTime(timeClient.getEpochTime());
        CurrentData.timeString = getTimeString(CurrentData.time);
    } else {
        CurrentData.time = DateTime((epochTime + (incrementMillisecond / 1000)));
        CurrentData.timeString = getTimeString(CurrentData.time);
    }
}

void initSensors() {
  // // Initialize BME280 sensor
  if (!bme280.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  // Wait for the sensor ccs811 to be ready
  ccs811.begin(0x5B);
  while (!ccs811.available()) ;
}

void setup() {
    // GENERAL
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);
    Serial.begin(115200);

    // Initialize SPIFFS - file system
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }

    // BUTTONS
    mainButton.attachClick(mainButtonSingleClick);
    mainButton.attachDoubleClick(mainButtonCheck2DoubleClicks);    

    // Initial Connection state before checking saved data
    connection_defined = false;
    // Read Configuration data from the JSON file
    File readFile = SPIFFS.open("/config.json", "r");
    if (readFile) {
        DynamicJsonDocument readJsonDoc(512); // Adjust the size according to the data
        DeserializationError deserializationConfigError = deserializeJson(readJsonDoc, readFile);
        if (!deserializationConfigError) {
            saved_ssid = String(readJsonDoc["ssid"]);
            saved_pass = String(readJsonDoc["pass"]);
            saved_server_ip = String(readJsonDoc["server_ip"]);
            Serial.print("saved_ssid: ");
            Serial.println(saved_ssid);
            Serial.print("saved_pass: ");
            Serial.println(saved_pass);
            Serial.print("server_ip: ");
            Serial.println(saved_server_ip);
            connection_defined = saved_ssid.length() && saved_pass.length() && verifyIPAddress(String(saved_server_ip)) ? true : false;
        } else {
            Serial.println("Failed to parse JSON");
        }
        readFile.close();
    } else {
        Serial.println("Failed to open config file for reading");
    }

    // If no Config, run initial page with a wifi form
    // Setting the AP Mode with SSID, Password, and Max Connection Limit
    if (!connection_defined) {
        if(WiFi.softAP(ap_ssid, ap_password, 1, false, 1) == true) {
            Serial.print("Access Point IP: ");
            Serial.println(WiFi.softAPIP());
            Serial.print("Access Point SSID: ");
            Serial.println(WiFi.softAPSSID());

            WiFi.mode(WIFI_AP);
        } else {
            Serial.println("Unable to Create Access Point");
        }

        // Route for the setup WiFi page
        server.on("/", HTTP_GET, defineWiFi);
        // Route for getting data
        server.on("/configure", HTTP_POST, saveDataAndConnectToWifi);
        // Route to WEB view
        server.begin();
    } else {
        // Connect to Wi-Fi
        connectToWifi(saved_ssid, saved_pass);
        initSensors();
    }
}

// BUTTON ACTIONS
int8_t doubleClickCount = 0;

void mainButtonSingleClick() {
  Serial.println("Single click");
  doubleClickCount = 0;
  digitalWrite(ledPin, !digitalRead(ledPin)); // Toggle the LED state
}

void mainButtonCheck2DoubleClicks() {
  doubleClickCount++;
  Serial.println("Double click");
  if (doubleClickCount == 2) {
    Serial.println("Quad click");
    resetSavedData();       // reset config
    internalLedBlink(4);    // confirm by blinking
    doubleClickCount = 0;   // Reset the click count
  }
}
// BUTTON ACTIONS END

void internalLedBlink(int8_t blinkCounter) {
    for (int8_t i=0; i < blinkCounter; i++) {
        digitalWrite(ledPin, LOW);
        delay(300);
        digitalWrite(ledPin, HIGH);
        delay(300);
    }
}

void getSensorsData () {
  // Read BME280 sensor data
  CurrentData.bme280_temp = bme280.readTemperature() - 3;           // temp correction
  CurrentData.bme280_hum = bme280.readHumidity();
  CurrentData.bmp280_press = bme280.readPressure() / 100.0F; // Convert to hPa
  float temperatureF = (bme280.readTemperature() * 9/5) + 32;

      Serial.print("Temperature: ");
      Serial.print(CurrentData.bme280_temp);
      Serial.println(" °C");

      Serial.print("Temperature (Fahrenheit): ");
      Serial.print(temperatureF);
      Serial.println(" °F");
      
      Serial.print("Humidity: ");
      Serial.print(CurrentData.bme280_hum);
      Serial.println(" %");

  // Read CCS811 sensor data
  if (ccs811.available()) {
    if (!ccs811.readData()) {
      CurrentData.ccs811_eco2 = ccs811.geteCO2();
      CurrentData.ccs811_tvoc = ccs811.getTVOC();

      Serial.print("Pressure: ");
      Serial.print(CurrentData.bmp280_press);
      Serial.println(" hPa");

      Serial.print("eCO2: ");
      Serial.print(CurrentData.ccs811_eco2);
      Serial.println(" ppm");

      Serial.print("TVOC: ");
      Serial.print(CurrentData.ccs811_tvoc);
      Serial.println(" ppb");

      Serial.println();


    } else {
      Serial.println("CCS811 read error!");
    }
  }

}

void sendData (String server_address) {
    if (!verifyIPAddress(server_address)) {
        Serial.println("Invalid IP: ");
        Serial.print(server_address);
        return;
    }

    String url = "http://";
    url += server_address;
    url += "/data?client_number=";
    url += CLIENT_NUMBER;
    url += "&client_name=";
    url += urlEncode(CLIENT_NAME).substring(0, 10);
    url += "&bme280_temp=";
    url += urlEncode(String(CurrentData.bme280_temp));
    url += "&bmp280_press=";
    url += urlEncode(String(CurrentData.bmp280_press));
    url += "&bme280_hum=";
    url += urlEncode(String(CurrentData.bme280_hum));
    url += "&ccs811_eco2=";
    url += urlEncode(String(CurrentData.ccs811_eco2));
    url += "&ccs811_tvoc=";
    url += urlEncode(String(CurrentData.ccs811_tvoc));

    Serial.print("Sending data to MAIN_SERVER: ");
    Serial.println(url);

    http.begin(client, url);
    internalLedBlink(2);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Server response: ");
      Serial.println(response);
    } else {
      Serial.print("Error: ");
      Serial.println(httpResponseCode);
    }

    http.end();
}

bool verifyIPAddress(String ipAddress) {
  int parts[4];  // To store the 4 parts of the IP address

  // Convert String to a C-style string (char*)
  char ipCharArray[ipAddress.length() + 1];
  ipAddress.toCharArray(ipCharArray, sizeof(ipCharArray));

  if (sscanf(ipCharArray, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]) == 4) {
    for (int i = 0; i < 4; i++) {
      if (parts[i] < 0 || parts[i] > 255) {
        return false;
      }
    }
    return true;
  }
  return false;
}

void loop() {
    nowMilliSeconds = millis();
    mainButton.tick();              // Call the tick() function to update the button state

    server.handleClient();          // run server in loop

    if (
        connection_defined
        && ((nowMilliSeconds - displayStartTime) > loopTimer)   // compare time diff from prev loop and current
    ) {
        setCurrentTime(nowMilliSeconds);
        getSensorsData();
        sendData(saved_server_ip);

        displayStartTime = millis();
    }
}
