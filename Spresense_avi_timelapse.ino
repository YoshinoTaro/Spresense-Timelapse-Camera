#include <SPI.h>
#include <SPISD.h>
#include <Camera.h>
#include <LowPower.h>
#include <RTC.h>

SpiSDClass SD(SPI5);
SpiFile infFile;
SpiFile aviFile;
SpiFile logFile;

static const String aviFilename = "movi";
static const String infFilename = "info.txt";
static const String logFilename = "log.txt";
static const int img_width = 1280;
static const int img_height = 960;

static const uint8_t WIDTH_1 = (img_width & 0x00ff);
static const uint8_t WIDTH_2 = (img_width & 0xff00) >> 8;
static const uint8_t HEIGHT_1 = (img_height & 0x00ff);
static const uint8_t HEIGHT_2 = (img_height & 0xff00) >> 8;

static uint16_t rec_frame_addr = 0x00;
static uint16_t movi_size_addr = 0x08;
static uint16_t total_size_addr = 0x10;
static uint32_t rec_frame = 0;
static uint32_t movi_size = 0;
static uint32_t total_size = 0;
static uint16_t reset_times = 0;
static int16_t  exposure_time = -1; // -1 is AutoExposure
static uint16_t interval_time = 60; // 60 sec
static float    target_fps = 10.0f; // 10 fps
static bool     auto_white_balance = 0;
static bool     log_enable = 0;

#define TOTAL_FRAMES 300
#define AVIOFFSET 240

const char avi_header[AVIOFFSET+1] = {
  0x52, 0x49, 0x46, 0x46, 0xD8, 0x01, 0x0E, 0x00, 0x41, 0x56, 0x49, 0x20, 0x4C, 0x49, 0x53, 0x54,
  0xD0, 0x00, 0x00, 0x00, 0x68, 0x64, 0x72, 0x6C, 0x61, 0x76, 0x69, 0x68, 0x38, 0x00, 0x00, 0x00,
  0xA0, 0x86, 0x01, 0x00, 0x80, 0x66, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  WIDTH_1, WIDTH_2, 0x00, 0x00, HEIGHT_1, HEIGHT_2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54, 0x84, 0x00, 0x00, 0x00,
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x76, 0x69, 0x64, 0x73,
  0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x28, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, WIDTH_1, WIDTH_2, 0x00, 0x00, HEIGHT_1, HEIGHT_2, 0x00, 0x00,
  0x01, 0x00, 0x18, 0x00, 0x4D, 0x4A, 0x50, 0x47, 0x00, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54,
  0x10, 0x00, 0x00, 0x00, 0x6F, 0x64, 0x6D, 0x6C, 0x64, 0x6D, 0x6C, 0x68, 0x04, 0x00, 0x00, 0x00,
  0x64, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54, 0x00, 0x01, 0x0E, 0x00, 0x6D, 0x6F, 0x76, 0x69,
  0x00
};


static void inline uint32_write_to_aviFile(uint32_t v) { 
  char value = v % 0x100;
  aviFile.write(value);  
  v = v >> 8; 
  value = v % 0x100;
  aviFile.write(value);  
  v = v >> 8;
  value = v % 0x100;
  aviFile.write(value);  
  v = v >> 8; 
  value = v;
  aviFile.write(value);
}

static void recordingTerminated() {
  Serial.println("Recording Abnormal Terminated");
  aviFile.close();
  infFile.close();
  logFile.close();
  while(true) {
    digitalWrite(LED0, LOW);
    digitalWrite(LED1, LOW);
    digitalWrite(LED2, LOW);
    digitalWrite(LED3, LOW);
    usleep(500000);
    digitalWrite(LED0, HIGH);
    digitalWrite(LED1, HIGH);
    digitalWrite(LED2, HIGH);
    digitalWrite(LED3, HIGH);
    usleep(500000);
  }
}

void logPrintln(String str) {
  Serial.println(str);
  if (log_enable && logFile) {
    logFile.println(str);
  }
}

