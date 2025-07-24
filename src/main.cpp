#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> // Add this line
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi Credentials
const char *ssid = "KNIH READING ROOM";
const char *password = "KNIH READING ROOM";

// API Keys
const char *DEEPGRAM_API_KEY = "e60e9df39a10927457676c9e31fb51b883750c4b";
const char *GEMINI_API_KEY = "AIzaSyC4rIZxYEk8P_cGqTK2uADBSYvVN4duBOE";

// I2S Microphone pins configuration
#define I2S_MIC_WS_PIN 4  // Word Select (LR)
#define I2S_MIC_SCK_PIN 5 // Bit Clock (SCK)
#define I2S_MIC_SD_PIN 6  // Serial Data (SD)

// I2S Speaker pins configuration (MAX98357A)
#define I2S_SPK_LRC_PIN 16  // Left/Right Clock
#define I2S_SPK_BCLK_PIN 15 // Bit Clock
#define I2S_SPK_DIN_PIN 7   // Data Input

// SD Card pins configuration
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_SCK_PIN 12
#define SD_MISO_PIN 13

// I2S configuration
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPK_PORT I2S_NUM_1
#define SAMPLE_RATE 16000
#define SAMPLE_BITS 16
#define CHANNEL_NUM 1

// Recording configuration - REDUCED BUFFER SIZE
#define RECORD_TIME 10  // Record for 10 seconds
#define BUFFER_SIZE 512 // Reduced from 1024 to 512

#define ATMEGA_CTRL_PIN 8

File audioFile;
bool recording = false;
bool playing = false;
unsigned long recordStartTime = 0;

// Allocate buffers in global memory instead of stack
int16_t audioBuffer[BUFFER_SIZE];
int32_t stereoBuffer[BUFFER_SIZE * 2];

// Add global WiFiClientSecure client
WiFiClientSecure client;

// Function Declarations
void setupMicrophone();
void setupSpeaker();
void startRecording();
void stopRecording();
void playLatestRecording();
void stopPlayback();
void listFiles();
void testTone();
void deleteAllFiles();
void transcribeLatestRecording();
void setupWifi();
void writeWavHeader(File &file, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize);
void generateGeminiResponse(String transcript);
String SpeechToText_Deepgram(String audio_filename);
String json_object(String input, String element);
void speakWithElevenLabs(String text);

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32-S3 I2S Audio Recorder + AI Assistant ===");
  Serial.println("Hardware Configuration:");
  Serial.println("I2S Microphone:");
  Serial.println("  WS (LR) -> Pin 4");
  Serial.println("  SCK -> Pin 5");
  Serial.println("  SD -> Pin 6");
  Serial.println("MAX98357A Amplifier:");
  Serial.println("  LRC -> Pin 16");
  Serial.println("  BCLK -> Pin 15");
  Serial.println("  DIN -> Pin 7");
  Serial.println("  SD -> 3.3V (Enable)");
  Serial.println();

  // Initialize SD card
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN))
  {
    Serial.println("ERROR: SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized successfully!");
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD card size: %lluMB\n", cardSize);

  setupMicrophone();
  setupSpeaker();

  Serial.println();
  Serial.println("Commands:");
  Serial.println("  's' - Start recording");
  Serial.println("  'x' - Stop recording");
  Serial.println("  'l' - List files on SD card");
  Serial.println("  'p' - Play latest recording");
  Serial.println("  'q' - Stop playback");
  Serial.println("  't' - Play test tone");
  Serial.println("  'd' - Delete all audio files");
  Serial.println("  'c' - Convert latest recording to text and get AI response");
  Serial.println();

  setupWifi();

  // Initialize WiFiClientSecure
  client.setInsecure(); // Add this line after setupWifi()

  pinMode(ATMEGA_CTRL_PIN, OUTPUT);
  digitalWrite(ATMEGA_CTRL_PIN, LOW);
}

void setupWifi()
{
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();

  // try to connect for 15 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("\nFailed to connect to WiFi. Transcription will not be available.");
  }
  else
  {
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void setupMicrophone()
{
  i2s_config_t i2s_mic_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4, // Reduced from 8 to 4
      .dma_buf_len = 512, // Reduced from 1024 to 512
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t mic_pin_config = {
      .bck_io_num = I2S_MIC_SCK_PIN,
      .ws_io_num = I2S_MIC_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_MIC_SD_PIN};

  esp_err_t result = i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL);
  if (result != ESP_OK)
  {
    Serial.printf("ERROR: Failed to install I2S microphone driver: %s\n", esp_err_to_name(result));
    return;
  }

  result = i2s_set_pin(I2S_MIC_PORT, &mic_pin_config);
  if (result != ESP_OK)
  {
    Serial.printf("ERROR: Failed to set I2S microphone pins: %s\n", esp_err_to_name(result));
    return;
  }

  Serial.println("I2S microphone driver installed successfully!");
}

