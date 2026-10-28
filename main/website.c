#define WIFI_SSID "EEE40011-PROJECT"
#define WIFI_PASSWORD "123456"
#define HISTORY_MAX_RECORDS 900
const char *dataFile = "/temp_log.csv";
static float cooling_temp = 0, ambient_temp = 0;
static bool cooling_ok = false, ambient_ok = false;

WiFi.begin(ssid, password);
Serial.print("Connecting to Wi-Fi");
while (WiFi.status() != WL_CONNECTED)
{
    delay(500);
    Serial.print(".");
}
Serial.println("\nWi-Fi Connected!");
// Print the ESP32 IP Address
Serial.print("Access ESP32 IP Address: http://");
Serial.println(WiFi.localIP());

void initSD()
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS))
    {
        Serial.println("SD Card Failed");
        return;
    }

    if (!SD.exists(dataFile))
    {
        File f = SD.open(dataFile, FILE_WRITE);
        if (f)
        {
            f.println(time, day, temp_blood, temp_internal);
            f.close();
        }
    }
}
