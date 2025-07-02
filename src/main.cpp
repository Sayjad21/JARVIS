#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>

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
#define RECORD_TIME 10 // Record for 10 seconds
#define BUFFER_SIZE 512  // Reduced from 1024 to 512

File audioFile;
bool recording = false;
bool playing = false;
unsigned long recordStartTime = 0;

// Allocate buffers in global memory instead of stack
int16_t audioBuffer[BUFFER_SIZE];
int32_t stereoBuffer[BUFFER_SIZE * 2];

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

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32-S3 I2S Audio Recorder + Player ===");
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
  Serial.println();
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
      .dma_buf_count = 4,  // Reduced from 8 to 4
      .dma_buf_len = 512,  // Reduced from 1024 to 512
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
  };

  i2s_pin_config_t mic_pin_config = {
      .bck_io_num = I2S_MIC_SCK_PIN,
      .ws_io_num = I2S_MIC_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_MIC_SD_PIN
  };

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
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Changed to mono for MAX98357A
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
  };

  i2s_pin_config_t spk_pin_config = {
      .bck_io_num = I2S_SPK_BCLK_PIN,
      .ws_io_num = I2S_SPK_LRC_PIN,
      .data_out_num = I2S_SPK_DIN_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE
  };

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

void startRecording()
{
  if (recording || playing)
  {
    Serial.println("Cannot record while playing or already recording!");
    return;
  }

  String filename = "/audio_" + String(millis()) + ".raw";

  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile)
  {
    Serial.println("ERROR: Failed to create audio file!");
    return;
  }

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
    if (!file.isDirectory() && String(file.name()).endsWith(".raw"))
    {
      String fileName = String(file.name());
      int startPos = fileName.indexOf("_") + 1;
      int endPos = fileName.indexOf(".raw");
      if (startPos > 0 && endPos > startPos)
      {
        unsigned long fileTime = fileName.substring(startPos, endPos).toInt();
        if (fileTime > latestTime)
        {
          latestTime = fileTime;
          if (fileName.startsWith("/")) {
            latestFileName = fileName;
          } else {
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
    int bytesRead = audioFile.read((uint8_t*)audioBuffer, sizeof(audioBuffer));
    
    if (bytesRead > 0)
    {
      size_t bytes_written = 0;
      esp_err_t result = i2s_write(I2S_SPK_PORT, audioBuffer, bytesRead, &bytes_written, portMAX_DELAY);

      if (result != ESP_OK)
      {
        Serial.printf("ERROR writing to I2S: %s\n", esp_err_to_name(result));
        break;
      }
      
      Serial.print(".");  // Progress indicator
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
  for (int j = 0; j < 3000 && playing; j++)  // 3 seconds
  {
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      // Generate 1kHz sine wave at 16kHz sample rate
      float sample = sin(2.0 * PI * 1000.0 * (j * BUFFER_SIZE + i) / SAMPLE_RATE);
      audioBuffer[i] = (int16_t)(sample * 8000);  // Scale to 16-bit
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
    
    if (fileName.endsWith(".raw"))
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
      int level = map(rms, 0, 4000, 0, 10);  // Reduced scale

      // Display audio level (less frequent)
      if (level > 1 && millis() % 200 < 50)  // Only show every 200ms
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
        audioFile.write((uint8_t*)audioBuffer, bytes_read);

        if (millis() - recordStartTime > RECORD_TIME * 1000)
        {
          stopRecording();
        }
      }
    }
  }

  delay(10);  // Reduced delay
}