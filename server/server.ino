#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Arduino_JSON.h>
#include <OneButton.h>
#include <Adafruit_SSD1306.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <RTClib.h>
#include <Arduino.h>

// Define the size of emulated EEPROM
// #define EEPROM_SIZE 512
// 128x32 displays init:
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1            // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C         // 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* ap_ssid = "ESP32_SERVER";               //Access Point SSID
const char* ap_password = "0123456789";             //Access Point Password
uint8_t max_connections = 60;                       //Maximum Connection Limit for AP
bool connection_defined = false;
String saved_ssid = "";
String saved_pass = "";

int8_t selected_client = 0;                         // Selected by Web view Client, default = 0
String web_periods[] = {"day", "week", "month"};
int8_t selected_period_id = 0;                      // Selected by Web view period range, default = 0
String web_x_legends[] = {"Hour", "Week Day", "Day"};
int8_t selected_x_legend = 0;                       // Selected by Web view legend label, default = 0

const int buttonPin = 04;                           // Define the pin for the button
const int ledPin = 2;                               // Built-in LED on most ESP32 boards 2 pin
OneButton mainButton(
    buttonPin,
    true,                                           // true - the button is active LOW
    false                                           // Disable internal pull-up resistor
);

WebServer server(80);

unsigned long nowMilliSeconds;                      // 1 sec == 1000 micros, to count loop events
const long webViewIterationTimer = 60 * 1000;       // every 1 min;
unsigned long webViewLoopTimer;                     // day loop counter
const long dayIterationTimer = 15 * 60 * 1000;      // save every 15 min;
unsigned long dayLoopTimer;                         // day loop counter
int weekSaveCounter = 4;                            // save week data every 1 hour, every fourth day iteration
int monthSaveCounter = 16;                          // save month data every 4 hour, every 16th  day iteration
const long utcOffsetInSeconds = (1*3600);           // +1 hour UK offset to UTC
// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", utcOffsetInSeconds);

unsigned long epochTime = 0;

const int MAX_CLIENTS = 1;                          // Maximum number of clients
const int TIME_STRING_LENGTH = 20;                  // Max length for sensor data - 6 symbols - 23.459, 1400.5, etc..
const int SENSOR_STRING_LENGTH = 6;                 // Max length for sensor data - 6 symbols - 23.459, 1400.5, etc..
const int DAY_DATA_POINTS = 96;                     // Day data contains data about each 15 mins - 96 iterations
const int WEEK_DATA_POINTS = 168;                   // Week data contains data about each 60 mins - 168 iterations
const int MONTH_DATA_POINTS = 186;                  // Month data contains data about each 240 hour - 186 iterations

// Client Names data structure
struct ClientNameStructure {
    String client_name;
};

ClientNameStructure ClientNames[MAX_CLIENTS];

// Time data structure
struct {
    DateTime time;
    String timeString;
} CurrentTime;

// Sensor data structure
struct SensorDataStructure {
    String bme280_temp;
    String bmp280_press;
    String bme280_hum;
    String ccs811_eco2;
    String ccs811_tvoc;
};

String time_String = "";
String bme280_temp_String = "";
String bmp280_press_String = "";
String bme280_hum_String = "";
String ccs811_eco2_String = "";
String ccs811_tvoc_String = "";


struct PeriodDataStructure {
    String TimeString;
    SensorDataStructure SensorData[MAX_CLIENTS];
};

// real time data
SensorDataStructure SensorData[MAX_CLIENTS];

// Day data Array
PeriodDataStructure DayData[DAY_DATA_POINTS];

// Week data Array
PeriodDataStructure WeekData[WEEK_DATA_POINTS];

// Month data Array
PeriodDataStructure MonthData[MONTH_DATA_POINTS];

const char* wifiConfigPage = R"(
    <!DOCTYPE html>
    <html>
        <head>
            <title>Wi-Fi Configuration</title>
        </head>
        <body>
            <h2>Wi-Fi Configuration</h2>
            <form action="/configure" methwifiWEBViewod="post">
                <label for="ssid">SSID:</label><br>
            <input type="text" id="ssid" name="ssid"><br><br>
                <label for="password">Password:</label><br>
            <input type="text" id="password" name="password"><br><br>
            <input type="submit" value="Connect">
            </form>
        </body>
    </html>
)";

