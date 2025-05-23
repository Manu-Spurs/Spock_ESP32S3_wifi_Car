#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "camera_index.h"
#include "Arduino.h"
#include <WiFi.h>
#include <WebSocketsServer.h>

// Select camera model
#define CAMERA_MODEL_XIAO_ESP32S3

const char* ssid = "Esp32S3";   //Enter SSID for AP
const char* password = "88888888";   //Enter Password for AP

// GPIO pins for XIAO ESP32S3 camera
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

#define LED_GPIO_NUM      21

// GPIO Setting for car control
extern int gpLb =  D6; // Left 1  D1
extern int gpLf = D5; // Left 2   D2
extern int gpRb = D8; // Right 1  D3
extern int gpRf = D9; // Right 2  D4
//extern int gpLed =  D9; // Light  D9
extern String WiFiAddr = "";

void WheelAct(int nLf, int nLb, int nRf, int nRb);

typedef struct {
        size_t size; //number of values used for filtering
        size_t index; //current value index
        size_t count; //value count
        int sum;
        int * values; //array to be filled with values
} ra_filter_t;

typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// WebSocket server
WebSocketsServer webSocket = WebSocketsServer(81);
int carSpeed = 150;  // Default speed (0-255)

// HTML page with WebSocket support
const char* htmlHomePage PROGMEM = R"HTMLHOMEPAGE(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <style>
      .arrows {
        font-size: 40px;
        color: black;
      }
      .button {
        background-color: white;
        border-radius: 5%;
        box-shadow: 5px 5px #888888;
        width: 90px;
        height: 80px;
        margin: 5px;
      }
      .button:active {
        transform: translate(5px,5px);
        box-shadow: none;
      }
      .stop-button {
        background-color: #f44336;
        color: white;
        width: 90px;
        height: 80px;
        margin: 5px;
        border-radius: 5%;
        box-shadow: 5px 5px #888888;
      }
      .stop-button:active {
        transform: translate(5px,5px);
        box-shadow: none;
      }
      .camera-feed {
        width: 380px;
        height: 300px;
        border-radius: 10px;
        margin: 10px;
      }
      .noselect {
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
      }
      .slidecontainer {
        width: 100%;
      }
      .slider {
        -webkit-appearance: none;
        width: 90%;
        height: 15px;
        border-radius: 5px;
        background: #d3d3d3;
        outline: none;
        opacity: 0.7;
        -webkit-transition: .2s;
        transition: opacity .2s;
      }
      .slider:hover {
        opacity: 1;
      }
      .slider::-webkit-slider-thumb {
        -webkit-appearance: none;
        width: 30px;
        height: 30px;
        border-radius: 50%;
        background: #4CAF50;
        cursor: pointer;
      }
      .slider::-moz-range-thumb {
        width: 25px;
        height: 25px;
        border-radius: 50%;
        background: #4CAF50;
        cursor: pointer;
      }
      .control-table {
        width: 400px;
        margin: auto;
        table-layout: fixed;
        border-spacing: 10px;
      }
    </style>
  </head>
  <body class="noselect" align="center" style="background-color: #f0f0f0">
    <table class="control-table">
      <tr>
        <td colspan="3">
          <img id="cameraImage" class="camera-feed" src="">
        </td>
      </tr>
      <tr>
        <td></td>
        <td class="button" ontouchstart='sendCommand("go")' ontouchend='sendCommand("stop")'><span class="arrows">&#8679;</span></td>
        <td></td>
      </tr>
      <tr>
        <td class="button" ontouchstart='sendCommand("left")' ontouchend='sendCommand("stop")'><span class="arrows">&#8678;</span></td>
        <td class="stop-button" ontouchstart='sendCommand("stop")' ontouchend='sendCommand("stop")'></td>
        <td class="button" ontouchstart='sendCommand("right")' ontouchend='sendCommand("stop")'><span class="arrows">&#8680;</span></td>
      </tr>
      <tr>
        <td></td>
        <td class="button" ontouchstart='sendCommand("back")' ontouchend='sendCommand("stop")'><span class="arrows">&#8681;</span></td>
        <td></td>
      </tr>
      <tr><td colspan="3"><br></td></tr>
      <tr>
        <td style="text-align:left"><b>Speed:</b></td>
        <td colspan="2">
          <div class="slidecontainer">
            <input type="range" min="0" max="255" value="150" class="slider" id="Speed" oninput='sendCommand("speed:" + this.value)'>
          </div>
        </td>
      </tr>
      <tr>
        <td style="text-align:left"><b>Light:</b></td>
        <td colspan="2">
          <div class="slidecontainer">
            <input type="range" min="0" max="255" value="0" class="slider" id="Light" oninput='sendCommand("light:" + this.value)'>
          </div>
        </td>
      </tr>
    </table>

    <script>
      var webSocket = new WebSocket('ws://' + window.location.hostname + ':81/');
      
      webSocket.onopen = function(event) {
        console.log('WebSocket Connected');
        // Send initial values
        var speedSlider = document.getElementById("Speed");
        var lightSlider = document.getElementById("Light");
        sendCommand("speed:" + speedSlider.value);
        sendCommand("light:" + lightSlider.value);
      };
      
      webSocket.onclose = function(event) {
        console.log('WebSocket Disconnected');
        setTimeout(function() {
          window.location.reload();
        }, 2000);
      };
      
      webSocket.onmessage = function(event) {
        if (event.data instanceof Blob) {
          var imageId = document.getElementById("cameraImage");
          imageId.src = URL.createObjectURL(event.data);
        }
      };

      function sendCommand(cmd) {
        if (webSocket.readyState === WebSocket.OPEN) {
          webSocket.send(cmd);
        }
      }

      document.addEventListener('touchend', function(event) {
        event.preventDefault();
      }, { passive: false });
    </script>
  </body>
