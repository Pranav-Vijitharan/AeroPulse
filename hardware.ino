#include <driver/i2s.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ArduinoJson.h>

#define BUTTON_PIN 14
#define BUZZER_PIN 20
#define DEBOUNCE_DELAY 200
#define I2S_WS 15
#define I2S_SD 13
#define I2S_SCK 2
#define I2S_PORT I2S_NUM_0
#define I2S_SAMPLE_RATE   (16000)
#define I2S_SAMPLE_BITS   (16)
#define I2S_READ_LEN      (16 * 1024)
#define RECORD_TIME       (10) //Seconds
#define I2S_CHANNEL_NUM   (1)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)
#define CHUNK_SIZE        (20000)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define NOTE_C5 523
#define NOTE_E5 659
#define NOTE_G5 783

const char* ssid = "Pranav";     // Replace with your Wi-Fi SSID
const char* password = "asdfghjkl"; // Replace with your Wi-Fi password
const char* server_url = "https://aeropulse.onrender.com";  // Your server URL

static bool isRecording = false;
static bool processComplete = false;  // New flag to track completion
static unsigned long lastDebounceTime = 0;

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

File file;
const char filename[] = "/recording.wav";
const int headerSize = 44;

void setup() {
  Serial.begin(115200);
  if(!display.begin(0x3C)) {
    Serial.println("Display initialization failed");
    for(;;);
  }
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  
  display.clearDisplay();
  pulseAnimation();
  simpleChime();
  displayProductScreen();
  promptUserAction();
  connectToWiFi();
}

void loop() {
  int buttonState = digitalRead(BUTTON_PIN);
  unsigned long currentTime = millis();

  // Allow button to restart process after completion
  if (buttonState == LOW && (currentTime - lastDebounceTime) > DEBOUNCE_DELAY) { 
    lastDebounceTime = currentTime;

    if (processComplete) {  

      if (SPIFFS.exists(filename)) {
        Serial.println("Deleting previous recording...");
        SPIFFS.remove(filename);
      }

      // Reset system to allow new cycle
      Serial.println("Restarting process...");
      processComplete = false;  // Reset flag
      isRecording = false;
      display.clearDisplay();
      promptUserAction();
      connectToWiFi();
      return;
    }

    if (!isRecording) { 
      isRecording = true;
      Serial.println("Button pressed!");
      doubleClickSound();
      buttonPressedCircleAnimation();
      successChime();
      flashingCounterAndScanning();

      SPIFFSInit();
      i2sInit();

      if (xTaskCreate(i2s_adc, "i2s_adc", 1024 * 8, NULL, 1, NULL) != pdPASS) {
        Serial.println("Failed to create task!");
        isRecording = false; // Reset recording state if task creation failed
      }

      delay(6000);
      displayProcessing();
      delay(20000);
      progressBarAnimation();
      getPredictionFromServer();
      successChime();  // Second occurrence

      // Erase stored recording after process completion
      if (SPIFFS.exists(filename)) {
        Serial.println("Deleting recording...");
        SPIFFS.remove(filename);
      }

      // Erase stored recording after process completion
      if (SPIFFS.exists(filename)) {
        Serial.println("Deleting recording...");
        SPIFFS.remove(filename);
      }
      // Mark process as complete so it can restart on button press
      processComplete = true;
      isRecording = false;
    }
  }
}

void SPIFFSInit(){
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS initialisation failed!");
    while(1) yield();
  }

  SPIFFS.remove(filename);
  file = SPIFFS.open(filename, FILE_WRITE);
  if(!file){
    Serial.println("File is not available!");
  }

  byte header[headerSize];
  wavHeader(header, FLASH_RECORD_SIZE);

  file.write(header, headerSize);
  listSPIFFS();
}

void i2sInit(){
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = 0,
    .dma_buf_count = 64,
    .dma_buf_len = 1024,
    .use_apll = 1
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_set_pin(I2S_PORT, &pin_config);
}