const char* wifiWEBView = R"(
    <!DOCTYPE html>
    <html lang="">
        <head>
            <title>School weather station</title>
            <style>
                /* Style for the chart container */
                .chart-container {
                    width: 800px;
                    height: 500px;
                    display: inline-block;
                    margin: 20px;
                    margin-top: 0px;
                }
                .general-data {
                    width: 800px;
                    height: auto;
                    display: inline-block;
                    margin: 20px;
                    top: -50px;
                    left: 100px;
                    position: relative;
                }
                .param-label {
                    color: #555;
                    position: relative;
                    left: 300px;
                    top: 38px;
                    font-size: 24px;
                    margin: 0;
                    width: 200px;
                }
                .x-legend-label {
                    color: #555;
                    position: relative;
                    left: 370px;
                    top: 5px;
                    font-size: 20px;
                    margin: 0;
                    width: 200px;
                }
                .temp-sign {
                    position: relative;
                    display: inline-block;
                    margin: 0;
                    width: 50px;
                }
                #nowTemp {
                    display: inline;
                }
                h3 {
                    color: darkgreen;
                    font-weight: 800;
                }
                form {
                    position: relative;
                    display: inline-block;
                }
                input {
                    position: relative;
                    width: 150px;
                    margin: 5px 10px 5px 0;
                }
            </style>
        </head>
        <body>
            <div class="general-data">
                <h1>@clientName</h1>
                <h2 id="nowTime"></h2>
                <h3 id="nowTemp"></h3><h3 class="temp-sign"> &#8451;</h3>
                <h3 id="nowHum"></h3>
                <h3 id="nowPres"></h3>
                <h3 id="nowCO2"></h3>
                <h3 id="nowTVOC"></h3>
                <form action="/data?client=@clientNumber&period=0" methwifiWEBViewod="post">
                    <input type="submit" value="Day Data">
                </form>
                <form action="/data?client=@clientNumber&period=1" methwifiWEBViewod="post">
                    <input type="submit" value="Week Data">
                </form>
                <form action="/data?client=@clientNumber&period=2" methwifiWEBViewod="post">
                    <input type="submit" value="Month Data">
                </form>
                <br>
                <br>
                <form action="/data?client=0&period=@selectedPeriod" methwifiWEBViewod="post">
                    <input type="submit" value="Room 1">
                </form>
                <form action="/data?client=1&period=@selectedPeriod" methwifiWEBViewod="post">
                    <input type="submit" value="Room 2" disabled="true">
                </form>
                <form action="/data?client=2&period=@selectedPeriod" methwifiWEBViewod="post">
                    <input type="submit" value="Room 3" disabled="true">
                </form>
            </div>
            <div class="chart-container">
                <h4 class="param-label">Temperature</h4>
                <canvas id="temperatureChart" class="chart-line"></canvas>
                <h6 class="x-legend-label">@xLegendLabel</h6>
            </div>
            <div class="chart-container">
                <h4 class="param-label">Humidity</h4>
                <canvas id="humidityChart" class="chart-line"></canvas>
                <h6 class="x-legend-label">@xLegendLabel</h6>
            </div>
            <div class="chart-container">
                <h4 class="param-label">Pressure</h4>
                <canvas id="pressureChart" class="chart-line"></canvas>
                <h6 class="x-legend-label">@xLegendLabel</h6>
            </div>
            <div class="chart-container">
                <h4 class="param-label">CO2</h4>
                <canvas id="co2Chart" class="chart-line"></canvas>
                <h6 class="x-legend-label">@xLegendLabel</h6>
            </div>
            <div class="chart-container">
                <h4 class="param-label">TVOC</h4>
                <canvas id="tvocChart" class="chart-line"></canvas>
                <h6 class="x-legend-label">@xLegendLabel</h6>
            </div>
            <script>
                const timeData = @timeData;
                let temperatureData = @temperatureData;
                temperatureData = temperatureData.map(n => parseFloat(n) || 0);
                let humidityData = @humidityData;
                humidityData = humidityData.map(n => parseFloat(n) || 0);
                let pressureData = @pressureData;
                pressureData = pressureData.map(n => parseFloat(n) || 0);
                let co2Data = @co2Data;
                co2Data = co2Data.map(n => parseFloat(n) || 0);
                let tvocData = @tvocData;
                tvocData = tvocData.map(n => parseFloat(n) || 0);

                let timeDayArray = timeData.filter((t, i) => i % 8 === 0);
                timeDayArray.push(timeData[timeData.length-1])
                timeDayArray = timeDayArray.map(t=> (new Date(t)).getHours());

                const weekday = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"];
                let timeWeekArray = timeData.filter((d, i) => i % 16 === 0);
                timeWeekArray.push(timeData[timeData.length-1])
                timeWeekArray = timeWeekArray.map(d=> weekday[(new Date(d)).getDay()]);

                let timeMonthArray = timeData.filter((d, i) => i % 16 === 0);
                timeMonthArray.push(timeData[timeData.length-1])
                timeMonthArray = timeMonthArray.map(d=> (new Date(d)).getDate());

                function drawCart(dataArray, timeDataArray, suffix, xDivider, yDivider, chartId) {
                    // Calculate the minimum and maximum values from the data array
                    const minValue = Math.min(...dataArray);
                    const maxValue = Math.max(...dataArray);

                    // Calculate the range for the Y-axis (min - 20% to max + 20%)
                    const yMin = minValue - (0.2 * (maxValue - minValue));
                    const yMax = maxValue + (0.2 * (maxValue - minValue));

                    // Get the canvas element and its context
                    const canvas = document.getElementById(chartId);
                    const ctx = canvas.getContext('2d');

                    // Set the canvas size
                    canvas.width = 800;
                    canvas.height = 500;

                    // Define chart colors
                    const lineColor = 'rgba(75, 192, 192, 1)';
                    const fillColor = 'rgba(75, 192, 192, 0.1)';

                    // Calculate chart dimensions
                    const chartWidth = canvas.width;
                    const chartHeight = canvas.height;

                    // Calculate the scale of the chart
                    const scaleY = chartHeight / (yMax - yMin); // Adjusted scaleY calculation

                    // Calculate the width of each data point
                    const pointWidth = chartWidth / (timeDataArray.length - 1);
                    const chartPointWidth = chartWidth / (dataArray.length - 1);

                    // Determine the number of Y axis labels
                    const numYLabels = dataArray.length / yDivider;

                    // Calculate Y-axis label values based on the range (min - 20% to max + 20%)
                    const yLabelValues = [];
                    for (let i = 0; i < numYLabels; i++) {
                        const labelValue = yMin + (i / (numYLabels - 1)) * (yMax - yMin);
                        yLabelValues.push(labelValue);
                    }

                    // Draw horizontal grid lines and Y-axis legends on the canvas
                    for (let i = 0; i < numYLabels; i++) {
                        const yPos = chartHeight - (yLabelValues[i] - yMin) * scaleY;

                        // Draw horizontal grid lines
                        ctx.beginPath();
                        ctx.moveTo(0, yPos);
                        ctx.lineTo(chartWidth, yPos);
                        ctx.strokeStyle = '#ddd';
                        ctx.lineWidth = 1;
                        ctx.stroke();

                        // Add Y-axis legends (showing Y-position value)
                        ctx.fillStyle = '#000';
                        ctx.font = '12px Arial';
                        ctx.textAlign = 'right';
                        ctx.fillText(yLabelValues[i].toFixed(0) + suffix, chartWidth - 10, yPos + 20);
                    }

                    // Determine the number of X axis labels based on data.length
                    const numXLabels = timeDataArray.length;

                    // Draw vertical grid lines and X-axis legends on the canvas
                    for (let i = 0; i < numXLabels; i++) {
                        const xPos = i * pointWidth;
                        let xaxisValue = "";
                        // Draw vertical grid lines
                        ctx.beginPath();
                        ctx.moveTo(xPos, 0);
                        ctx.lineTo(xPos, chartHeight);
                        ctx.strokeStyle = '#ddd';
                        ctx.lineWidth = 1;
                        ctx.stroke();

                        // Add X-axis legends (showing X-position value)
                        ctx.fillStyle = '#000';
                        ctx.font = '12px Arial';
                        ctx.textAlign = 'center';
                        xaxisValue = timeDataArray[i].toString();
                        ctx.fillText(xaxisValue, xPos - 20, chartHeight - 10);
                    }

                    // Draw the chart line on the canvas
                    ctx.beginPath();
                    ctx.moveTo(0, chartHeight - (dataArray[0] - yMin) * scaleY);

                    for (let i = 1; i < dataArray.length; i++) {
                        ctx.lineTo(i * chartPointWidth, chartHeight - (dataArray[i] - yMin) * scaleY);
                    }

                    ctx.strokeStyle = lineColor;
                    ctx.lineWidth = 2;
                    ctx.stroke();

                    // Fill the area under the chart line
                    ctx.lineTo(chartWidth, chartHeight);
                    ctx.lineTo(0, chartHeight);
                    ctx.closePath();
                    ctx.fillStyle = fillColor;
                    ctx.fill();
                }

                drawCart(temperatureData, timeDayArray, "\u00B0" + "C", 8, 8, "temperatureChart");
                drawCart(humidityData, timeDayArray,  "\u00B0" + "%", 8, 8, "humidityChart");
                drawCart(pressureData, timeDayArray, " hPa", 8, 8, "pressureChart");
                drawCart(co2Data, timeDayArray, " ppm", 8, 8, "co2Chart");
                drawCart(tvocData, timeDayArray, " ppb", 8, 8, "tvocChart");


                // Get the h4 element by its id
                const timeElement = document.getElementById("nowTime");
                const tempElement = document.getElementById("nowTemp");
                const humElement = document.getElementById("nowHum");
                const presElement = document.getElementById("nowPres");
                const co2Element = document.getElementById("nowCO2");
                const tvocElement = document.getElementById("nowTVOC");

                // Convert the time to UK format
                const date = new Date("@timeNow");
                const options = {
                    weekday: 'long',
                    year: 'numeric',
                    month: '2-digit',
                    day: '2-digit',
                    hour: '2-digit',
                    minute: '2-digit',
                    hour12: true,
                };
                const ukTime = formattedTime = date.toLocaleDateString('en-US', options);

                // Update the content of the h4 element with the UK format time
                timeElement.textContent = ukTime;
                tempElement.textContent = `Temperature: ${parseFloat("@nowTemp").toFixed(2)} `;
                humElement.textContent = `Humidity: ${parseFloat("@nowHum").toFixed(2)}  %`;
                presElement.textContent = `Pressure: ${parseFloat("@nowPres").toFixed(0)}  hPa`;
                co2Element.textContent = `CO2: ${parseFloat("@nowCO2").toFixed(0)}  ppm`;
                tvocElement.textContent = `TVOC: ${parseFloat("@nowTVOC").toFixed(0)}  ppd`;
            </script>
        </body>
    </html>
)";

