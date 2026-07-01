// Todas las librerias Necesarias
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <HX711.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include <ArduinoJson.h>

// --- CONFIGURACIÓN DE HARDWARE ---
const int HX711_DOUT = 5; 
const int HX711_SCK = 18;  
HX711 balanza;

// --- CONFIGURACIÓN DE ESPACIOS EN EEPROM (MEMORIA PERSISTENTE) ---
#define EEPROM_SIZE 512
const int ADDR_SSID   = 0;   
const int ADDR_PASS   = 64;  
const int ADDR_TOKEN  = 128; 
const int ADDR_NOMBRE = 192; 
const int ADDR_MARCA  = 256; 
const int ADDR_SMAX   = 320; 
const int ADDR_SMIN   = 350; 
const int ADDR_CALIB  = 380; 
const int ADDR_PUNIT  = 420; 

// --- CONFIGURACIÓN GENERAL DE RED ---
const char* AP_SSID = "Estante_Inteligente_AP"; // NOMBRE DE RED WIFI TEMPORAL
const char* THINGSBOARD_SERVER = "thingsboard.cloud"; // SERVIDOR AL QUE EL DISPOSITIVO SE CONECTARA

// Variables globales dinámicas (EN VALOR POR DEFECTO)
String ssidReal = "";
String passReal = "";
String tokenReal = "";
String productoNombre = "";
String productoMarca = "";
int stockMax = 10;
int stockMin = 2;
float factorCalibracion = 420.12; 
float pesoUnitarioReal = 500.0;   

// Servidores y Clientes de Red
WebServer server(80);
WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient);

// --- FUNCIONES DE ALMACENAMIENTO SEGURO (EEPROM) ---
void guardarStringEEPROM(int addr, String str) {
  int len = str.length();
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + i, str[i]);
  }
  EEPROM.write(addr + len, '\0');
  EEPROM.commit();
}

String leerStringEEPROM(int addr) {
  char data[65]; 
  int i = 0;
  char ch = EEPROM.read(addr);
  while (ch != '\0' && i < 64) {
    data[i] = ch;
    i++;
    ch = EEPROM.read(addr + i);
  }
  data[i] = '\0';
  return String(data);
}

void guardarIntEEPROM(int addr, int valor) {
  EEPROM.write(addr, valor);
  EEPROM.commit();
}

int leerIntEEPROM(int addr) {
  return EEPROM.read(addr);
}

void guardarFloatEEPROM(int addr, float valor) {
  EEPROM.put(addr, valor);
  EEPROM.commit();
}

float leerFloatEEPROM(int addr, float valorPorDefecto) {
  float valor;
  EEPROM.get(addr, valor);
  if (isnan(valor) || valor <= 0.0) {
    return valorPorDefecto; 
  }
  return valor;
}

// --- ENDPOINT ASÍNCRONO PARA AJAX (ACTUALIZAR PESO INTELIGENTE EN EL PORTAL AP PARA EL USUARIO) ---
void handlePesoRaw() {
  float pesoMuestra = balanza.is_ready() ? balanza.get_units(1) : 0.0;
  if(pesoMuestra < 0) pesoMuestra = 0.0;
  server.send(200, "text/plain", String(pesoMuestra, 2));
}