void setupSpeaker()
{
  i2s_config_t i2s_spk_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // Changed to mono for MAX98357A
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0};

  i2s_pin_config_t spk_pin_config = {
      .bck_io_num = I2S_SPK_BCLK_PIN,
      .ws_io_num = I2S_SPK_LRC_PIN,
      .data_out_num = I2S_SPK_DIN_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE};

  esp_err_t result = i2s_driver_install(I2S_SPK_PORT, &i2s_spk_config, 0, NULL);
  if (result != ESP_OK)
  {
    Serial.printf("ERROR: Failed to install I2S speaker driver: %s\n", esp_err_to_name(result));
    return;
  }

  result = i2s_set_pin(I2S_SPK_PORT, &spk_pin_config);
  if (result != ESP_OK)
  {
    Serial.printf("ERROR: Failed to set I2S speaker pins: %s\n", esp_err_to_name(result));
    return;
  }

  Serial.println("I2S speaker driver installed successfully!");
}

void writeWavHeader(File &file, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize)
{
  file.seek(0);
  file.write((const uint8_t *)"RIFF", 4);
  uint32_t chunkSize = 36 + dataSize;
  file.write((uint8_t *)&chunkSize, 4);
  file.write((const uint8_t *)"WAVE", 4);
  file.write((const uint8_t *)"fmt ", 4);
  uint32_t subChunk1Size = 16;
  file.write((uint8_t *)&subChunk1Size, 4);
  uint16_t audioFormat = 1;
  file.write((uint8_t *)&audioFormat, 2);
  file.write((uint8_t *)&channels, 2);
  file.write((uint8_t *)&sampleRate, 4);
  uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  file.write((uint8_t *)&byteRate, 4);
  uint16_t blockAlign = channels * bitsPerSample / 8;
  file.write((uint8_t *)&blockAlign, 2);
  file.write((uint8_t *)&bitsPerSample, 2);
  file.write((const uint8_t *)"data", 4);
  file.write((uint8_t *)&dataSize, 4);
}

void startRecording()
{
  if (recording || playing)
  {
    Serial.println("Cannot record while playing or already recording!");
    return;
  }

  String filename = "/audio_" + String(millis()) + ".wav";

  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile)
  {
    Serial.println("ERROR: Failed to create audio file!");
    return;
  }

  // Reserve space for WAV header
  for (int i = 0; i < 44; i++)
    audioFile.write((uint8_t)0);

  recording = true;
  recordStartTime = millis();
  Serial.println("Recording started: " + filename);
  Serial.printf("Recording for %d seconds...\n", RECORD_TIME);
}

void stopRecording()
{
  if (!recording)
  {
    Serial.println("Not recording!");
    return;
  }

  recording = false;
  uint32_t dataSize = audioFile.size() - 44; // exclude header
  writeWavHeader(audioFile, SAMPLE_RATE, SAMPLE_BITS, CHANNEL_NUM, dataSize);
  audioFile.close();

  unsigned long recordDuration = (millis() - recordStartTime) / 1000;
  Serial.printf("Recording stopped. Duration: %lu seconds\n", recordDuration);
}

