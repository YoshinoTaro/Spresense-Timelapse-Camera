
#include <SPI.h>
#include <SPISD.h>
#include <Camera.h>
#include <LowPower.h>
#include <RTC.h>

SpiSDClass SD(SPI5);
SpiFile infFile;
SpiFile aviFile;

static const String aviFilename = "movie.avi";
static const String infFilename = "info.txt";
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
static int16_t exposure_time = -1; // -1 is AutoExposure
static uint16_t interval_time = 60; // 60 sec

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
      while(1);
    }
    rec_frame = infFile.readStringUntil('\n').toInt();
    movi_size = infFile.readStringUntil('\n').toInt();
    exposure_time = infFile.readStringUntil('\n').toFloat();
    interval_time = infFile.readStringUntil('\n').toInt();
    Serial.println("Read Rec Frame: " + String(rec_frame));
    Serial.println("Read Movie Size: " + String(movi_size));
    Serial.println("Read Exposure Time: " + String(exposure_time));
    Serial.println("Read Interval Time: " + String(interval_time));
    infFile.close();
  } else {
    // default
    // rec_frame = 0;
    // movi_size = 0;
    // exposure_time = -1; /* 100msec */ 
    // interval_time = 60;  /* sec */
  }

   // check for recording for the first time.
  if (bc != DEEP_RTC) {
    Serial.println("Power on reset");
    if (SD.exists(aviFilename)) {
      SD.remove(aviFilename);
      rec_frame = 0;
      movi_size = 0;
      Serial.println("removed " + aviFilename);
    }
  } 
  aviFile = SD.open(aviFilename ,FILE_WRITE);
  if (!aviFile) {
    Serial.println("Movie File Open Error!");
    while(1);
  }
  
  if (rec_frame == 0) {
    Serial.println("First time: write header");
    aviFile.write(avi_header, AVIOFFSET);
    sleep(3); // wait for 3sec
  }

  Serial.println("Recording...");
  theCamera.begin();
  if (exposure_time < 0) {
    Serial.println("Exposure Auto");
    theCamera.setAutoExposure(true); // 0.1 sec    
  } else {
    Serial.println("Exposure:" + String(exposure_time/10.) + "msec");
    theCamera.setAbsoluteExposure(exposure_time); // 0.1 sec
  }
  theCamera.setStillPictureImageFormat(
      img_width
     ,img_height
     ,CAM_IMAGE_PIX_FMT_JPG);
}

void loop() {
  
  CamImage img = theCamera.takePicture();
  if (!img.isAvailable()) {
    Serial.println("faile to take a picture");
    return;
  }

  if (rec_frame != 0) {
    aviFile.seek(aviFile.size());
  }
  aviFile.write("00dc", 4);

  uint32_t jpeg_size = img.getImgSize();
  uint32_write_to_aviFile(jpeg_size);
  
  aviFile.write(img.getImgBuff() ,jpeg_size);
  movi_size += jpeg_size;
  ++rec_frame;
  theCamera.end(); // to save power consumption

  /* Spresense's jpg file is assumed to be 16bits aligned 
   * So, there's no padding operation */

  float duration_sec = 0.1; // fix 10fps for Timelapse
  float fps_in_float = 10.0f; // fix 10fps for Timelapse
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
  infFile.println(String(exposure_time));
  infFile.println(String(interval_time));
  infFile.close();
  Serial.println("Information File Update: ");
  Serial.println("Write Rec Frame: " + String(rec_frame));
  Serial.println("Write Movie Size: " + String(movi_size));

  Serial.println("Movie saved");
  Serial.println(" File size (kB): " + String(total_size));
  Serial.println(" Captured Frame: " + String(rec_frame)); 
  Serial.println(" Duration (sec): " + String(duration_sec));
  Serial.println(" Frame per sec : " + String(fps));
  Serial.println(" Max data rate : " + String(max_bytes_per_sec));

  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
  LowPower.deepSleep(interval_time); // Go to deep sleep for 60 seconds
} 