// --- PORTAL CAUTIVO CON AUTO-REFRESCO ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Configuración Estante Inteligente</title>";
  html += "<style>body{font-family:Arial,sans-serif;background-color:#f4f4f9;margin:0;padding:20px;display:flex;justify-content:center;}";
  html += ".card{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.1);width:100%;max-width:400px;}";
  html += "h2{color:#333;margin-bottom:20px;text-align:center;}label{font-weight:bold;color:#555;}";
  html += ".sensor-box{background-color:#e8f4fd;border-left:5px solid #2196F3;padding:12px;margin-bottom:15px;border-radius:4px;text-align:center;font-size:15px;font-weight:bold;color:#0d47a1;}";
  html += "input[type='text'],input[type='password'],input[type='number']{width:100%;padding:10px;margin:6px 0 16px 0;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;}";
  html += "input[type='submit']{width:100%;background-color:#4CAF50;color:white;padding:12px;border:none;border-radius:6px;cursor:pointer;font-size:16px;font-weight:bold;margin-top:10px;}";
  html += "input[type='submit']:hover{background-color:#45a049;}</style>";
  
  html += "<script>setInterval(function(){";
  html += "var xhttp = new XMLHttpRequest();";
  html += "xhttp.onreadystatechange = function(){";
  html += "if(this.readyState == 4 && this.status == 200){";
  html += "document.getElementById('peso_vivo').innerHTML = this.responseText + ' g';";
  html += "}};";
  html += "xhttp.open('GET', '/peso_raw', true);";
  html += "xhttp.send();";
  html += "}, 500);</script>";
  
  html += "</head><body>";
  html += "<div class='card'><h2>⚙️ Configuración del Estante</h2>";
  html += "<div class='sensor-box'>⚖️ Lectura Actual del Sensor: <span id='peso_vivo'>Cargando...</span></div>";
  
  html += "<form action='/guardar' method='POST'>";
  html += "<label>SSID WiFi de la Casa:</label><input type='text' name='ssid' required>";
  html += "<label>Contraseña WiFi:</label><input type='password' name='password' required>";
  html += "<label>Device Token (ThingsBoard):</label><input type='text' name='token' required>";
  html += "<hr style='border:0;border-top:1px solid #eee;margin:20px 0;'>";
  html += "<label>Nombre del Producto:</label><input type='text' name='nombre' placeholder='Ej: Crema batida' required>";
  html += "<label>Marca del Producto:</label><input type='text' name='marca' placeholder='Ej: Colun' required>";
  html += "<label>Peso Unitario Real del Envase (Gramos):</label><input type='number' step='0.1' name='peso_unit' value='500.0' min='0.1' required>";
  html += "<label>Stock Máximo de Capacidad:</label><input type='number' name='stock_max' value='10' min='1' required>";
  html += "<label>Stock Mínimo de Alerta:</label><input type='number' name='stock_min' value='2' min='0' required>";
  html += "<label>Factor de Calibración Calculado:</label><input type='number' step='0.01' name='factor_calib' value='" + String(factorCalibracion, 2) + "' required>";
  html += "<input type='submit' value='Guardar e Iniciar Estante'>";
  html += "</form></div></body></html>";
  
  server.send(200, "text/html", html);
}
// FUNCIOIN GUARDAR TODOS LOS DATOS SOLICITADOS
void handleGuardar() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("token") &&
      server.hasArg("nombre") && server.hasArg("marca") && server.hasArg("stock_max") && 
      server.hasArg("stock_min") && server.hasArg("factor_calib") && server.hasArg("peso_unit")) {
    
    float fCalibInput = server.arg("factor_calib").toFloat();
    float pUnitInput = server.arg("peso_unit").toFloat();
    int sMaxInput = server.arg("stock_max").toInt();
    int sMinInput = server.arg("stock_min").toInt();

    if (fCalibInput == 0.0) fCalibInput = 420.12;  
    if (pUnitInput <= 0.0) pUnitInput = 500.0;    
    if (sMaxInput <= 0) sMaxInput = 10;            

    guardarStringEEPROM(ADDR_SSID, server.arg("ssid"));
    guardarStringEEPROM(ADDR_PASS, server.arg("password"));
    guardarStringEEPROM(ADDR_TOKEN, server.arg("token"));
    guardarStringEEPROM(ADDR_NOMBRE, server.arg("nombre"));
    guardarStringEEPROM(ADDR_MARCA, server.arg("marca"));
    guardarIntEEPROM(ADDR_SMAX, sMaxInput);
    guardarIntEEPROM(ADDR_SMIN, sMinInput);
    guardarFloatEEPROM(ADDR_CALIB, fCalibInput);
    guardarFloatEEPROM(ADDR_PUNIT, pUnitInput);

    String htmlResp = "<html><body style='font-family:Arial;text-align:center;padding-top:50px;'>";
    htmlResp += "<h2>✅ Configuración Procesada y Guardada</h2><p>Reiniciando dispositivo...</p></body></html>";
    server.send(200, "text/html", htmlResp);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Error: Formulario incompleto.");
  }
}