const char* boot_cause_strings[] = {
  "Power On Reset with Power Supplied",
  "System WDT expired or Self Reboot",
  "Chip WDT expired",
  "WKUPL signal detected in deep sleep",
  "WKUPS signal detected in deep sleep",
  "RTC Alarm expired in deep sleep",
  "USB Connected in deep sleep",
  "Others in deep sleep",
  "SCU Interrupt detected in cold sleep",
  "RTC Alarm0 expired in cold sleep",
  "RTC Alarm1 expired in cold sleep",
  "RTC Alarm2 expired in cold sleep",
  "RTC Alarm Error occurred in cold sleep",
  "Unknown(13)",
  "Unknown(14)",
  "Unknown(15)",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "GPIO detected in cold sleep",
  "SEN_INT signal detected in cold sleep",
  "PMIC signal detected in cold sleep",
  "USB Disconnected in cold sleep",
  "USB Connected in cold sleep",
  "Power On Reset",
};

void setup() {
  Serial.begin(115200);
  while (!SD.begin()) {
    Serial.println("Insert SD Card");
  }

  LowPower.begin();
  RTC.begin();
  bootcause_e bc = LowPower.bootCause();

  digitalWrite(LED3, HIGH);
  digitalWrite(LED2, HIGH);
  
  if (SD.exists(infFilename)) {
    infFile = SD.open(infFilename, FILE_READ);
    if (!infFile) {
      Serial.println("Information File Open Error for reading");
      recordingTerminated();
    }
    rec_frame = infFile.readStringUntil('\n').toInt();
    movi_size = infFile.readStringUntil('\n').toInt();
    total_size = infFile.readStringUntil('\n').toInt();
    exposure_time = infFile.readStringUntil('\n').toFloat();
    interval_time = infFile.readStringUntil('\n').toInt();
    target_fps = infFile.readStringUntil('\n').toFloat();
    auto_white_balance = infFile.readStringUntil('\n').toInt();
    log_enable = infFile.readStringUntil('\n').toInt();
    reset_times = infFile.readStringUntil('\n').toInt();
    infFile.close();
  } else {
    /*-- default --*/
    // rec_frame = 0;
    // movi_size = 0;
    // exposure_time = -1; /* 100msec */ 
    // interval_time = 60;  /* sec */
  }

  if (log_enable) {
    logFile = SD.open(logFilename, FILE_WRITE);
    if (!logFile) {
      Serial.println("Log File Open Error");
      recordingTerminated();
    }    
  }
  
  logPrintln("Boot Cause: " + String(boot_cause_strings[bc]));  
  logPrintln("Read Rec Frame: " + String(rec_frame));
  logPrintln("Read Movie Size: " + String(movi_size));
  logPrintln("Read Total Size: " + String(total_size));  
  logPrintln("Read Exposure Time: " + String(exposure_time));
  logPrintln("Read Interval Time: " + String(interval_time));
  logPrintln("Read Target FPS: " + String(target_fps));
  logPrintln("Read White Balance: " + String(auto_white_balance));
  logPrintln("Read Log Enable: " + String(log_enable));
  logPrintln("Read Reset Times: " + String(reset_times));
    
  // check for recording for the first time.
  if (bc != DEEP_RTC && bc != DEEP_OTHERS) {
    logPrintln("Power on reset");
    rec_frame = 0;
    movi_size = 0;
    total_size = 0;
    ++reset_times;
  }
  
  aviFilename += String(reset_times) + ".avi";
  aviFile = SD.open(aviFilename ,FILE_WRITE);
  if (!aviFile) {
    logPrintln("Movie File Open Error!");
    recordingTerminated();
  }
  
  if (rec_frame == 0) {
    logPrintln("First time: write header");
    aviFile.write(avi_header, AVIOFFSET);
    sleep(3); // wait for 3sec
  }
  
  Serial.println("Recording...");
  theCamera.begin();
  if (exposure_time < 0) {
    logPrintln("Exposure Auto");
    theCamera.setAutoExposure(true); // 0.1 sec    
  } else {
    logPrintln("Exposure:" + String(exposure_time/10.) + "msec");
    theCamera.setAbsoluteExposure(exposure_time); // 0.1 sec
  }
  
  if (auto_white_balance) {
    logPrintln("Auto White Balance ON");
    theCamera.setAutoWhiteBalance(true);  
  } else {
    logPrintln("Auto White Balance OFF");
    theCamera.setAutoWhiteBalance(false);      
  }
  
  theCamera.setStillPictureImageFormat(
      img_width
     ,img_height
     ,CAM_IMAGE_PIX_FMT_JPG);
}