void defineWiFi() {
    server.send(200, "text/html", wifiConfigPage);
}

void defineWEBView() {
    String wifiPage = String(wifiWEBView);
    wifiPage.replace("@clientName", ClientNames[selected_client].client_name);
    wifiPage.replace("@clientNumber", String(selected_client));
    wifiPage.replace("@selectedPeriod", String(selected_period_id));
    wifiPage.replace("@timeData", time_String);
    wifiPage.replace("@temperatureData", bme280_temp_String);
    wifiPage.replace("@humidityData", bme280_hum_String);
    wifiPage.replace("@pressureData", bmp280_press_String);
    wifiPage.replace("@co2Data", ccs811_eco2_String);
    wifiPage.replace("@tvocData", ccs811_tvoc_String);
    wifiPage.replace("@timeNow", CurrentTime.timeString);
    wifiPage.replace("@nowTemp", SensorData[selected_client].bme280_temp);
    wifiPage.replace("@nowHum", SensorData[selected_client].bme280_hum);
    wifiPage.replace("@nowPres", SensorData[selected_client].bmp280_press);
    wifiPage.replace("@nowCO2", SensorData[selected_client].ccs811_eco2);
    wifiPage.replace("@nowTVOC", SensorData[selected_client].ccs811_tvoc);
    wifiPage.replace("@xLegendLabel", web_x_legends[selected_x_legend]);

    server.send(200, "text/html", wifiPage);
}