</html>
)HTMLHOMEPAGE";

void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Initialize GPIO pins
  pinMode(gpLb, OUTPUT);
  pinMode(gpLf, OUTPUT);
  pinMode(gpRb, OUTPUT);
  pinMode(gpRf, OUTPUT);
  //pinMode(gpLed, OUTPUT);

  // Initialize all pins to LOW
  digitalWrite(gpLb, LOW);
  digitalWrite(gpLf, LOW);
  digitalWrite(gpRb, LOW);
  digitalWrite(gpRf, LOW);
  //digitalWrite(gpLed, LOW);

  // Camera configuration
  camera_config_t cam_config;
  cam_config.ledc_channel = LEDC_CHANNEL_0;
  cam_config.ledc_timer = LEDC_TIMER_0;
  cam_config.pin_d0 = Y2_GPIO_NUM;
  cam_config.pin_d1 = Y3_GPIO_NUM;
  cam_config.pin_d2 = Y4_GPIO_NUM;
  cam_config.pin_d3 = Y5_GPIO_NUM;
  cam_config.pin_d4 = Y6_GPIO_NUM;
  cam_config.pin_d5 = Y7_GPIO_NUM;
  cam_config.pin_d6 = Y8_GPIO_NUM;
  cam_config.pin_d7 = Y9_GPIO_NUM;
  cam_config.pin_xclk = XCLK_GPIO_NUM;
  cam_config.pin_pclk = PCLK_GPIO_NUM;
  cam_config.pin_vsync = VSYNC_GPIO_NUM;
  cam_config.pin_href = HREF_GPIO_NUM;
  cam_config.pin_sscb_sda = SIOD_GPIO_NUM;
  cam_config.pin_sscb_scl = SIOC_GPIO_NUM;
  cam_config.pin_pwdn = PWDN_GPIO_NUM;
  cam_config.pin_reset = RESET_GPIO_NUM;
  cam_config.xclk_freq_hz = 20000000;
  cam_config.pixel_format = PIXFORMAT_JPEG;
  
  // Initialize with high specs to pre-allocate larger buffers
  if(psramFound()){
    cam_config.frame_size = FRAMESIZE_UXGA;
    cam_config.jpeg_quality = 10;
    cam_config.fb_count = 2;
  } else {
    cam_config.frame_size = FRAMESIZE_SVGA;
    cam_config.jpeg_quality = 12;
    cam_config.fb_count = 1;
  }

  // Camera init
  esp_err_t err = esp_camera_init(&cam_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Get camera sensor
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    // Disable auto exposure and gain control
    s->set_ae_level(s, 0);        // Set exposure level to 0 (manual)
    s->set_aec2(s, 0);            // Disable auto exposure DSP
    s->set_ae_level(s, 0);        // Set exposure level to 0
    s->set_aec_value(s, 300);     // Set fixed exposure value
    s->set_agc_gain(s, 0);        // Set gain to 0
    s->set_gain_ctrl(s, 0);       // Disable auto gain control
    s->set_brightness(s, 0);      // Set brightness to 0 (neutral)
    s->set_contrast(s, 0);        // Set contrast to 0 (neutral)
    s->set_saturation(s, 0);      // Set saturation to 0 (neutral)
    s->set_whitebal(s, 1);        // Enable white balance
    s->set_awb_gain(s, 1);        // Enable AWB gain
    s->set_wb_mode(s, 0);         // Set white balance mode to auto
    s->set_exposure_ctrl(s, 0);   // Disable exposure control
    s->set_aec2(s, 0);            // Disable AEC2
    s->set_gain_ctrl(s, 0);       // Disable gain control
    s->set_agc_gain(s, 0);        // Set AGC gain to 0
    s->set_gainceiling(s, (gainceiling_t)0);  // Set gain ceiling to 0

    // Fix camera orientation
    s->set_hmirror(s, 0);    // No horizontal mirror
    s->set_vflip(s, 0);      // No vertical flip
  }

  // Drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_VGA);  // Set to VGA for better performance

  // Start WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(IP);
  Serial.println("' to connect");
  WiFiAddr = IP.toString();

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Start HTTP server
  httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&camera_httpd, &http_config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
  }
}