// --- INICIALIZACIÓN DE MODO ACCESS POINT ---
void iniciarAP() {
  Serial.println("\n[Modo ACCESS POINT] Red doméstica no encontrada o no configurada.");
  Serial.println("[Modo ACCESS POINT] Activando Portal de Usuario Local...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_SSID); 

  server.on("/", HTTP_GET, handleRoot);
  server.on("/peso_raw", HTTP_GET, handlePesoRaw); 
  server.on("/guardar", HTTP_POST, handleGuardar);
  server.begin();
  Serial.print("[Web Server] Portal Cautivo listo en la IP: ");
  Serial.println(WiFi.softAPIP());
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  ssidReal = leerStringEEPROM(ADDR_SSID);
  passReal = leerStringEEPROM(ADDR_PASS);
  tokenReal = leerStringEEPROM(ADDR_TOKEN);
  productoNombre = leerStringEEPROM(ADDR_NOMBRE);
  productoMarca = leerStringEEPROM(ADDR_MARCA);
  
  int sMaxAux = leerIntEEPROM(ADDR_SMAX);
  int sMinAux = leerIntEEPROM(ADDR_SMIN);
  if(sMaxAux > 0 && sMaxAux != 255) stockMax = sMaxAux;
  if(sMinAux != 255) stockMin = sMinAux;

  factorCalibracion = leerFloatEEPROM(ADDR_CALIB, 420.12);
  pesoUnitarioReal = leerFloatEEPROM(ADDR_PUNIT, 500.0);

  balanza.begin(HX711_DOUT, HX711_SCK);
  balanza.set_scale(factorCalibracion);
  balanza.tare(); 

  // --- INTEGRACIÓN OPCIÓN A: TIMEOUT DE RED ---
  if (ssidReal.length() > 0 && tokenReal.length() > 0) {
    Serial.println("\n[Modo CLIENTE] Intentando conectar a la red guardada...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidReal.c_str(), passReal.c_str());
    
    int intentos = 0;
    // 30 intentos de 500ms = 15 segundos máximos de espera
    while (WiFi.status() != WL_CONNECTED && intentos < 30) {
      delay(500);
      Serial.print(".");
      intentos++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] ¡Conectado con éxito!");
      return; // Salta el AP y va directo al loop() operativo
    }
  }

  // Si no hay credenciales o se agotaron los 15 segundos sin éxito:
  iniciarAP();
}

// --- LOOP PRINCIPAL ---
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
    delay(10);
    return; 
  }

  if (!tb.connected()) {
    if (!tb.connect(THINGSBOARD_SERVER, tokenReal.c_str())) {
      delay(5000);
      return;
    }
  }

  float pesoRaw = balanza.get_units(10); 
  if (pesoRaw < 0) pesoRaw = 0; 

  int stockCalculado = round(pesoRaw / pesoUnitarioReal);
  if (stockCalculado > stockMax) stockCalculado = stockMax;

  float ocupacionEstante = 0.0;
  if (stockMax > 0) {
    ocupacionEstante = ((float)stockCalculado / (float)stockMax) * 100.0;
  }

unsigned long tiempoSegundos = millis() / 1000;
long calidadWifi = WiFi.RSSI();

// Nota dev: pinche gemini no se entera de nada

  //Usamos libreria ArduinoJson para crear el objeto
  JsonDocument doc; 

  // Llenamos el documento con todos nuestros datos
  doc["nombre"] = productoNombre;
  doc["marca"] = productoMarca;
  doc["stock_actual"] = stockCalculado;
  doc["ocupacion_estante"] = ocupacionEstante;
  doc["peso_raw"] = pesoRaw;
  doc["stock_max"] = stockMax;
  doc["stock_min"] = stockMin;
  doc["peso_unitario"] = pesoUnitarioReal;
  doc["ESP32uptime"] = tiempoSegundos;
  doc["ESP32_rssi"] = calidadWifi;

  //Calculamos el tamaño exacto del JSON usando el método nativo "Measure_Json" de su propia lista
  size_t jsonSize = measureJson(doc);

  //Pasamos el documento y el tamaño medido tal como exige la firma de los devs de ThingsBoard
  tb.sendTelemetryJson(doc, jsonSize);

  tb.loop();
  delay(5000); 
}
