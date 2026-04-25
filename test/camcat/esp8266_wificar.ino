#define LASER  D2 
#define ENA    D3          // Enable/speed motors Right          GPIO 5(D3)

#define ENB    D4          // Enable/speed motors Left           GPIO 4(D4)

#define IN_1  D5          // L298N in1 motors Right             GPIO14(D5)
#define IN_2  D6          // L298N in2 motors Right             GPIO12(D6)
#define IN_3  D7          // L298N in3 motors Left              GPIO13(D7)
#define IN_4  D8          // L298N in4 motors Left              GPIO15(D8)
//in1 in4 对应绿线和红线，控制前进
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
 
ESP8266WiFiMulti wifiMulti;
ESP8266WebServer server(80);
String webPage = ""; 

/* your car speed */
int speedCar = 450;         // 0 - 1023.
int speedInterval = 90;     //速度间隔
int speedTrun = 1.5;        //转弯系数
  
void handleRoot() {
  server.send(200, "text/html", "<h1>You are connected</h1>");
}
 
void setup() {
  
  //打开串口输出调试信息
  Serial.begin(115200);//注意波特率
 
  wifiMulti.addAP("DESKTOP_wang", "wztdewifi"); // 将需要连接的一系列WiFi ID和密码输入这里
  wifiMulti.addAP("倒卖斐济杯获无妻徒刑", "wztdewifi"); // ESP8266-NodeMCU再启动后会扫描当前网络
  //wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3"); // 环境查找是否有这里列出的WiFi ID。如果有
  Serial.println("Connecting ...");                            // 则尝试使用此处存储的密码进行连接。
  
  int i = 0;                                 
  while (wifiMulti.run() != WL_CONNECTED) {  // 此处的wifiMulti.run()是重点。通过wifiMulti.run()，NodeMCU将会在当前
    delay(1000);                             // 环境中搜索addAP函数所存储的WiFi。如果搜到多个存储的WiFi那么NodeMCU
    Serial.print(i++); Serial.print(' ');    // 将会连接信号最强的那一个WiFi信号。
  }                                          // 一旦连接WiFI成功，wifiMulti.run()将会返回“WL_CONNECTED”。这也是
                                             // 此处while循环判断是否跳出循环的条件。
  // WiFi连接成功后将通过串口监视器输出连接成功信息 
  Serial.println('\n');                     // WiFi连接成功后
  Serial.print("Connected to ");            // NodeMCU将通过串口监视器输出。
  Serial.println(WiFi.SSID());              // 连接的WiFI名称
  Serial.print("IP address:\t");            // 以及
  Serial.println(WiFi.localIP());           // NodeMCU的IP地址
  
  webPage += "<div align=\"center\"><h1>ESP8266 CAR</h1>";
  webPage += "<a href=\"FL\"><button style=\"height:200px;width:200px\"><font size=\"20\">FL</font></button></a>";
  webPage += "<a href=\"F2\"><button style=\"height:200px;width:200px\"><font size=\"20\">FW</font></button></a>";
  webPage += "<a href=\"FR\"><button style=\"height:200px;width:200px\"><font size=\"20\">FR</font></button></a><br>";
  webPage += "<a href=\"L2\"><button style=\"height:200px;width:200px\"><font size=\"20\">LT</font></button></a>";
  webPage += "<a href=\"S2\"><button style=\"height:200px;width:200px\"><font size=\"20\">ST</font></button></a>";
  webPage += "<a href=\"R2\"><button style=\"height:200px;width:200px\"><font size=\"20\">RT</font></button></a><br>";
  webPage += "<a href=\"BL\"><button style=\"height:200px;width:200px\"><font size=\"20\">BL</font></button></a>";
  webPage += "<a href=\"B2\"><button style=\"height:200px;width:200px\"><font size=\"20\">BC</font></button></a>";
  webPage += "<a href=\"BR\"><button style=\"height:200px;width:200px\"><font size=\"20\">BR</font></button></a></div>";
  webPage += "<div align=\"center\"><h1>SPEED CHANGE</h1>";
  webPage += "<a href=\"SE1\"><button style=\"height:200px;width:200px\"><font size=\"20\">1</font></button></a>";
  webPage += "<a href=\"SE2\"><button style=\"height:200px;width:200px\"><font size=\"20\">2</font></button></a>";
  webPage += "<a href=\"SE3\"><button style=\"height:200px;width:200px\"><font size=\"20\">3</font></button></a><br>";
  webPage += "<a href=\"SE4\"><button style=\"height:200px;width:200px\"><font size=\"20\">4</font></button></a>";
  webPage += "<a href=\"SE5\"><button style=\"height:200px;width:200px\"><font size=\"20\">5</font></button></a>";
  webPage += "<a href=\"SE6\"><button style=\"height:200px;width:200px\"><font size=\"20\">6</font></button></a><br>";
  webPage += "<a href=\"SE7\"><button style=\"height:200px;width:200px\"><font size=\"20\">7</font></button></a>";
  webPage += "<a href=\"SE8\"><button style=\"height:200px;width:200px\"><font size=\"20\">8</font></button></a>";
  webPage += "<a href=\"SE9\"><button style=\"height:200px;width:200px\"><font size=\"20\">9</font></button></a>";
 
  server.on("/", []() {
    server.send(200, "text/html", webPage);
  });
  server.on("/FL", []() {
    server.send(200, "text/html", webPage);
    FL();
  });
  server.on("/F2", []() {
    server.send(200, "text/html", webPage);
    F2();
  });
  server.on("/FR", []() {
    server.send(200, "text/html", webPage);
    FR();
  });
  server.on("/L2", []() {
    server.send(200, "text/html", webPage);
    L2();
  });
  server.on("/S2", []() {
    server.send(200, "text/html", webPage);
    S2();
  });
  server.on("/R2", []() {
    server.send(200, "text/html", webPage);
    R2();
  });
  server.on("/BL", []() {
    server.send(200, "text/html", webPage);
    BL();
  });
  server.on("/B2", []() {
    server.send(200, "text/html", webPage);
    B2();
  });
  server.on("/BR", []() {
    server.send(200, "text/html", webPage);
    BR();
  });
 
  server.on("/SE1", []() {
    server.send(200, "text/html", webPage);
    CS(1);
  });
  server.on("/SE2", []() {
    server.send(200, "text/html", webPage);
    CS(2);
  });
  server.on("/SE3", []() {
    server.send(200, "text/html", webPage);
    CS(3);
  });
  server.on("/SE4", []() {
    server.send(200, "text/html", webPage);
    CS(4);
  });
  server.on("/SE5", []() {
    server.send(200, "text/html", webPage);
    CS(5);
  });
  server.on("/SE6", []() {
    server.send(200, "text/html", webPage);
    CS(6);
  });
  server.on("/SE7", []() {
    server.send(200, "text/html", webPage);
    CS(7);
  });
  server.on("/SE8", []() {
    server.send(200, "text/html", webPage);
    CS(8);
  });
  server.on("/SE9", []() {
    server.send(200, "text/html", webPage);
    CS (9);
  });
 
  server.begin();
  Serial.println("HTTP server started");
  
  // preparing GPIOs
  pinMode(ENA, OUTPUT);
  //digitalWrite(ENA, 0);
  pinMode(ENB, OUTPUT);
  //digitalWrite(ENB, 0);
  pinMode(IN_1, OUTPUT);
  // digitalWrite(IN_1, 0);
  pinMode(IN_2, OUTPUT);
  // digitalWrite(IN_2, 0);
  pinMode(IN_3, OUTPUT);
  // digitalWrite(IN_3, 0);
  pinMode(IN_4, OUTPUT);
  // digitalWrite(IN_4, 0);

}
 
