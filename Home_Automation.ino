#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiMulti.h> 
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include "FS.h"
#include <base64.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

/*****************
* Created By: Rommel A. Mendiola
* Home Automation using ESP8266 with Portal
* Supports MQQT
******************/

String captivePortal_url = "mendy";


//System Variable (Do Not Change)
bool connectedToWifi = false;

String adminUser = "";
String adminPass = "";
String adminAuth = "";
String wifiSSID = "";
String wifiPASS = "";
String apSSID = "";
String apPASS = "";
String token = "";
String devHostName = "";

//ESP8266 PINS Format = {Pin#, Active, On/Off}
int Loads[6][3] = {{16,0,0},{14,0,0},{12,0,0},{13,0,0},{4,0,0},{5,0,0}};
String Loads_name[6] = {"Device","Device","Device","Device","Device","Device"};
String Loads_mqtt_commands[6] = {"","","","","",""};
//String Feeds_name[6] = {"Dev1","Dev2","Dev3","Dev4","Dev5","Dev6"};
const int WIFI_CONNECT_TIMEOUT = 10000;
const int WIFI_CONNECT_DELAY = 100;

const byte DNS_PORT = 53;
ESP8266WebServer webServer(80);
IPAddress apIP(10,10,10,1);
DNSServer dnsServer;

/************************* Adafruit.io Setup *********************************/

String MQTT_ACTIVE       = "";
String MQTT_SERVER       = ""; //Adafruit Server
String MQTT_SERVERPORT   = "";
String MQTT_USERNAME     = "";            // Username
String MQTT_KEY          = "";   // Auth Key

WiFiClient client;
PubSubClient mqtt_client(client);

void initMQTT() {
  mqtt_client.setServer(MQTT_SERVER.c_str(),strtol(MQTT_SERVERPORT.c_str(),NULL,0));
  mqtt_client.setCallback(mqtt_callback);
}

bool isValidInput(String inputstr){
    int  charlenght =  inputstr.length() + 1;
    char instr[charlenght];
    
    inputstr.toCharArray(instr,charlenght);
    for(int i = 0; i < strlen(instr); i++){
         if(isalnum(instr[i])){
             continue;
         }
         if(instr[i] == '.'){
             continue;
         }
         if(instr[i] == '_'){
             continue;
         }
         if(instr[i] != ','){
             continue;
         }
         if(instr[i] != '|'){
             continue;
         }
         return false;
    }
    return true;
}

void loopMQTT() {
  yield();
  
  if(!mqtt_client.connected()) {
    Serial.println("Connecting to MQTT Server");
    
    if(mqtt_client.connect("",MQTT_USERNAME.c_str(),MQTT_KEY.c_str())) {
      Serial.println("Connection to MQTT Successfull");

      for(int i = 0; i< 6;i++) {
        if(Loads_mqtt_commands[i] != "" && Loads[i][1] == 1) {
          mqtt_client.subscribe(Loads_mqtt_commands[i].c_str());
         Serial.println("Subscribe to: " + Loads_mqtt_commands[i]);
        }
      }
    } else {
      Serial.println("Failed to Connect [Code: ");
      Serial.print(mqtt_client.state());
      Serial.print("]");
      delay(5000);
    }
  }
  mqtt_client.loop();
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();


  for(int i = 0; i < 6;i ++ ) {
    if(String(topic) == Loads_mqtt_commands[i]) {
      
      Loads[i][2] = payload[0] == '1' ? 0 : 1;
      digitalWrite(Loads[i][0], (payload[0] == '1' ? LOW : HIGH));
      Serial.println(String(Loads[i][0]) + " Set to " + (Loads[i][2] == 1 ? "HIGH" : "LOW"));
      updateConfig();
    }
  }
}

void handleNotFound()
{
    Serial.println("preflight....");
    if (webServer.method() == HTTP_OPTIONS)
    {
        Serial.println("Preflight request....");
        webServer.sendHeader("Access-Control-Allow-Origin", "*");
        webServer.sendHeader("Access-Control-Max-Age", "10000");
        webServer.sendHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
        webServer.sendHeader("Access-Control-Allow-Headers", "*");
        webServer.sendHeader("Access-Control-Allow-Credentials", "false");
        webServer.send(204);
    }
    else
    {
        webServer.send(404, "text/plain", "");
    }
}

void handleNotAuthorize(){
  webServer.sendHeader("WWW-Authenticate", "Basic realm=\"Secure\"");
  webServer.send(401, "text/html", "<html>Authentication failed</html>");
}