void playLatestRecording()
{
  if (recording || playing)
  {
    Serial.println("Cannot play while recording or already playing!");
    return;
  }

  // Find latest file
  File root = SD.open("/");
  String latestFileName = "";
  unsigned long latestTime = 0;

  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory() && String(file.name()).endsWith(".wav"))
    {
      String fileName = String(file.name());
      int startPos = fileName.indexOf("_") + 1;
      int endPos = fileName.indexOf(".wav");
      if (startPos > 0 && endPos > startPos)
      {
        unsigned long fileTime = fileName.substring(startPos, endPos).toInt();
        if (fileTime > latestTime)
        {
          latestTime = fileTime;
          if (fileName.startsWith("/"))
          {
            latestFileName = fileName;
          }
          else
          {
            latestFileName = "/" + fileName;
          }
        }
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  if (latestFileName == "")
  {
    Serial.println("No audio files found!");
    return;
  }

  Serial.println("Attempting to play: " + latestFileName);

  audioFile = SD.open(latestFileName, FILE_READ);
  if (!audioFile)
  {
    Serial.println("ERROR: Failed to open audio file for playback!");
    return;
  }

  playing = true;
  Serial.println("Playing: " + latestFileName);
  Serial.printf("File size: %d bytes\n", audioFile.size());

  // For mono MAX98357A - send data directly without stereo conversion
  while (audioFile.available() && playing)
  {
    int bytesRead = audioFile.read((uint8_t *)audioBuffer, sizeof(audioBuffer));

    if (bytesRead > 0)
    {
      size_t bytes_written = 0;
      esp_err_t result = i2s_write(I2S_SPK_PORT, audioBuffer, bytesRead, &bytes_written, portMAX_DELAY);

      if (result != ESP_OK)
      {
        Serial.printf("ERROR writing to I2S: %s\n", esp_err_to_name(result));
        break;
      }

      Serial.print("."); // Progress indicator
    }

    // Check for stop command
    if (Serial.available())
    {
      char command = Serial.read();
      if (command == 'q' || command == 'Q')
      {
        break;
      }
    }

    delay(1);
  }

  audioFile.close();
  playing = false;
  Serial.println("\nPlayback finished!");
}

void generateGeminiResponse(String transcript)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot generate AI response.");
    return;
  }

  Serial.println("\n=== Generating AI Response ===");
  Serial.println("Sending to Gemini AI...");

  HTTPClient http;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-goog-api-key", GEMINI_API_KEY);

  // Create JSON payload
  DynamicJsonDocument doc(2048);
  JsonArray contents = doc.createNestedArray("contents");
  JsonObject content = contents.createNestedObject();
  JsonArray parts = content.createNestedArray("parts");
  JsonObject part = parts.createNestedObject();
  part["text"] = transcript;

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.println("Sending request to Gemini...");
  int httpCode = http.POST(jsonString);

  if (httpCode > 0)
  {
    Serial.printf("[HTTP] POST... code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();

      // Parse the JSON response
      DynamicJsonDocument responseDoc(8192);
      DeserializationError error = deserializeJson(responseDoc, payload);

      if (error)
      {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        Serial.println("Raw response:");
        Serial.println(payload);
      }
      else
      {
        // Extract the AI response text
        if (responseDoc.containsKey("candidates") &&
            responseDoc["candidates"].size() > 0 &&
            responseDoc["candidates"][0].containsKey("content") &&
            responseDoc["candidates"][0]["content"].containsKey("parts") &&
            responseDoc["candidates"][0]["content"]["parts"].size() > 0 &&
            responseDoc["candidates"][0]["content"]["parts"][0].containsKey("text"))
        {
          String aiResponse = responseDoc["candidates"][0]["content"]["parts"][0]["text"];

          Serial.println("\n=== AI RESPONSE ===");
          Serial.println(aiResponse);
          Serial.println("===================\n");

            // Speak the AI response using ElevenLabs
            speakWithElevenLabs(aiResponse);
        }
        else
        {
          Serial.println("Could not extract AI response from JSON");
          Serial.println("Raw response:");
          Serial.println(payload);
        }
      }
    }
    else
    {
      Serial.println("Gemini API did not return HTTP 200 OK.");
      String errorResponse = http.getString();
      Serial.println("Error response:");
      Serial.println(errorResponse);
    }
  }
  else
  {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

void transcribeLatestRecording()
{
  if (recording || playing)
  {
    Serial.println("Cannot transcribe while recording or playing!");
    return;
  }

  // Find latest .wav file
  File root = SD.open("/");
  String latestFileName = "";
  unsigned long latestTime = 0;

  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory() && String(file.name()).endsWith(".wav"))
    {
      String fileName = String(file.name());
      int startPos = fileName.indexOf("_") + 1;
      int endPos = fileName.indexOf(".wav");
      if (startPos > 0 && endPos > startPos)
      {
        unsigned long fileTime = fileName.substring(startPos, endPos).toInt();
        if (fileTime > latestTime)
        {
          latestTime = fileTime;
          if (fileName.startsWith("/"))
          {
            latestFileName = fileName;
          }
          else
          {
            latestFileName = "/" + fileName;
          }
        }
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  if (latestFileName == "")
  {
    Serial.println("No audio files found!");
    return;
  }

  Serial.println("Attempting to transcribe: " + latestFileName);

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected");
    return;
  }

  // Use the improved transcription method from main.txt
  String transcript = SpeechToText_Deepgram(latestFileName);

  if (transcript.length() > 0 && transcript != "")
  {
    Serial.println("\n=== TRANSCRIPT ===");
    Serial.println(transcript);
    Serial.println("==================");

    // Convert transcript to lowercase for case-insensitive comparison
    String lowerTranscript = transcript;
    lowerTranscript.toLowerCase();

    // Check for "on" or "off" commands
    if (lowerTranscript.indexOf("on") != -1)
    {
      Serial.println("Voice command detected: ON");
      //pinMode(ATMEGA_CTRL_PIN, OUTPUT);
      digitalWrite(ATMEGA_CTRL_PIN, HIGH);
      Serial.println("Sent logic 1 to ATmega32 (pin 40)");
      return; // Exit function without calling Gemini
    }
    else if (lowerTranscript.indexOf("off") != -1)
    {
      Serial.println("Voice command detected: OFF");
      //pinMode(ATMEGA_CTRL_PIN, OUTPUT);
      digitalWrite(ATMEGA_CTRL_PIN, LOW);
      Serial.println("Sent logic 0 to ATmega32 (pin 40)");
      return; // Exit function without calling Gemini
    }

    // If no "on" or "off" detected, proceed with normal AI response

    // Generate AI response
    generateGeminiResponse(transcript);

    
  }
  else
  {
    Serial.println("Transcript is empty or transcription failed.");
  }
}


void speakWithElevenLabs(String text)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot use ElevenLabs.");
    return;
  }

  Serial.println("\n=== Converting Text to Speech with ElevenLabs ===");

  HTTPClient http;
  String url = "https://api.elevenlabs.io/v1/text-to-speech/JBFqnCBsd6RMkjVDRZzb?output_format=pcm_16000";

  http.begin(url);
  http.addHeader("xi-api-key", "sk_d8bb7b04566a2d62e2ae54552444167017874714a01e65d0");
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(1024);
  doc["text"] = text;
  doc["model_id"] = "eleven_multilingual_v2";

  String requestBody;
  serializeJson(doc, requestBody);

  int httpCode = http.POST(requestBody);

  if (httpCode == HTTP_CODE_OK)
  {
    WiFiClient *stream = http.getStreamPtr();

    String filename = "/tts_" + String(millis()) + ".wav";
    File outFile = SD.open(filename, FILE_WRITE);

    if (!outFile)
    {
      Serial.println("Failed to create file on SD!");
      http.end();
      return;
    }

    Serial.println("Writing WAV header placeholder...");
    for (int i = 0; i < 44; i++) outFile.write((byte)0); // Reserve header

    uint32_t audioLength = 0;

    while (http.connected() && stream->available())
    {
      uint8_t buffer[512];
      int len = stream->readBytes(buffer, sizeof(buffer));
      outFile.write(buffer, len);
      audioLength += len;
    }

    writeWavHeader(outFile, 16000, 16, 1, audioLength); // Mono 16-bit WAV
    outFile.close();
    http.end();

    Serial.println("TTS audio saved. Playing...");
    delay(500);
    playLatestRecording(); // Play immediately
  }
  else
  {
    Serial.printf("TTS request failed: %d\n", httpCode);
    Serial.println(http.getString());
    http.end();
  }
}