void i2s_adc_data_scale(uint8_t * d_buff, uint8_t* s_buff, uint32_t len)
{
    uint32_t j = 0;
    uint32_t dac_value = 0;
    for (int i = 0; i < len; i += 2) {
        dac_value = ((((uint16_t) (s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
        d_buff[j++] = 0;
        d_buff[j++] = dac_value * 256 / 2048;
    }
}

void i2s_adc(void* arg) {
  int i2s_read_len = I2S_READ_LEN;
  int flash_wr_size = 0;
  size_t bytes_read;

  char* i2s_read_buff = (char*)calloc(i2s_read_len, sizeof(char));
  uint8_t* flash_write_buff = (uint8_t*)calloc(i2s_read_len, sizeof(char));

  i2s_read(I2S_PORT, (void*)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
  i2s_read(I2S_PORT, (void*)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);

  Serial.println(" *** Recording Start *** ");
  while (flash_wr_size < FLASH_RECORD_SIZE) {
    i2s_read(I2S_PORT, (void*)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    i2s_adc_data_scale(flash_write_buff, (uint8_t*)i2s_read_buff, i2s_read_len);
    file.write((const byte*)flash_write_buff, i2s_read_len);
    flash_wr_size += i2s_read_len;
    ets_printf("Sound recording %u%%\n", flash_wr_size * 100 / FLASH_RECORD_SIZE);
  }
  file.close();

  free(i2s_read_buff);
  free(flash_write_buff);

  listSPIFFS();

  uploadFileToCloud(); // Upload the file in chunks after recording

  vTaskDelete(NULL);
}

void uploadFileToCloud() {
  File file = SPIFFS.open(filename, "r");
  if (!file || file.isDirectory()) {
    Serial.println("Failed to open WAV file for reading");
    return;
  }

  HTTPClient http;
  String boundary = "----ESP32Boundary";
  http.begin("https://aeropulse.onrender.com/upload"); // Replace with your server URL
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  // Read the first chunk of the file
  uint8_t buffer[CHUNK_SIZE];
  size_t bytesRead = file.read(buffer, CHUNK_SIZE);
  if (bytesRead > 0) {
    // Construct the multipart form-data body
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n";
    bodyStart += "Content-Type: audio/wav\r\n\r\n";

    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    // Combine the parts of the body
    String body = bodyStart;
    body += String((char*)buffer, bytesRead); // Append the first chunk
    body += bodyEnd;

    // Send the HTTP POST request
    int httpResponseCode = http.POST(body);

    // Log the server response
    if (httpResponseCode > 0) {
      Serial.printf("File uploaded, response: %d\n", httpResponseCode);
      Serial.println(http.getString()); // Print server response
    } else {
      Serial.printf("Upload failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
  } else {
    Serial.println("Failed to read the first chunk");
  }

  file.close();
  http.end();
}
void example_disp_buf(uint8_t* buf, int length)
{
    printf("======\n");
    for (int i = 0; i < length; i++) {
        printf("%02x ", buf[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("======\n");
}

void wavHeader(byte* header, int wavSize){
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSize = wavSize + headerSize - 8;
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;
  header[21] = 0x00;
  header[22] = 0x01;
  header[23] = 0x00;
  header[24] = 0x80;
  header[25] = 0x3E;
  header[26] = 0x00;
  header[27] = 0x00;
  header[28] = 0x00;
  header[29] = 0x7D;
  header[30] = 0x00;
  header[31] = 0x00;
  header[32] = 0x02;
  header[33] = 0x00;
  header[34] = 0x10;
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(wavSize & 0xFF);
  header[41] = (byte)((wavSize >> 8) & 0xFF);
  header[42] = (byte)((wavSize >> 16) & 0xFF);
  header[43] = (byte)((wavSize >> 24) & 0xFF);
  
}


void listSPIFFS(void) {
  Serial.println(F("\r\nListing SPIFFS files:"));
  static const char line[] PROGMEM =  "=================================================";

  Serial.println(FPSTR(line));
  Serial.println(F("  File name                              Size"));
  Serial.println(FPSTR(line));

  fs::File root = SPIFFS.open("/");
  if (!root) {
    Serial.println(F("Failed to open directory"));
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(F("Not a directory"));
    return;
  }

  fs::File file = root.openNextFile();
  while (file) {

    if (file.isDirectory()) {
      Serial.print("DIR : ");
      String fileName = file.name();
      Serial.print(fileName);
    } else {
      String fileName = file.name();
      Serial.print("  " + fileName);
      // File path can be 31 characters maximum in SPIFFS
      int spaces = 33 - fileName.length(); // Tabulate nicely
      if (spaces < 1) spaces = 1;
      while (spaces--) Serial.print(" ");
      String fileSize = (String) file.size();
      spaces = 10 - fileSize.length(); // Tabulate nicely
      if (spaces < 1) spaces = 1;
      while (spaces--) Serial.print(" ");
      Serial.println(fileSize + " bytes");
    }

    file = root.openNextFile();
  }

  Serial.println(FPSTR(line));
  Serial.println();
  delay(1000);
}

void getPredictionFromServer() {
  // Check WiFi connection status
  if(WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(server_url) + "/get_prediction";
    
    Serial.println("Requesting prediction from server...");
    http.begin(url);
    
    // Send HTTP GET request
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println("Raw response: " + response);
      
      // Parse JSON response
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        // Extract the prediction and confidence
        String prediction = doc["prediction"].as<String>();
        
        // Print to console
        Serial.println("------------------------------");
        Serial.println("PREDICTION RESULTS:");
        Serial.print("Condition: ");
        Serial.println(prediction);
        Serial.println("------------------------------");

        displayPredictionResult(prediction);

      } else {
        Serial.print("JSON parsing error: ");
        Serial.println(error.c_str());
      }
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      Serial.println(http.errorToString(httpResponseCode).c_str());
    }
    
    // Free resources
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
    connectToWiFi(); // Try to reconnect
  }
}

void displayPredictionResult(String prediction) {
  displayWithBorder();
  display.setTextSize(1); // Set text size for better fit
  display.setTextColor(SH110X_WHITE); // Set text color

  // Display "Result:"
  String line1 = "Result:";
  int16_t x1, y1;
  uint16_t w1, h1;
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, (SCREEN_HEIGHT / 2) - h1 - 4);
  display.println(line1);

  // Modify "Bronchiolitis" to "Bronchi"
  if (prediction == "Bronchiolitis") {
    prediction = "Bronchi";
  }

  // Display prediction result
  int16_t x2, y2;
  uint16_t w2, h2;
  display.getTextBounds(prediction, 0, 0, &x2, &y2, &w2, &h2);
  display.setCursor((SCREEN_WIDTH - w2) / 2, (SCREEN_HEIGHT / 2) + 2);
  display.println(prediction);

  display.display();
}


void pulseAnimation() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  for (int i = 0; i < display.width(); i += 4) {
    display.drawPixel(i, display.height() / 2 + (sin(i * 0.1) * 8), SH110X_WHITE);
    display.display();
    delay(10);
  }
  delay(1000);
  display.clearDisplay();
}

void displayProductScreen() {
  displayWithBorder();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  String message = "AeroPulse";
  centerText(message, 2);
  delay(2000);
}

void promptUserAction() {
  displayWithBorder();
  display.setTextSize(1); // Set text size for better fit
  display.setTextColor(SH110X_WHITE); // Set text color

  // Display "Ready to Scan"
  String line1 = "Ready to Scan";
  int16_t x1, y1;
  uint16_t w1, h1;
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, (SCREEN_HEIGHT / 2) - h1 - 4); // Adjust vertical position
  display.println(line1);

  // Display "Press Button"
  String line2 = "Press Button";
  int16_t x2, y2;
  uint16_t w2, h2;
  display.getTextBounds(line2, 0, 0, &x2, &y2, &w2, &h2);
  display.setCursor((SCREEN_WIDTH - w2) / 2, (SCREEN_HEIGHT / 2) + 2); // Center text below the first line
  display.println(line2);

  display.display(); 
}


void buttonPressedCircleAnimation() {
  int maxRadius = min(SCREEN_WIDTH, SCREEN_HEIGHT) / 4;
  for (int radius = 0; radius < maxRadius; radius += 2) {
    displayWithBorder();
    display.drawCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, radius, SH110X_WHITE);
    display.display();
    delay(25);
  }
  delay(500);
  displayWithBorder();
}

void flashingCounterAndScanning() {
  displayWithBorder();
  display.setTextSize(3); // Larger text for the counter

  // Flashing counter from 3 to 1
  for (int i = 3; i > 0; i--) {
    displayWithBorder();
    String count = String(i);
    centerText(count, 3);
    delay(1000);
    display.clearDisplay();
    displayWithBorder();
  }

  // Display "Scanning..." after the countdown
  display.setTextSize(1);
  String message = "Scanning...";
  centerText(message, 1);
  delay(2000); 
}

void displayWithBorder() {
  display.clearDisplay();
  display.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, SH110X_WHITE);
  display.display();
}

void centerText(String message, int textSize) {
  display.setTextSize(textSize);
  display.setTextColor(SH110X_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
  display.println(message);
  display.display();
}

void progressBarAnimation() {
  displayWithBorder();
  int barWidth = SCREEN_WIDTH - 20; // Further reduced width for the progress bar
  int barHeight = 12; // Height of the progress bar
  int barX = 10; // Increased X position to create more padding from the display edge
  int barY = (SCREEN_HEIGHT - barHeight) / 2; // Center the bar vertically

  display.drawRect(barX, barY, barWidth, barHeight, SH110X_WHITE); // Draw the empty progress bar
  for (int i = 0; i <= barWidth - 2; i += 4) {
    display.fillRect(barX + 1, barY + 1, i, barHeight - 2, SH110X_WHITE); // Fill the progress bar gradually
    display.display();
    delay(50); // Delay for demonstration purposes
  }
  delay(1000); // Hold the full bar for a moment
}

void displayProcessing() {
  displayWithBorder();
  display.setTextSize(1);
  String message = "Processing...";
  centerText(message, 1);
  delay(2000); 
}

void simpleChime() {
  tone(BUZZER_PIN, NOTE_C5, 500);  
  delay(550);                      
}

void doubleClickSound() {
  tone(BUZZER_PIN, NOTE_E5, 50);
  delay(80);
  tone(BUZZER_PIN, NOTE_E5, 50);
  delay(150);
}

void successChime() {
  tone(BUZZER_PIN, NOTE_G5, 100);
  delay(150);
  tone(BUZZER_PIN, NOTE_E5, 100);
  delay(150);
  tone(BUZZER_PIN, NOTE_C5, 300);
  delay(400);
}


void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nConnected to Wi-Fi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