void loop() {
  
  CamImage img = theCamera.takePicture();
  if (!img.isAvailable()) {
    logPrintln("faile to take a picture");
    return;
  }

  logPrintln("aviFile.size() " + String(aviFile.size())); 
  aviFile.seek(aviFile.size());
  aviFile.write("00dc", 4);
  
  uint32_t jpeg_size = img.getImgSize();
  uint32_write_to_aviFile(jpeg_size);
  
  aviFile.write(img.getImgBuff() ,jpeg_size);
  movi_size += jpeg_size;
  ++rec_frame;
  theCamera.end(); // to save power consumption
  
  /* Spresense's jpg file is assumed to be 16bits aligned 
   * So, there's no padding operation */
   
  float duration_sec = 1/target_fps; // fix 10fps for Timelapse
  float fps_in_float = target_fps; // fix 10fps for Timelapse
  float us_per_frame_in_float = 1000000.0f / fps_in_float;
  uint32_t fps = round(fps_in_float);
  uint32_t us_per_frame = round(us_per_frame_in_float);
  
  /* overwrite riff file size */
  aviFile.seek(0x04);
  uint32_t total_size = movi_size + 12*rec_frame + 4;
  uint32_write_to_aviFile(total_size);

  /* overwrite hdrl */
  /* hdrl.avih.us_per_frame */
  aviFile.seek(0x20);
  uint32_write_to_aviFile(us_per_frame);
  uint32_t max_bytes_per_sec = movi_size * fps / rec_frame;
  aviFile.seek(0x24);
  uint32_write_to_aviFile(max_bytes_per_sec);

  /* hdrl.avih.tot_frames */
  aviFile.seek(0x30);
  uint32_write_to_aviFile(rec_frame);
  aviFile.seek(0x84);
  uint32_write_to_aviFile(fps);   

  /* hdrl.strl.list_odml.frames */
  aviFile.seek(0xe0);
  uint32_write_to_aviFile(rec_frame);
  aviFile.seek(0xe8);
  uint32_write_to_aviFile(movi_size);

  aviFile.close();

  if (SD.exists(infFilename)) SD.remove(infFilename);
  infFile = SD.open(infFilename, FILE_WRITE);
  if (!infFile) {
    Serial.println("Information File Open Error for writing");
    while(1);
  }
  
  infFile.println(String(rec_frame));
  infFile.println(String(movi_size));
  infFile.println(String(total_size));
  infFile.println(String(exposure_time));
  infFile.println(String(interval_time));
  infFile.println(String(target_fps));
  infFile.println(String(auto_white_balance));  
  infFile.println(String(log_enable));  
  infFile.println(String(reset_times));  
  infFile.close();
  
  logPrintln("Information File Update: ");
  logPrintln("Write Rec Frame: " + String(rec_frame));
  logPrintln("Write Movie Size: " + String(movi_size));
  logPrintln("Write Total Size: " + String(total_size));
  logPrintln("Movie saved");
  logPrintln(" File size (kB): " + String(total_size));
  logPrintln(" Captured Frame: " + String(rec_frame)); 
  logPrintln(" Duration (sec): " + String(duration_sec));
  logPrintln(" Frame per sec : " + String(fps));
  logPrintln(" Max data rate : " + String(max_bytes_per_sec));
  
  logFile.close();
  
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
  LowPower.deepSleep(interval_time); // Go to deep sleep for 60 seconds
} 