void saveConfigToFile() {
    String ssid = String(server.arg("ssid"));
    String password = String(server.arg("password"));

    JSONVar
    jsonDoc;                                            // Write data to a JSON file
    jsonDoc["ssid"] = ssid;
    jsonDoc["pass"] = password;
    Serial.println("Saving Configuration:");
    // Serial.println(jsonDoc);

    File
    configFile = SPIFFS.open("/config.txt", "w");         // write to file
    if (configFile) {
        configFile.println(jsonDoc);
        configFile.close();

        configFile = SPIFFS.open("/config.txt", "r");
        String a = configFile.readString();
        Serial.println("saved config:" + a);
    } else {
        Serial.println("Failed to open config file for writing");
    }
}

void resetSavedData(){
    // Delete the file if it exists
    if (SPIFFS.exists("/config.txt")) {
        SPIFFS.remove("/config.txt");
        Serial.println("Config deleted");
    } else {
        Serial.println("Config does not exist");
    }
}

void connectToWifi(String ssid_string, String password_string) {
    Serial.println("Connecting to Wi-Fi");
    const char *ssid     = ssid_string.c_str();
    const char *password = password_string.c_str();

    WiFi.begin(ssid, password);
    int8_t connections = 0;

    while (WiFi.status() != WL_CONNECTED  && connections < max_connections) {
        delay(1000);
        Serial.print(".");
        connections++;
    }

    if (connections == max_connections) {
        Serial.println("\nConnect to Wi-Fi issue!");
        internalLedBlink(5);
    } else {
        Serial.print("\nConnected to Wi-Fi!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        displayData();
    }
}

String modifyStringToCharLength(String a, int8_t stringLength, char symbol[1]) {
    while (a.length() < stringLength) {
        a = String(symbol) + a;
    }

    if (a.length() > stringLength) {
        a = a.substring(0, stringLength);
    }

    return a;
}

void startServerListener() {
    // the server listener
    server.on("/data", HTTP_GET, [](){
        int8_t clientNumber = server.arg("client_number").toInt();

        if (clientNumber <= MAX_CLIENTS) {
            String data_client_name = String(server.arg("client_name"));
            String data_bme280_temp = String(server.arg("bme280_temp"));
            String data_bmp280_press = String(server.arg("bmp280_press"));
            String data_bme280_hum = String(server.arg("bme280_hum"));
            String data_ccs811_eco2 = String(server.arg("ccs811_eco2"));
            String data_ccs811_tvoc = String(server.arg("ccs811_tvoc"));

            ClientNames[clientNumber].client_name = modifyStringToCharLength(data_client_name, 10, " ");
            SensorData[clientNumber].bme280_temp = modifyStringToCharLength(data_bme280_temp, SENSOR_STRING_LENGTH, "0");
            SensorData[clientNumber].bmp280_press = modifyStringToCharLength(data_bmp280_press, SENSOR_STRING_LENGTH, "0");
            SensorData[clientNumber].bme280_hum = modifyStringToCharLength(data_bme280_hum, SENSOR_STRING_LENGTH, "0");
            SensorData[clientNumber].ccs811_eco2 = modifyStringToCharLength(data_ccs811_eco2, SENSOR_STRING_LENGTH, "0");
            SensorData[clientNumber].ccs811_tvoc = modifyStringToCharLength(data_ccs811_tvoc, SENSOR_STRING_LENGTH, "0");

            Serial.print("Received data from client #: ");
            Serial.print(clientNumber);
            Serial.print(" time: ");
            Serial.println(CurrentTime.timeString);

            internalLedBlink(2);
            server.send(200, "text/plain", "Data received by MAIN_SERVER");
        } else {
            server.send(503, "text/plain", "Server busy. Try again later.");
        }
    });

    // the WEB view
    server.on("/", HTTP_GET, defineWEBView);
    server.on("/data", HTTP_GET, defineWEBView);
    server.begin();
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

void setCurrentTime(unsigned long incrementMillisecond) {
    char time[TIME_STRING_LENGTH];
    if (incrementMillisecond == 0) {
        timeClient.update();
        epochTime = timeClient.getEpochTime();
        CurrentTime.time = DateTime(timeClient.getEpochTime());
    } else {
        CurrentTime.time = DateTime((epochTime + (incrementMillisecond / 1000)));
    }
    getTimeString(CurrentTime.time).toCharArray(time, sizeof(time));
    CurrentTime.timeString = String(time);
}

void restartESP () {
    ESP.restart();
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("− failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" − not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

String readFile(fs::FS &fs, const char * path) {
    File file = fs.open(path, "r");
    if (!file) {
        Serial.println("File not found: " + String(path));
        return String("");
    }
    String data = file.readString();
    file.close();
    return data;
}

void readDataFromFile(const char * period, int8_t points, PeriodDataStructure * PeriodStructure) {
    for (int i = 0; i < points; i++) {
        for (int j = 0; j < MAX_CLIENTS; j++) {
            String fileName = "/" + String(period) + "_client_" + String(j + 1) + "_point_" + String(i + 1);
            String jsonString = readFile(SPIFFS, fileName.c_str());
            if (jsonString.length()) {
                Serial.println(jsonString);
                JSONVar jsonData = JSON.parse(jsonString);

                if (JSON.typeof(jsonData) == "undefined") {
                    Serial.println("Failed to parse JSON.");
                } else {
                    // Copy the data to the data structure
                    const char* time = jsonData["time"];
                    const char* bme280_temp = jsonData["bme280_temp"];
                    const char* bmp280_press = jsonData["bmp280_press"];
                    const char* bme280_hum = jsonData["bme280_hum"];
                    const char* ccs811_eco2 = jsonData["ccs811_eco2"];
                    const char* ccs811_tvoc = jsonData["ccs811_tvoc"];


                    PeriodStructure[i].TimeString = String(time);
                    PeriodStructure[i].SensorData[j].bme280_temp = String(bme280_temp);
                    PeriodStructure[i].SensorData[j].bmp280_press = String(bmp280_press);
                    PeriodStructure[i].SensorData[j].bme280_hum = String(bme280_hum);
                    PeriodStructure[i].SensorData[j].ccs811_eco2 = String(ccs811_eco2);
                    PeriodStructure[i].SensorData[j].ccs811_tvoc = String(ccs811_tvoc);
                }
            }
        }
    }
};

void setup() {
    // GENERAL
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);
    Serial.begin(115200);

    // BUTTONS
    mainButton.attachClick(mainButtonSingleClick);
    mainButton.attachDoubleClick(mainButtonCheck2DoubleClicks);

    // DISPLAY
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }

    // Show initial display buffer contents on the screen --
    // the library initializes this with an Adafruit splash screen.
    // Clear the buffer
    display.clearDisplay();
    display.display();

    // READ CONFIG FROM
    // Initialize SPIFFS - file system
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount file system, formatting the File system");
        SPIFFS.format();
        Serial.print("Formatting.....");
        delay(10000);                                           // formatting time
        Serial.println("finished.");
        return;
    };

    delay(5000);                                                // delay to initialize SPIFFS
    // listDir(SPIFFS, "/", 0);

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    Serial.println("SPIFFS Capacity:");
    Serial.printf("Total bytes: %u\n", totalBytes);
    Serial.printf("Used bytes: %u\n", usedBytes);
    Serial.printf("Free bytes: %u\n", freeBytes);

    connection_defined = false;                                 // Initial Connection state before checking saved data
    File configFile = SPIFFS.open("/config.txt");               // Read Configuration data from the JSON file

    if (!configFile) {
        Serial.println("Failed to open config file for reading");
    } else {
        String jsonStr = configFile.readString();               // Read the contents of the file into a string
        configFile.close();                                     // Close the file to reduce memory usage
        JSONVar jsonData = JSON.parse(jsonStr);                 // Deserialize the JSON string into an Arduino_JSON object
        Serial.println("jsonStr: " + jsonStr);

        if (JSON.typeof(jsonData) == "undefined") {
            Serial.println("Failed to parse JSON data");
        } else {
            const char* ssidChar = jsonData["ssid"];
            const char* passChar = jsonData["pass"];
            saved_ssid = String(ssidChar);
            saved_pass = String(passChar);
        }

        connection_defined = saved_ssid.length() && saved_pass.length() ? true : false;
    }
    Serial.print("connection_defined: ");
    Serial.println(connection_defined);

    // If no Config, run initial page with a wifi form
    // Setting the AP Mode with SSID, Password, and Max Connection Limit
    if (!connection_defined) {
        Serial.print("Creadt Access Point");
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
        server.on("/configure", HTTP_GET, saveConfigToFile);
        // Route to restart
        server.on("/restart", HTTP_GET, restartESP);
        server.begin();
    } else {
        // Connect to Wi-Fi
        connectToWifi(saved_ssid, saved_pass);

        // Read Data From Files
        readDataFromFile("day", DAY_DATA_POINTS, DayData);
        readDataFromFile("week", WEEK_DATA_POINTS, WeekData);
        readDataFromFile("month", MONTH_DATA_POINTS, MonthData);

        startServerListener();
    }

    // initial time setup
    setCurrentTime(0);
    webViewLoopTimer = millis();
    dayLoopTimer = millis();
}

// BUTTON ACTIONS
int8_t doubleClickCount = 0;

void mainButtonSingleClick() {
    Serial.println("Single click");
    doubleClickCount = 0;                         // Reset the doubleClickCount count
    digitalWrite(ledPin, !digitalRead(ledPin));   // Toggle the LED state
}

void mainButtonCheck2DoubleClicks() {
    doubleClickCount++;
    Serial.println("Double click");
    if (doubleClickCount == 2) {
        Serial.println("Quad click");
        resetSavedData();                           // reset config
        internalLedBlink(4);                        // confirm by blinking
        esp_restart();                              // Reset the ESP32 programmatically
    }
}
// BUTTON ACTIONS END

void displayData() {
    display.clearDisplay();

    display.setTextSize(1);                       // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);          // Draw white text
    display.setCursor(0,0);                       // Start at top-left corner
    display.print("IP: ");
    display.println(WiFi.localIP());
    // TODO
    // show amount of clients
    // blink online status etc..
    display.display();
}

void internalLedBlink(int8_t blinkCounter) {
    for (int8_t i=0; i < blinkCounter; i++) {
        digitalWrite(ledPin, LOW);
        delay(300);
        digitalWrite(ledPin, HIGH);
        delay(300);
    }
}

void shiftPeriodDataStructure(PeriodDataStructure arr[], int size) {
    // Shift elements to the left
    for (int i = 0; i < size - 1; i++) {
        arr[i] = arr[i + 1];
    }
}

void updateLEDDisplay() {
    // display connected devices and IP
}

void updateWEBView() {
    // web interface to display data
}

void writeToFile(fs::FS &fs, const char * path, const char * data){
   File file = fs.open(path, "w");

   if(!file){
      Serial.print("File:");
      Serial.print(path);
      Serial.println("− failed to open.");
      return;
   }

   if(file.print(data)){
      // Serial.print("File:");
      // Serial.print(path);
      // Serial.print(" Data:");
      // Serial.println(data);
      file.close();
   } else {
      Serial.print("File:");
      Serial.print(path);
      Serial.println("− writing failed.");
   }
}

void saveDataToFile(const char * period, int8_t points, PeriodDataStructure * PeriodStructure) {
    JSONVar json;
    String fileName;

    for (int i = 0; i < points; i++) {
        for (int j = 0; j < MAX_CLIENTS; j++) {
            json["time"] = PeriodStructure[i].TimeString;
            json["bme280_temp"] = PeriodStructure[i].SensorData[j].bme280_temp;
            json["bme280_hum"] = PeriodStructure[i].SensorData[j].bme280_hum;
            json["bmp280_press"] = PeriodStructure[i].SensorData[j].bmp280_press;
            json["ccs811_eco2"] = PeriodStructure[i].SensorData[j].ccs811_eco2;
            json["ccs811_tvoc"] = PeriodStructure[i].SensorData[j].ccs811_tvoc;

            fileName = "/" + String(period) + "_client_" + String(j+1) + "_point_" + String(i+1);
            writeToFile(SPIFFS, fileName.c_str(), JSON.stringify(json).c_str());
        }
    }
}

void saveData () {
    Serial.print("Saving data...");

    PeriodDataStructure data;
    data.TimeString = CurrentTime.timeString;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        data.SensorData[i].bme280_temp = SensorData[i].bme280_temp;
        data.SensorData[i].bmp280_press = SensorData[i].bmp280_press;
        data.SensorData[i].bme280_hum = SensorData[i].bme280_hum;
        data.SensorData[i].ccs811_eco2 = SensorData[i].ccs811_eco2;
        data.SensorData[i].ccs811_tvoc = SensorData[i].ccs811_tvoc;
    }

    // delete the first item if array is full, by shifting data to the left, always save a new data to the latest element of array,
    shiftPeriodDataStructure(DayData, DAY_DATA_POINTS);
    DayData[DAY_DATA_POINTS - 1] = data;

    if (weekSaveCounter == 4) {                                             // every forth iteration == every hour
        shiftPeriodDataStructure(WeekData, WEEK_DATA_POINTS);
        WeekData[WEEK_DATA_POINTS - 1] = data;
    }

    if (monthSaveCounter == 16) {                                            // every 16th iteration == every 4 hours
        shiftPeriodDataStructure(MonthData, MONTH_DATA_POINTS);
        MonthData[MONTH_DATA_POINTS - 1] = data;
    }

//    if (weekSaveCounter == 4) {                                             // save to files every forth iteration == every hour
//        saveDataToFile("day", DAY_DATA_POINTS, DayData);
//        saveDataToFile("week", WEEK_DATA_POINTS, WeekData);
//        saveDataToFile("month", MONTH_DATA_POINTS, MonthData);
//    }

    saveDataToFile("day", DAY_DATA_POINTS, DayData);
    saveDataToFile("week", WEEK_DATA_POINTS, WeekData);
    saveDataToFile("month", MONTH_DATA_POINTS, MonthData);

    Serial.println("saved.");
}