bool handleFileRead(String path){  // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";           // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){  // If the file exists, either as a compressed archive, or normal
    if(SPIFFS.exists(pathWithGz))                          // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = webServer.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

bool handleFileWrite(String path, String content){  // send the right file to the client (if it exists)
  Serial.println("handleFileWrite: " + path);
  if(SPIFFS.exists(path)){
    File file = SPIFFS.open(path, "w");
    int bytesWritten = file.print(content);
    if(bytesWritten <= 0){
      return false;
    }
    file.close();
    Serial.println(String("\Write file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

String readFile(String path){ 
  String result;
  if(SPIFFS.exists(path)){
    Serial.println("File Found");
    File file = SPIFFS.open(path, "r");
    String content = file.readStringUntil('\n');
    file.close();
    Serial.println(content);
    return content;
  }
  Serial.println("File Not Found");
  return result;
}

String getContentType(String filename){
  if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void setupCORSPolicy(){
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Access-Control-Max-Age", "10000");
  webServer.sendHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "*");
  webServer.sendHeader("Access-Control-Allow-Credentials", "false");
}

bool isAuthorized(){
  String auth = webServer.header("Authorization");
  String expectedAuth = "Basic "+adminAuth;
  if(auth != expectedAuth){
    Serial.print("Admin incorrect: ");
    Serial.print(auth);
    Serial.print(" vs ");
    Serial.println(expectedAuth);
  }
  return auth == expectedAuth;
}

void split(String rows[], String data, char delimeter){
  int count = 0;
  String elementData = "";
  for(int i=0;i<data.length();i++){
      if(data.charAt(i) != delimeter){
        elementData.concat(data.charAt(i));
      }else{
        rows[count] = elementData;  
        elementData = "";
        count++;
      }
  }
  if(elementData != ""){
     rows[count] = elementData;
  }
}

void loadConfig(){
  Serial.println("loading Config");
  
  String data = readFile("/config.data");
  String dataCol[20];
  String loadCol[6];
  String tmpStr[2];
  split(dataCol, data, '|');

  
  
  adminUser = dataCol[0];
  adminPass = dataCol[1];
  wifiSSID  = dataCol[2];
  wifiPASS  = dataCol[3];
  apSSID    = dataCol[4];
  apPASS    = dataCol[5];

  split(loadCol, dataCol[6],',');
  devHostName = dataCol[7];

  String dv_data = readFile("/devices_name.data");
  String dv_dataCol[7];
  split(dv_dataCol,dv_data,'|');
  
  
 
  for(int i = 0; i < 6; i++) {
    Serial.println("Pin:" + String(Loads[i][0]) + " = " + loadCol[i]);
    split(tmpStr,loadCol[i],'-');
    Loads[i][1] = tmpStr[0].toInt();
    Loads[i][2] = tmpStr[1].toInt();
    Loads_name[i] = dv_dataCol[i];
    digitalWrite(Loads[i][0], Loads[i][2] == 1 ? LOW : HIGH);
  }
  
  adminAuth = base64::encode(adminUser + ":" + adminPass);
  Serial.println(adminUser);


  String mqtt_data = readFile("/mqtt.data");
  String mqtt_dataCol[15];
  split(mqtt_dataCol,mqtt_data,'|');

  MQTT_ACTIVE     = mqtt_dataCol[0];
  MQTT_SERVER     = mqtt_dataCol[1];
  MQTT_SERVERPORT = mqtt_dataCol[2];
  MQTT_USERNAME   = mqtt_dataCol[3];
  MQTT_KEY        = mqtt_dataCol[4];
  //MQTT_PREAMBLE   = mqtt_dataCol[5];

  Serial.println("MQTT SERVER ADDRESS: " + MQTT_SERVER);
  Serial.println("MQTT SERVER PORT: " + MQTT_SERVERPORT);
  Serial.println("MQTT USERNAME: " + MQTT_USERNAME);
  Serial.println("MQTT AUTH KEY: " + MQTT_KEY);
  //Serial.println("MQTT PREAMDLE: " + MQTT_PREAMBLE);

  for(int i = 0; i < 6; i++) {
    Loads_mqtt_commands[i] = mqtt_dataCol[i+5];
    Serial.println("Creating Command for " + Loads_mqtt_commands[i]);
  }

  
}



void handleFront_Page() {
  handleFileRead("/front.html");
}

void handleSettings_Page()
{
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/settings.html");
}

void handleDevice_Page()
{
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/device_settings.html");
}

void handleAp_Page()
{
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/ap.html");
}

void handleUser_Page()
{
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/user_credentials.html");
}

void handleDevices_Page() {
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/devices.html");
}

void handleButtons_Page() {
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/button_settings.html");
}

void handleMQTT_Page(){
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }
  handleFileRead("/mqtt_details.html");
}

void handleWiFi_Page()
{
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    return webServer.requestAuthentication();
  }

  if(WiFi.status() == WL_CONNECTED) {
    handleFileRead("/wifi_disconnect.html");
  } else {
    handleFileRead("/wifi_connect.html");
  }
  
}

void handleButtons(){
  String pin_number = webServer.arg("pin");
  String data = "{\"result\":\"error\",\"text\":\"Invalid Pin\"}";
  
  for(int i = 0; i < 6;i++) {
   if(pin_number == String(Loads[i][0])) {
      Loads[i][2] = Loads[i][2] == 1 ? 0 : 1;
      data = "{\"result\":\"success\",\"text\":\"Pin Updated\",\"pin\":\"" + String(Loads[i][0]) + "\", \"status\":\"" + String(Loads[i][2]) + "\",\"device_name\":\"" + Loads_name[i] + "\"}";
      digitalWrite(Loads[i][0], (Loads[i][2] == 1 ? LOW : HIGH));
      Serial.println(String(Loads[i][0]) + " Set to " + (Loads[i][2] == 1 ? "HIGH" : "LOW"));
    }
  }
  updateConfig();
  webServer.send(200,"application/javascript",data);
}

void handleDevicesStatus(){
  String data = "[";

  for(int i = 0;i < 6; i++) {
    data += "{\"0\":" + String(Loads[i][0]) + ",\"1\":" + String(Loads[i][1]) + ",\"2\":" + String(Loads[i][2]) + ",\"3\":" + "\"" + Loads_name[i] + "\"" + "}" + (i + 1< 6 ? "," : "");
  }
  data += "]";
  webServer.send(200,"application/javascript",data);
}



void updateConfig(){
  String conf = adminUser + "|" + adminPass + "|" + wifiSSID + "|" + wifiPASS + "|" + apSSID + "|" + apPASS + "|";
  for(int i = 0; i < 6; i++) {
    conf = conf + String(Loads[i][1]) + "-" + String(Loads[i][2]) + (i < 6 ? "," : "");
  }
  conf = conf + "|";
  //Serial.println(conf);

  String d_conf = Loads_name[0] + "|" + Loads_name[1] + "|" + Loads_name[2] + "|" + Loads_name[3] + "|" + Loads_name[4] + "|" + Loads_name[5] + "|";

  String mqtt_conf = MQTT_ACTIVE + "|" + MQTT_SERVER + "|" + MQTT_SERVERPORT + "|" + MQTT_USERNAME + "|" + MQTT_KEY + "|" + Loads_mqtt_commands[0] + "|" + Loads_mqtt_commands[1] + "|" + Loads_mqtt_commands[2] + "|" + Loads_mqtt_commands[3] + "|" + Loads_mqtt_commands[4] + "|" + Loads_mqtt_commands[5] + "|";

  handleFileWrite("/config.data",conf);
  handleFileWrite("/devices_name.data",d_conf);
  handleFileWrite("/mqtt.data",mqtt_conf);
}

void handleDisconnect(){
  String data = "{\"result\":\"success\",\"text\":\"Disconnecting to WiFi Network\"}";
  webServer.send(200,"application/javascript",data);
  delay(1000);
  wifiSSID = "";
  wifiPASS = "";
  updateConfig();
  WiFi.disconnect();
}

void handleConnect(){
  String data = "{\"result\":\"error\",\"text\":\"Invalid Pin\"}";
  String wifi_ssid = webServer.arg("ssid");
  String wifi_pass = webServer.arg("pass");

  if(WiFi.status() == WL_CONNECTED) {
    data = "{\"result\":\"error\",\"text\":\"Already Connected to Wifi Network\"}";
  } else if(wifi_ssid=="") {
    data = "{\"result\":\"error\",\"text\":\"No SSID Provided\"}";
  } else {
    
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    
    data = "{\"result\":\"error\",\"text\":\"Can`t connect to wifi #2\"}";
    int seconds = 0;
    int con_status = 0;
    while(seconds <= WIFI_CONNECT_TIMEOUT) {
      delay(1000);
      con_status = WiFi.status();
      seconds += WIFI_CONNECT_DELAY;
      if(WiFi.status() == WL_CONNECTED) {
        data = "{\"result\":\"success\",\"text\":\"Connected to Wifi\"}";
        wifiSSID = wifi_ssid;
        wifiPASS = wifi_pass;
        updateConfig();
        break; 
      } else if(WiFi.status() == WL_CONNECT_FAILED && seconds >= WIFI_CONNECT_TIMEOUT) {
        data = "{\"result\":\"error\",\"text\":\"Can`t connect to WiFi\"}";
        break;
      } else if(WiFi.status() == WL_NO_SSID_AVAIL) {
        data = "{\"result\":\"error\",\"text\":\"WiFi Network not available\"}";
        break;
      } else if(WiFi.status() == WL_IDLE_STATUS) {
        data = "{\"result\":\"error\",\"text\":\"Error While connecting #3\"}";
        break;
      }

      /*
      connectedToWifi = (WiFi.status() == WL_CONNECTED);
      if(connectedToWifi) { 
        
      } 
      */
    }
  }
  webServer.send(200,"application/javascript",data);
}

void handleScan() {
  String data = "{\"result\":\"error\",\"text\":\"Invalid Scan\"}";
  int n = WiFi.scanNetworks();
  String networks = "[";
  for(int i = 0; i < n;i++) {
    networks += "\"" + WiFi.SSID(i) + "\"";
    if((i + 1) < n) networks += ",";
  }
  networks += "]";
  data = "{\"result\":\"success\",\"data\":" + networks + "}";
  webServer.send(200,"application/javascript",data);
}


void handleChangeAP()
{
  String data = "{\"result\":\"error\",\"text\":\"Invalid Action\"}";
  String ap_ssid = webServer.arg("ssid");
  String ap_pass = webServer.arg("pass");

  if(!isValidInput(ap_ssid)) {
    data = "{\"result\":\"error\",\"text\":\"SSID contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isValidInput(ap_pass)) {
    data = "{\"result\":\"error\",\"text\":\"Password contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(ap_ssid=="") {
    data = "{\"result\":\"error\",\"text\":\"SSID must not be empty\"}";
    webServer.send(200,"application/javascript",data);
  } else if(ap_pass.length() > 0 && ap_pass.length() < 8) {
    data = "{\"result\":\"error\",\"text\":\"Password must be 8 charactes above long\"}";
    webServer.send(200,"application/javascript",data);
  } else {
    apSSID = ap_ssid;
    apPASS = ap_pass;
    data = "{\"success\":\"error\",\"text\":\"Access Point has been changed, rebooting..\"}";
    webServer.send(200,"application/javascript",data);
    updateConfig();
    delay(2000);
    ESP.restart();
  }
}

void handleDeviceInfo()
{
  String data = "{\"result\":\"error\",\"text\":\"Invalid Action\"}";
  String device_idStr = webServer.arg("id");
  int device_id = -1;
  
  if(!isDigit(device_idStr.charAt(0)))
  {
    data = "{\"result\":\"error\",\"text\":\"Invalid Device ID #1 - " + device_idStr + "\"}";
  } else {
    device_id = device_idStr.toInt();

    if(device_id < 0 || device_id > 5)
    {
      data = "{\"result\":\"error\",\"text\":\"Invalid Device ID #2\"}";
    } else {
      data = "{\"result\":\"success\",\"text\":\"Invalid Device ID\",\"device_name\":\"" + Loads_name[device_id] + "\",\"active\":\"" + Loads[device_id][1] + "\",\"mqtt_topic\":\"" + Loads_mqtt_commands[device_id] + "\",\"mqtt_status\":\"" + mqtt_client.state() + "\",\"pin_num\":\"" + Loads[device_id][0] + "\",\"device_id\":\"" + device_id + "\"}";
    }
  }
  webServer.send(200,"application/javascript",data);
}
  
void handleChangeUSER()
{
  String data = "{\"result\":\"error\",\"text\":\"Invalid Action\"}";
  String user_name = webServer.arg("user");
  String user_pass = webServer.arg("pass");
  char charBuffer[20];

  if(!isValidInput(user_name)) {
    data = "{\"result\":\"error\",\"text\":\"Username contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isValidInput(user_pass)) {
    data = "{\"result\":\"error\",\"text\":\"Password contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(user_name=="") {
    data = "{\"result\":\"error\",\"text\":\"Username must not be empty\"}";
    webServer.send(200,"application/javascript",data);
  } else if(user_pass.length() > 0 && user_pass.length() < 8) {
    data = "{\"result\":\"error\",\"text\":\"Password must be 8 charactes above long\"}";
    webServer.send(200,"application/javascript",data);
  } else {
    adminUser = user_name;
    adminPass = user_pass;
    data = "{\"success\":\"error\",\"text\":\"User Credentials has been changed, rebooting..\"}";
    webServer.send(200,"application/javascript",data);
    updateConfig();
    delay(2000);
    ESP.restart();
  }
}

void handleEditDevice(){
  String data = "{\"result\":\"error\",\"text\":\"Invalid Action\"}";
  String device_name     = webServer.arg("device_name");
  String mqtt_topic      = webServer.arg("mqtt_topic");
  String device_active   = webServer.arg("active");
  String device_idStr    = webServer.arg("device_id");
  int device_id = -1;
  bool topic_change = false;


  
  if(!isValidInput(device_name)) {
    data = "{\"result\":\"error\",\"text\":\"Invalid Device name\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isValidInput(mqtt_topic)) {
    data = "{\"result\":\"error\",\"text\":\"Invalid MQTT Topic\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isDigit(device_active.charAt(0))) {
    data = "{\"result\":\"error\",\"text\":\"Invalid Status\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isDigit(device_idStr.charAt(0))) {
    data = "{\"result\":\"error\",\"text\":\"Invalid Device ID #1 - " + device_idStr + "\"}";
    webServer.send(200,"application/javascript",data);
  } else {

    device_id = device_idStr.toInt();

    if(device_id < 0 || device_id > 5) {
      data = "{\"result\":\"error\",\"text\":\"Invalid Device ID #2\"}";
      webServer.send(200,"application/javascript",data);
    } else {

   
      Loads_name[device_id]   = device_name;      
      Loads[device_id][1]     = device_active.charAt(0) == '1' ? 1 : 0;
      
      if(mqtt_topic != Loads_mqtt_commands[device_id])
      {
        if(mqtt_client.connected()) 
        {
          mqtt_client.unsubscribe(Loads_mqtt_commands[device_id].c_str());
          mqtt_client.subscribe(mqtt_topic.c_str());
        }
        Loads_mqtt_commands[device_id] = mqtt_topic;
      }
  
      data = "{\"result\":\"success\",\"text\":\"Device Updated\"}";
      webServer.send(200,"application/javascript",data);
      updateConfig();
    }
  }
}

void handleEditMQTT(){
  String data = "{\"result\":\"error\",\"text\":\"Invalid Action\"}";
  String mqtt_user     = webServer.arg("user");
  String mqtt_key      = webServer.arg("key");
  String mqtt_server   = webServer.arg("server");
  String mqtt_port     = webServer.arg("port");
  String mqtt_active   = webServer.arg("active");
  bool valid_command = true;
  
  String mqtt_command[6];

  for(int i = 0;i < 6; i ++ ) {
    mqtt_command[i] = webServer.arg("command" + String(i + 1));
    Serial.println(webServer.arg("command" + String(i + 1)));
    if(!isValidInput(mqtt_command[i])) {
      valid_command = false;
      break;
    }
  }

  
  if(!isValidInput(mqtt_user)) {
    data = "{\"result\":\"error\",\"text\":\"Username contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isValidInput(mqtt_key)) {
    data = "{\"result\":\"error\",\"text\":\"Username contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isValidInput(mqtt_server)) {
    data = "{\"result\":\"error\",\"text\":\"Username contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!isValidInput(mqtt_port) && isDigit(mqtt_port.toInt())) {
    data = "{\"result\":\"error\",\"text\":\"Username contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  } else if(!valid_command) {
    data = "{\"result\":\"error\",\"text\":\"Username contains invalid character\"}";
    webServer.send(200,"application/javascript",data);
  
  } else {
    MQTT_SERVER             = mqtt_server;
    MQTT_SERVERPORT         = mqtt_port;
    MQTT_USERNAME           = mqtt_user;
    MQTT_KEY                = mqtt_key;
    MQTT_ACTIVE             = mqtt_active.charAt(0) == '1' ? "1" : "0";

    for(int i=0;i<6;i++) {
      Loads_mqtt_commands[i]  = mqtt_command[i]; 
      //Serial.println(Feeds_name[i]);
    }

    String mqtt_conf = MQTT_ACTIVE + "|" + MQTT_SERVER + "|" + MQTT_SERVERPORT + "|" + MQTT_USERNAME + "|" + MQTT_KEY + "|" + Loads_mqtt_commands[0] + "|" + Loads_mqtt_commands[1] + "|" + Loads_mqtt_commands[2] + "|" + Loads_mqtt_commands[3] + "|" + Loads_mqtt_commands[4] + "|" + Loads_mqtt_commands[5] + "|";

    data = "{\"result\":\"success\",\"text\":\"MQTT Updated\"}";
    webServer.send(200,"application/javascript",data);
    updateConfig();
    delay(2000);
    ESP.restart();
  }
}


void handleStatus() {
  String data = "{\"result\":\"success\",\"user\":\"" + adminUser + "\",\"wifi_ssid\":\"" + wifiSSID + "\",\"ap_ssid\":\"" + apSSID + "\",\"wifi_connected\":\"" + (WiFi.status() == WL_CONNECTED ? "1" : "0") + "\",\"server\":\"" + MQTT_SERVER + "\",\"port\":\"" + MQTT_SERVERPORT + "\",\"mqtt_user\":\"" + MQTT_USERNAME + "\",\"mqtt_key\":\"" + MQTT_KEY + "\",\"mqtt_active\":\"" + MQTT_ACTIVE + "\",\"mqtt_status\":\"" + mqtt_client.state() + "\",\"dev1\":\"" + Loads_mqtt_commands[0] + "\",\"dev2\":\"" + Loads_mqtt_commands[1] + "\",\"dev3\":\"" + Loads_mqtt_commands[2] + "\",\"dev4\":\"" + Loads_mqtt_commands[3] + "\",\"dev5\":\"" + Loads_mqtt_commands[4] + "\",\"dev6\":\"" + Loads_mqtt_commands[5] + "\"}"; //aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa

  Serial.println(WiFi.localIP());
  webServer.send(200,"application/javascript",data);
}





void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  if(WiFi.status() == WL_CONNECTED && MQTT_ACTIVE == "1") {
    loopMQTT();
  }
  ArduinoOTA.handle();
}

void setup() {
  Serial.begin(9600);
  Serial.println("Connecting to Network");

  SPIFFS.begin();
  Serial.println("Open Spiffs");

  loadConfig();
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname(devHostName.c_str());
  WiFi.softAPConfig(apIP,apIP,IPAddress(255,255,255,0));

  if(apSSID == ""){
    apSSID = "Mendy Setup";
    apPASS = "12345678";
  }
  
  WiFi.softAP(apSSID,apPASS);
  dnsServer.start(DNS_PORT,"*",apIP);
  
  if(wifiSSID != "") {

    WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
      
    int seconds = 0;
    while(seconds <= WIFI_CONNECT_TIMEOUT) {
      connectedToWifi = (WiFi.status() == WL_CONNECTED);
      if(connectedToWifi) { break; } 
      seconds += WIFI_CONNECT_DELAY;
    }
  }
  if(MQTT_ACTIVE == "1") {
    initMQTT();
  }
  //FronEnd
  webServer.on("/js/jquery.min.js",[]{handleFileRead("/js/jquery.min.js");});
  webServer.on("/settings",handleSettings_Page);
  webServer.on("/wifi_networks",handleWiFi_Page);
  webServer.on("/user",handleUser_Page);
  webServer.on("/accesspoint",handleAp_Page);
  webServer.on("/devices",handleDevices_Page);
  webServer.on("/device",handleDevice_Page);
  webServer.on("/mqtt",handleMQTT_Page);
  webServer.on("/buttons",handleButtons_Page);


  //Apis
  webServer.on("/update_device",handleEditDevice);
  webServer.on("/update_mqtt",handleEditMQTT);
  webServer.on("/connect",handleConnect);
  webServer.on("/disconnect",handleDisconnect);
  webServer.on("/scan",handleScan);
  webServer.on("/changeAp", handleChangeAP);
  webServer.on("/changeUser",handleChangeUSER);
  webServer.on("/toggle",handleButtons);
  webServer.on("/status",handleStatus);
  webServer.on("/devices_status",handleDevicesStatus);
  webServer.on("/device_info",handleDeviceInfo);
  webServer.onNotFound(handleFront_Page);
  
  webServer.begin();
  ArduinoOTA.begin();
  //updateConfig();

  for(int i = 0; i < 6;i++) {
    pinMode(Loads[i][0],OUTPUT);
    Serial.println(String(Loads[i][0]) + " Set to Output");
  }
}

 