void stopPlayback()
{
  if (!playing)
  {
    Serial.println("Not playing!");
    return;
  }

  playing = false;
  audioFile.close();
  Serial.println("Playback stopped!");
}

void listFiles()
{
  Serial.println("Files on SD card:");
  File root = SD.open("/");
  File file = root.openNextFile();

  while (file)
  {
    if (!file.isDirectory())
    {
      Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
}

void testTone()
{
  Serial.println("Playing test tone for 3 seconds...");
  playing = true;

  // Generate a simple 1kHz sine wave
  for (int j = 0; j < 3000 && playing; j++) // 3 seconds
  {
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      // Generate 1kHz sine wave at 16kHz sample rate
      float sample = sin(2.0 * PI * 1000.0 * (j * BUFFER_SIZE + i) / SAMPLE_RATE);
      audioBuffer[i] = (int16_t)(sample * 8000); // Scale to 16-bit
    }

    size_t bytes_written = 0;
    esp_err_t result = i2s_write(I2S_SPK_PORT, audioBuffer, sizeof(audioBuffer), &bytes_written, portMAX_DELAY);

    if (result != ESP_OK)
    {
      Serial.printf("ERROR: %s\n", esp_err_to_name(result));
      break;
    }

    // Check for stop command
    if (Serial.available())
    {
      char command = Serial.read();
      if (command == 'q' || command == 'Q')
      {
        break;
      }
    }

    delay(1);
  }

  playing = false;
  Serial.println("Test tone finished!");
}

void deleteAllFiles()
{
  if (recording || playing)
  {
    Serial.println("Cannot delete files while recording or playing!");
    return;
  }

  Serial.println("WARNING: This will delete ALL audio files!");
  Serial.println("Press 'y' to confirm or any other key to cancel...");

  // Wait for confirmation
  unsigned long startTime = millis();
  while (!Serial.available() && (millis() - startTime) < 10000) // 10 second timeout
  {
    delay(100);
  }

  if (!Serial.available())
  {
    Serial.println("Timeout - operation cancelled");
    return;
  }

  char confirm = Serial.read();
  if (confirm != 'y' && confirm != 'Y')
  {
    Serial.println("Operation cancelled");
    return;
  }

  Serial.println("Deleting all audio files...");

  File root = SD.open("/");
  File file = root.openNextFile();
  int deletedCount = 0;
  int totalSize = 0;

  while (file)
  {
    String fileName = String(file.name());
    int fileSize = file.size();
    file.close();

    if (fileName.endsWith(".raw") || fileName.endsWith(".wav"))
    {
      String fullPath = "/" + fileName;
      if (fileName.startsWith("/"))
      {
        fullPath = fileName;
      }

      Serial.println("Deleting: " + fullPath);

      if (SD.remove(fullPath.c_str()))
      {
        deletedCount++;
        totalSize += fileSize;
        Serial.println("  ✓ Deleted successfully");
      }
      else
      {
        Serial.println("  ✗ Failed to delete");
      }
    }

    file = root.openNextFile();
  }
  root.close();

  Serial.printf("Delete operation completed!\n");
  Serial.printf("Files deleted: %d\n", deletedCount);
  Serial.printf("Space freed: %d bytes (%.2f KB)\n", totalSize, totalSize / 1024.0);

  if (deletedCount == 0)
  {
    Serial.println("No audio files found to delete");
  }
}

// Add this function before setup()
String SpeechToText_Deepgram(String audio_filename)
{
  uint32_t t_start = millis();

  // Connect to Deepgram Server
  if (!client.connected())
  {
    Serial.println("> Initialize Deepgram Server connection ... ");
    client.setInsecure();
    if (!client.connect("api.deepgram.com", 443))
    {
      Serial.println("\nERROR - WifiClientSecure connection to Deepgram Server failed!");
      client.stop();
      return ("");
    }
    Serial.println("Done. Connected to Deepgram Server.");
  }

  // Check if AUDIO file exists, check file size
  File audioFile = SD.open(audio_filename);
  if (!audioFile)
  {
    Serial.println("ERROR - Failed to open file for reading");
    return ("");
  }
  size_t audio_size = audioFile.size();
  audioFile.close();
  Serial.println("> Audio File [" + audio_filename + "] found, size: " + String(audio_size));

  // Flush potential inbound streaming data
  String socketcontent = "";
  while (client.available())
  {
    char c = client.read();
    socketcontent += String(c);
  }

  // Send HTTPS request header to Deepgram Server
  String optional_param;
  optional_param = "?model=nova-2-general";
  optional_param += "&language=en";
  optional_param += "&smart_format=true";
  optional_param += "&numerals=true";

  client.println("POST /v1/listen" + optional_param + " HTTP/1.1");
  client.println("Host: api.deepgram.com");
  client.println("Authorization: Token " + String(DEEPGRAM_API_KEY));
  client.println("Content-Type: audio/wav");
  client.println("Content-Length: " + String(audio_size));
  client.println(); // header complete

  Serial.println("> POST Request to Deepgram Server started, sending WAV data now ...");

  // Read and send audio file in chunks
  File file = SD.open(audio_filename, FILE_READ);
  const size_t bufferSize = 1024;
  uint8_t buffer[bufferSize];
  size_t bytesRead;
  while (file.available())
  {
    bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead > 0)
    {
      client.write(buffer, bytesRead);
    }
  }
  file.close();
  Serial.println("> All bytes sent, waiting for Deepgram transcription");

  // Wait for Deepgram Server response (timeout after 12 seconds)
  String response = "";
  uint32_t timeout = 12000; // 12 seconds
  uint32_t startWait = millis();

  while (response == "" && (millis() - startWait) < timeout)
  {
    while (client.available())
    {
      char c = client.read();
      response += String(c);
    }
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  if ((millis() - startWait) >= timeout)
  {
    Serial.println("*** TIMEOUT ERROR - forced TIMEOUT after 12 seconds ***");
  }

  // Close connection
  client.stop();

  // Parse JSON response
  Serial.println(response);
  String transcription = json_object(response, "\"transcript\":");


  uint32_t t_end = millis();
  Serial.println("=> TOTAL Duration [sec]: " + String((float)((t_end - t_start)) / 1000));
  Serial.println("=> Transcription: [" + transcription + "]");

  return transcription;
}

// Add JSON parsing helper function
String json_object(String input, String element)
{
  String content = "";
  int pos_start = input.indexOf(element);
  if (pos_start > 0)
  {
    pos_start += element.length();
    int pos_end = input.indexOf(",\"", pos_start);
    if (pos_end > pos_start)
    {
      content = input.substring(pos_start, pos_end);
    }
    content.trim();
    if (content.startsWith("\""))
    {
      content = content.substring(1, content.length() - 1);
    }
  }
  return content;
}

void loop()
{
  // Check for serial commands
  if (Serial.available())
  {
    char command = Serial.read();
    switch (command)
    {
    case 's':
    case 'S':
      startRecording();
      break;
    case 'x':
    case 'X':
      stopRecording();
      break;
    case 'l':
    case 'L':
      listFiles();
      break;
    case 'p':
    case 'P':
      playLatestRecording();
      break;
    case 'q':
    case 'Q':
      stopPlayback();
      break;
    case 't':
    case 'T':
      testTone();
      break;
    case 'd':
    case 'D':
      deleteAllFiles();
      break;
    case 'c':
    case 'C':
      transcribeLatestRecording();
      break;
    case 'b':
    case 'B':
      digitalWrite(ATMEGA_CTRL_PIN, HIGH);
      Serial.println("Sent logic 1 to ATmega32 (pin 40)");
      break;
    case 'n':
    case 'N':
      digitalWrite(ATMEGA_CTRL_PIN, LOW);
      Serial.println("Sent logic 0 to ATmega32 (pin 40)");
      break;
    }
  }

  // Recording loop - using global buffer
  if (!playing)
  {
    size_t bytes_read = 0;
    esp_err_t result = i2s_read(I2S_MIC_PORT, audioBuffer, sizeof(audioBuffer), &bytes_read, 100);

    if (result == ESP_OK && bytes_read > 0)
    {
      // Calculate audio level
      int samples_read = bytes_read / sizeof(int16_t);
      long sum = 0;
      for (int i = 0; i < samples_read; i++)
      {
        sum += audioBuffer[i] * audioBuffer[i];
      }

      float rms = sqrt(sum / samples_read);
      int level = map(rms, 0, 4000, 0, 10); // Reduced scale

      // Display audio level (less frequent)
      if (level > 1 && millis() % 200 < 50) // Only show every 200ms
      {
        Serial.print(recording ? "REC " : "    ");
        Serial.print("Audio: ");
        for (int i = 0; i < level && i < 10; i++)
        {
          Serial.print("█");
        }
        Serial.printf(" (%.0f)\n", rms);
      }

      // Save to SD card if recording
      if (recording)
      {
        audioFile.write((uint8_t *)audioBuffer, bytes_read);

        if (millis() - recordStartTime > RECORD_TIME * 1000)
        {
          stopRecording();
        }
      }
    }
  }

  delay(10); // Reduced delay
}



// [env:esp32-s3-devkitc-1]
// platform = espressif32
// board = esp32-s3-devkitc-1
// framework = arduino
// monitor_speed = 115200
// monitor_filters = esp32_exception_decoder
// build_flags = 
//     -DCORE_DEBUG_LEVEL=3
//     -DARDUINO_USB_CDC_ON_BOOT=1
//     -DCONFIG_ARDUINO_LOOP_STACK_SIZE=32768
//     -DCONFIG_FREERTOS_UNICORE=1
//     -DCONFIG_ESP_MAIN_TASK_STACK_SIZE=32768
// lib_deps = 
//     ArduinoJson