void loop() {
  webSocket.loop();
  
  // Send camera frame if there are connected clients
  if (webSocket.connectedClients() > 0) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      webSocket.broadcastBIN(fb->buf, fb->len);
      esp_camera_fb_return(fb);
    }
  }
  
  delay(10);  // Small delay to prevent overwhelming the system
}

void WheelAct(int nLf, int nLb, int nRf, int nRb)
{
 digitalWrite(gpLf, nLf);
 digitalWrite(gpLb, nLb);
 digitalWrite(gpRf, nRf);
 digitalWrite(gpRb, nRb);
}

static ra_filter_t * ra_filter_init(ra_filter_t * filter, size_t sample_size){
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if(!filter->values){
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t * filter, int value){
    if(!filter->values){
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.printf("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    size_t fb_len = 0;
    if(fb->format == PIXFORMAT_JPEG){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        jpg_chunking_t jchunk = {req, 0};
        res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);
        fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];

    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.printf("Camera capture failed");
            res = ESP_FAIL;
        } else {
            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if(!jpeg_converted){
                    Serial.printf("JPEG compression failed");
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(fb){
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if(_jpg_buf){
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
        Serial.printf("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)"
            ,(uint32_t)(_jpg_buf_len),
            (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
            avg_frame_time, 1000.0 / avg_frame_time
        );
    }

    last_frame = 0;
    return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
    char*  buf;
    size_t buf_len;
    char variable[32] = {0,};
    char value[32] = {0,};

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if(!buf){
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
            } else {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        } else {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t * s = esp_camera_sensor_get();
    int res = 0;

    if(!strcmp(variable, "framesize")) {
        if(s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
    }
    else if(!strcmp(variable, "quality")) res = s->set_quality(s, val);
    else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
    else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
    else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
    else if(!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
    else if(!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
    else if(!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
    else if(!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
    else if(!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
    else if(!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
    else if(!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
    else if(!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
    else if(!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
    else if(!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
    else if(!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
    else if(!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
    else if(!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
    else if(!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
    else if(!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
    else if(!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
    else if(!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
    else if(!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
    else if(!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
    else {
        res = -1;
    }

    if(res){
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req){
    static char json_response[1024];

    sensor_t * s = esp_camera_sensor_get();
    char * p = json_response;
    *p++ = '{';

    p+=sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p+=sprintf(p, "\"quality\":%u,", s->status.quality);
    p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p+=sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p+=sprintf(p, "\"awb\":%u,", s->status.awb);
    p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p+=sprintf(p, "\"aec\":%u,", s->status.aec);
    p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p+=sprintf(p, "\"agc\":%u,", s->status.agc);
    p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p+=sprintf(p, "\"colorbar\":%u", s->status.colorbar);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, htmlHomePage, strlen(htmlHomePage));
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      }
      break;
    case WStype_TEXT:
      {
        // Simple command parsing without JSON
        String cmd = String((char*)payload);
        int value = 0;
        
        // Extract value if present (format: "command:value")
        int colonIndex = cmd.indexOf(':');
        if (colonIndex != -1) {
          value = cmd.substring(colonIndex + 1).toInt();
          cmd = cmd.substring(0, colonIndex);
        }
        
        if (cmd == "go") {
          WheelAct(HIGH, LOW, HIGH, LOW);
        }
        else if (cmd == "back") {
          WheelAct(LOW, HIGH, LOW, HIGH);
        }
        else if (cmd == "left") {
          WheelAct(HIGH, LOW, LOW, HIGH);
        }
        else if (cmd == "right") {
          WheelAct(LOW, HIGH, HIGH, LOW);
        }
        else if (cmd == "stop") {
          WheelAct(LOW, LOW, LOW, LOW);
        }
        else if (cmd == "speed") {
          carSpeed = value;
          // Update motor speed if needed
        }
        else if (cmd == "light") {
          //analogWrite(gpLed, value);
        }
      }
      break;
  }
}