void generateChartsData(const char * period, int8_t sensor_id) {
    JSONVar timeDataArray;
    JSONVar bme280TempArray;
    JSONVar bmp280PressArray;
    JSONVar bme280HumArray;
    JSONVar ccs811Eco2Array;
    JSONVar ccs811TvocArray;

    if (period == "day") {
        for (int i = 0; i < DAY_DATA_POINTS; i++) {
            timeDataArray[i] = DayData[i].TimeString;
            bme280TempArray[i] = DayData[i].SensorData[sensor_id].bme280_temp;
            bmp280PressArray[i] = DayData[i].SensorData[sensor_id].bmp280_press;
            bme280HumArray[i] = DayData[i].SensorData[sensor_id].bme280_hum;
            ccs811Eco2Array[i] = DayData[i].SensorData[sensor_id].ccs811_eco2;
            ccs811TvocArray[i] = DayData[i].SensorData[sensor_id].ccs811_tvoc;
        }

        time_String =  JSON.stringify(timeDataArray);
        bme280_temp_String = JSON.stringify(bme280TempArray);
        bmp280_press_String = JSON.stringify(bmp280PressArray);
        bme280_hum_String = JSON.stringify(bme280HumArray);
        ccs811_eco2_String = JSON.stringify(ccs811Eco2Array);
        ccs811_tvoc_String = JSON.stringify(ccs811TvocArray);
    }
}

void loop() {
    nowMilliSeconds = millis();

    mainButton.tick();                                                      // Call the tick() function to update the button state
    server.handleClient();                                                  // run server in loop

    if (connection_defined) {
        if ((nowMilliSeconds - webViewLoopTimer) > webViewIterationTimer) {   // every webViewIterationTimer actions
            Serial.println("WEB loop tick...");
            setCurrentTime(0);
            generateChartsData("day", 0);
            webViewLoopTimer = millis();
        }

        if ((nowMilliSeconds - dayLoopTimer) > dayIterationTimer) {          // every dayIterationTimer actions
            Serial.println("Day loop tick...");
            setCurrentTime(0);
            saveData();
            weekSaveCounter = weekSaveCounter == 1 ? 4 : weekSaveCounter - 1;
            monthSaveCounter = monthSaveCounter == 1 ? 16 : monthSaveCounter - 1;
            dayLoopTimer = millis();
        }
    }
}
