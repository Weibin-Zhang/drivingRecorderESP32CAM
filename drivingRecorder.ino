#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "SD_MMC.h"
const char* ssid = "CAMERA2";//wifi的账号密码
const char* password = "12345678";
const int ZHESHI_LED = 33; //指示灯 WiFi连接成功点亮
bool cameraRunning = true;//是否开启摄像头
bool flag = true;//是否为行驶状态
bool flagSave = true;//是否存储
int i = 0;//计数
WebServer server(80);
const char* ntpServer = "cn.pool.ntp.org";  //pool.ntp.org为获取时间得接口，可以尝试更多得接口。比如微软的time.windows.com，美国国家标准与技术研究院的time.nist.gov
const long  gmtOffset_sec = 8*60*60;//这里采用UTC计时，中国为东八区，就是 8*60*60
const int   daylightOffset_sec = 0*60*60;
const int IN = 3;//输入引脚
unsigned long lastMovementTime = 0;
//负责http传输图像
void handleCapture() {
  if(cameraRunning && flag){
    flagSave = false;
    camera_fb_t * fb = esp_camera_fb_get();
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    flagSave = true;
  }
}
//用于测试连接
void handleTest() {
  flagSave = false;
  server.send(200, "text/plain", "this is esp32cam, qianduan");
  flagSave = true;
}
void handleFileList() {
  flagSave = false;
  String html = "<html><body>";
  String response = "";
  // 获取SD卡根目录下的所有文件和文件夹
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    response += file.name();
    response += "\n";
    html += "<a href='" + String(file.name()) + "'>" + String(file.name()) + "</a><br/>";
    file = root.openNextFile();
  }
//  html += "<a href='" + response + "'>" + response + "</a><br/>";
  html += "</body></html>";
