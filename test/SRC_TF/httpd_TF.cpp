#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include "FS.h"
#include "SD_MMC.h"

bool sd_ready = false;

void setup_sd_card() {
  // 如果担心新版报错，用条件编译
#if ESP_ARDUINO_VERSION_MAJOR < 2
    sd_ready = SD_MMC.begin(true);
#else
    sd_ready = SD_MMC.begin("/sdcard", true);
#endif
}

void save_photo(camera_fb_t *fb) {
  if (!sd_ready || !fb) return;
  
  String filename = "/IMG_" + String(millis()) + "_" + String(random(100)) + ".jpg";
  
  File file = SD_MMC.open(filename.c_str(), FILE_WRITE);
  if (!file) return;
  
  size_t written = file.write(fb->buf, fb->len);
  file.close();
  
  if (written != fb->len) SD_MMC.remove(filename.c_str());  // 写失败删残片
}

esp_err_t film_handler(httpd_req_t* req) {
  char query[64];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  
  bool to_server = strstr(query, "saverealm=server");
  
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);
  
  if (to_server) {
    save_photo(fb);  // 保存到 SD
    httpd_resp_send(req, "{\"ok\":1}", 8);
  } else {
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (char*)fb->buf, fb->len);  // 发送到客户端
  }
  
  esp_camera_fb_return(fb);
  return ESP_OK;
}