/*******************************************/
/*
  1+ 2- EA on right  
& 4+ 3- EB on left 
  to move forward*/

void F2() {//forward
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar);
  digitalWrite(IN_1, 1);
  digitalWrite(IN_2, 0);
 
  digitalWrite(IN_3, 0);
  digitalWrite(IN_4, 1);
 
}
  
void B2() {//backward
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar);
  digitalWrite(IN_1, 0);
  digitalWrite(IN_2, 1);
 
  digitalWrite(IN_3, 1);
  digitalWrite(IN_4, 0);
}
 
void L2() {//left steering
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar);
  digitalWrite(IN_1, 1);
  digitalWrite(IN_2, 0);
 
  digitalWrite(IN_3, 1);
  digitalWrite(IN_4, 0);
}
  
void R2() {//right steering
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar);
  digitalWrite(IN_1, 0);
  digitalWrite(IN_2, 1);
 
  digitalWrite(IN_3, 0);
  digitalWrite(IN_4, 1);
}
 
void S2() {//brake
  digitalWrite(IN_1, 0);
  digitalWrite(IN_2, 0);
  digitalWrite(IN_3, 0);
  digitalWrite(IN_4, 0);
}

/*beyond is designed for my first ESP8266_CAR with omni_wheels */ 

void FL() {//Left Forward
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar / speedTrun);
  digitalWrite(IN_1, 1);
  digitalWrite(IN_2, 0);
 
  digitalWrite(IN_3, 0);
  digitalWrite(IN_4, 0);
}
 
void FR() {//Right Forward
  analogWrite(ENA, speedCar / speedTrun);
  analogWrite(ENB, speedCar);
  digitalWrite(IN_1, 0);
  digitalWrite(IN_2, 0);
 
  digitalWrite(IN_3, 0);
  digitalWrite(IN_4, 1);
}

void BL() {//Left Backward
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar / speedTrun);
  digitalWrite(IN_1, 0);
  digitalWrite(IN_2, 1);
 
  digitalWrite(IN_3, 0);
  digitalWrite(IN_4, 0);
}

void BR() {//Right Backward
  analogWrite(ENA, speedCar / speedTrun);
  analogWrite(ENB, speedCar);
  digitalWrite(IN_1, 0);
  digitalWrite(IN_2, 0);
 
  digitalWrite(IN_3, 1);
  digitalWrite(IN_4, 0);
}
 
/*Simulation of rudder steering*/
 
void CS(int NUM) {
  speedCar = 1023 - (9 - NUM) * speedInterval;
  analogWrite(ENA, speedCar);
  analogWrite(ENB, speedCar);
}
 
/*******************************************/
 
void loop() {
  server.handleClient();
}