//  server.send(200, "text/plain", response);
  server.send(200, "text/html", html);
  flagSave = true;
}
//其他网络后缀
void handleFileRead() {
  if(!flag){
    //只有停车状态才允许访问
    String path = server.uri();
    // 判断请求的是文件还是文件夹
    if (SD_MMC.exists(path)) {
      // 如果是文件夹，则显示该文件夹下的所有文件和文件夹
      if (SD_MMC.open(path).isDirectory()) {
        String response = "";
        String html = "<html><body>";
        html += "<a href='" + String(path) + "'>" + path + "</a><br/>";
        File dir = SD_MMC.open(path);
        File file = dir.openNextFile();
        while (file) {
          response += String(file.name());
          response += "\n";
          String filePath = path + "/" + String(file.name());
          html += "<a href='" + filePath + "'>" + String(file.name()) + "</a><br/>";
          file = dir.openNextFile();
        }
        html += "</body></html>";
//        server.send(200, "text/plain", response);
        server.send(200, "text/html", html);
      }
      // 如果是文件，则读取并发送该文件的内容
      else {
        File file = SD_MMC.open(path);
        server.streamFile(file, "image/jpeg");
        file.close();
      }
    }
    // 如果请求的文件或文件夹不存在，则返回404错误
    else {
      server.send(404, "text/plain", path + "File not found000");
    }
  }else{
    server.send(404,"text/plain", "please don't visit SDcard when driving");
  }

}
//摄像头配置
static camera_config_t camera_config = {
    .pin_pwdn = 32,
    .pin_reset = -1,
    .pin_xclk = 0,
    .pin_sscb_sda = 26,
    .pin_sscb_scl = 27,
    .pin_d7 = 35,
    .pin_d6 = 34,
    .pin_d5 = 39,
    .pin_d4 = 36,
    .pin_d3 = 21,
    .pin_d2 = 19,
    .pin_d1 = 18,
    .pin_d0 = 5,
    .pin_vsync = 25,
    .pin_href = 23,
    .pin_pclk = 22,    
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,    
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 10,   //图像质量   0-63  数字越小质量越高
    .fb_count = 1,
};
//初始化摄像头
esp_err_t camera_init() {
    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.println("Camera Init Failed!");
        return err;
    }
    return ESP_OK;
}
//初始化网络连接
bool wifi_init(const char* ssid,const char* password ){
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); //关闭STA模式下wifi休眠，提高响应速度
  WiFi.begin(ssid, password);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 10) {
      delay(500);
  }
  if (i == 11) {
    digitalWrite(ZHESHI_LED,HIGH);  //网络连接失败 熄灭指示灯
    return false;
  }
  digitalWrite(ZHESHI_LED,LOW);  //网络连接成功 点亮指示灯
  return true;
}
//初始化SD卡
void sd_init()
{
  if(!SD_MMC.begin()){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }
  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
    } 
  else if(cardType == CARD_SD){  Serial.println("SDSC");  }
  else if(cardType == CARD_SDHC){  Serial.println("SDHC");  } 
  else {  Serial.println("UNKNOWN");  }
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);    //获取SD卡大小，大小单位是MB
  Serial.printf("SD 卡容量大小: %lluMB\n", cardSize);  
}
//初始化sd卡存储路径、文件名
char tian[20] = {0};
char fen[20] = {0};
char tu[40] = "test.jpg";
char folderName[20] = {0};
char filename[40] = "test.jpg";
//存图进sd卡
void saveCameraFramesToSD(){
  //获取时间
  time_t mytime = time(nullptr);
  struct tm *timeinfo;
  timeinfo = localtime(&mytime);
  sprintf(folderName, "/%04d_%02d_%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  sprintf(tian, "/%04d_%02d_%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  if(!SD_MMC.exists(folderName)){
    SD_MMC.mkdir(folderName);
  }
  sprintf(filename, "/%02d_%02d_%02d.jpg", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  sprintf(fen, "/%02d_%02d", timeinfo->tm_hour, timeinfo->tm_min);
  String filepath = String(folderName) + "/" + String(filename);
  if(cameraRunning){
    File file = SD_MMC.open(filepath, FILE_WRITE);
    if(!file){
      Serial.println("文件创建失败");
    }else{
      // Write image data to file
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Error getting camera frame buffer");
        return;
      }
      file.write(fb->buf, fb->len);
      file.close();
      // Release the frame buffer
      esp_camera_fb_return(fb);
    }
  }
}
char lastDay = {0};
char currentDay[20] = {0};
char lastMin = {0};
char currentMin[20] = {0};
char imgName[40] = {0};
////将图片分级存入SD卡
//void saveImgToSD(){
//  for(int i = 0; i < 10; i++){
//      //获取时间
//    time_t mytime = time(nullptr);
//    struct tm * timeinfo;
//    timeinfo = localtime(&mytime);
//    sprintf(currentDay, "/%04d_%02d_%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
//    sprintf(currentMin, "%02d_%02d", timeinfo->tm_hour, timeinfo->tm_min);
//    sprintf(imgName, "%02d_%01d.jpg", timeinfo->tm_sec, i);
//    if(!SD_MMC.exists(currentDay)){
//      SD_MMC.mkdir(currentDay);
//    }
//    String folderMin = String(currentDay) + "/" + String(currentMin);
//    if(!SD_MMC.exists(folderMin)){
//      SD_MMC.mkdir(folderMin);
//    }
//    String imgPath = folderMin + "/" + imgName;
//    if(cameraRunning && (i % 2 == 0)){
//      File file = SD_MMC.open(imgPath, FILE_WRITE);
//      if(!file){
//        Serial.println("文件创建失败");
//      }else{
//        camera_fb_t * fb = esp_camera_fb_get();
//        if(!fb){
//          Serial.println("Error getting camera frame buffer");
//          return;
//        }
//        file.write(fb->buf, fb->len);
//        file.close();
//        esp_camera_fb_return(fb);
//      }
//    }
//  }
//}
//将图片分级存入SD卡
void saveImgToSD(){
      //获取时间
  time_t mytime = time(nullptr);
  i++;
  if(i>=10){
    i = 0;
  }
  struct tm * timeinfo;
  timeinfo = localtime(&mytime);
  sprintf(currentDay, "/%04d_%02d_%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  sprintf(currentMin, "%02d_%02d", timeinfo->tm_hour, timeinfo->tm_min);
  sprintf(imgName, "%02d_%01d.jpg", timeinfo->tm_sec, i);
  if(!SD_MMC.exists(currentDay)){
    SD_MMC.mkdir(currentDay);
  }
  String folderMin = String(currentDay) + "/" + String(currentMin);
  if(!SD_MMC.exists(folderMin)){
    SD_MMC.mkdir(folderMin);
  }
  String imgPath = folderMin + "/" + imgName;
  if(cameraRunning){
//  if(cameraRunning && (i % 2 == 0)){
    File file = SD_MMC.open(imgPath, FILE_WRITE);
    if(!file){
      Serial.println("文件创建失败");
    }else{
      camera_fb_t * fb = esp_camera_fb_get();
      if(!fb){
        Serial.println("Error getting camera frame buffer");
        return;
      }
      file.write(fb->buf, fb->len);
      file.close();
      esp_camera_fb_return(fb);
    }
  }
}
//开启摄像头函数
void startCamera(){
  if(!cameraRunning){
    cameraRunning = true;
    Serial.println("Starting camera...");
  }
  camera_init();
}
// 停止摄像头函数
void stopCamera() {
  if (cameraRunning) { // 如果摄像头已开启
    cameraRunning = false;
    Serial.println("Stopping camera...");
//    // 关闭HTTP服务器
//    server.close();
    // 停止摄像头
    esp_camera_deinit();
    Serial.println("Camera stopped");
  }
}
void setup(){
  Serial.begin(115200);
  pinMode(ZHESHI_LED, OUTPUT);
  digitalWrite(ZHESHI_LED, HIGH);//灭掉wifi连接显示灯
  camera_init();
  sd_init();
  wifi_init(ssid, password);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  lastMovementTime = millis();
//  WiFi.begin(ssid, password);
//  if(WiFi.status() == WL_CONNECTED){
//    server.on("/", handleRoot);
//    server.on("/test",handleTest);
//    server.begin();
//  }
  flagSave = true;
  server.on("/capture", handleCapture);//开启服务器
  server.on("/test", handleTest);
  server.on("/", handleFileList);
  server.onNotFound(handleFileRead);
  server.begin();
}
void loop(){
  if(digitalRead(IN) == 0){
    //更新最近震动时间为当前时间
    lastMovementTime = millis();
    flag = true;
  }else{
    //如果连续2分钟无震动，视为停车
    if((millis() - lastMovementTime) > (2 * 60 *1000)){
      flag = false;
    }else{
      flag = true;
    }
  }
  server.handleClient();
  WiFiClient client = server.client();
  if(flag && !client){
    saveImgToSD();
  }
}
