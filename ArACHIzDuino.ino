#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// --- CONFIGURACIÓN HARDWARE ---
PN532_I2C pn532_i2c(Wire);
PN532 nfc_hardware(pn532_i2c);
SoftwareSerial mySerial(2, 3); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);


//hola
const int PIN_BUZZER = 9;

struct Usuario {
  char nombre[16];
  int huellaID;
  char nfcUID[20];
};

Usuario usuarios[10];
int cantidadUsuarios = 0;

// --- PROTOTIPOS ---
void sonidoExito();
void sonidoError();
bool enrolar(int id);
String hexUID(uint8_t* uid, uint8_t len);
bool nfcYaExiste(String uidNuevo);
void verificarAccesoNFC(String uid);
void verificarAccesoHuella();
void borrarTodo();
void registrarNuevoUsuario();

void setup() {
  Serial.begin(9600);
  pinMode(PIN_BUZZER, OUTPUT);
  nfc_hardware.begin();
  nfc_hardware.SAMConfig();
  finger.begin(57600);


  EEPROM.get(0, cantidadUsuarios);
  if (cantidadUsuarios > 10 || cantidadUsuarios < 0) cantidadUsuarios = 0;
  for (int i = 0; i < cantidadUsuarios; i++) {
    EEPROM.get(sizeof(int) + (i * sizeof(Usuario)), usuarios[i]);
  }

  Serial.println(F("\n-------------------------------------------"));
  Serial.println(F("SISTEMA ARACIZ V5.2 - LISTO"));
  Serial.print(F("Usuarios: ")); Serial.println(cantidadUsuarios);
  Serial.println(F("R = Registrar | B = Borrar Todo"));
  Serial.println(F("-------------------------------------------"));
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'R' || c == 'r') registrarNuevoUsuario();
    if (c == 'B' || c == 'b') borrarTodo();
  }

  // Lectura NFC
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; 
  uint8_t uidLength;
  success = nfc_hardware.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);
 if (success) {
    verificarAccesoNFC(hexUID(uid, uidLength));
    delay(1000); 
  } else {
    nfc_hardware.getFirmwareVersion(); // Mantiene el bus I2C activo
  }

  // Lectura Huella
  verificarAccesoHuella();

  static int fallos = 0;

success = nfc_hardware.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);

static unsigned long ultimoExito = 0;
const unsigned long TIEMPO_LIMITE = 5000; // 5 segundos

success = nfc_hardware.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);


}

// --- GESTIÓN DE REGISTRO ---

void registrarNuevoUsuario() {
  if (cantidadUsuarios >= 10) { Serial.println(F("Memoria llena.")); sonidoError(); return; }

  Usuario nuevoU;
  memset(nuevoU.nfcUID, 0, sizeof(nuevoU.nfcUID));
  nuevoU.huellaID = 0;

  Serial.println(F("\n--- INICIANDO REGISTRO ---"));
  Serial.println(F("Escribe el nombre:"));
  while (Serial.available()) Serial.read(); 
  while (Serial.available() == 0);
  String n = Serial.readStringUntil('\n'); n.trim();
  n.toCharArray(nuevoU.nombre, 16);

  // REGISTRO HUELLA
  Serial.println(F("¿Registrar HUELLA? (S/N)"));
  while (Serial.available() == 0);
  char opH = Serial.read();
 
  if (opH == 'S' || opH == 's') {
    int idH = cantidadUsuarios + 1; 
    if (!enrolar(idH)) {
      Serial.println(F("Registro de huella cancelado. Pasando a NFC..."));
      nuevoU.huellaID = 0; // Aseguramos que queda en 0 si no terminó
    } else {
      nuevoU.huellaID = idH; // Solo se asigna si enrolar devolvió true
    }
  }
  // REGISTRO NFC
  Serial.println(F("\n¿Registrar NFC? (S/N)"));
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0);
  char opN = Serial.read();
  if (opN == 'S' || opN == 's') {
    bool okNFC = false;
    while (!okNFC) {
      Serial.println(F("Acerque tarjeta (C para cancelar)..."));
      uint8_t success = 0;
      uint8_t u_nfc[] = { 0, 0, 0, 0, 0, 0, 0 };
      uint8_t len;
      while(!success) {
        success = nfc_hardware.readPassiveTargetID(PN532_MIFARE_ISO14443A, u_nfc, &len, 100);
        if (Serial.available() > 0 && toupper(Serial.peek()) == 'C') { Serial.read(); goto saltarNFC; }
      }
      String s = hexUID(u_nfc, len);
      if (nfcYaExiste(s)) {
        Serial.println(F("Error: Ya registrada."));
        sonidoError();
      } else {
        s.toCharArray(nuevoU.nfcUID, 20);
        Serial.println(F("NFC Vinculado."));
        sonidoExito();
        okNFC = true;
        nfc_hardware.SAMConfig(); // <--- AGREGA ESTA LÍNEA AQUÍ
      }
    }
  }
  saltarNFC:

  if (nuevoU.huellaID != 0 || strlen(nuevoU.nfcUID) > 0) {
    usuarios[cantidadUsuarios] = nuevoU;
    cantidadUsuarios++;
    EEPROM.put(0, cantidadUsuarios);
    EEPROM.put(sizeof(int) + ((cantidadUsuarios-1) * sizeof(Usuario)), nuevoU);
    Serial.print(F("Usuario guardado: ")); Serial.println(nuevoU.nombre);
    sonidoExito();
    delay(1000);
  }
}

bool enrolar(int id) {
  while (true) { // Bucle infinito hasta éxito o cancelación
    int p = -1;
    Serial.println(F("COLOQUE EL DEDO... (C para cancelar)"));
    while (p != FINGERPRINT_OK) {
      p = finger.getImage();
      if (Serial.available() > 0 && toupper(Serial.peek()) == 'C') { Serial.read(); return false; }
    }

    // --- NUEVA VALIDACIÓN: ¿La huella ya existe? ---
    p = finger.image2Tz(1);
    if (p == FINGERPRINT_OK) {
      p = finger.fingerFastSearch();
      if (p == FINGERPRINT_OK) {
        Serial.println(F("ERROR: Esta huella ya está registrada. ¡Use otro dedo!"));
        sonidoError();
        delay(2000);
        continue; // Reinicia el bucle para pedir otro dedo
      }
    }

    // Si la huella es nueva, procedemos
    tone(PIN_BUZZER, 2000, 150); 
    Serial.println(F("QUITE EL DEDO..."));
    delay(1000);
    while (finger.getImage() != FINGERPRINT_NOFINGER);
    
    p = -1;
    Serial.println(F("COLOQUE EL MISMO DEDO OTRA VEZ..."));
    while (p != FINGERPRINT_OK) {
      p = finger.getImage();
      if (Serial.available() > 0 && toupper(Serial.peek()) == 'C') { Serial.read(); return false; }
    }

    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) {
      Serial.println(F("Error al procesar. Reintentando registro completo..."));
      sonidoError();
      continue; 
    }

    // Crear modelo
    if (finger.createModel() == FINGERPRINT_OK) {
      if (finger.storeModel(id) == FINGERPRINT_OK) {
         tone(PIN_BUZZER, 2000, 200);
        Serial.println(F("¡Huella guardada con éxito!"));
        return true; // Única forma de salir con éxito
      }
    } else {
      Serial.println(F("Las huellas no coinciden. Reintentando..."));
      sonidoError();
      delay(1000);
    }
  }
}

void verificarAccesoHuella() {
  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) return;

  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    for (int i = 0; i < cantidadUsuarios; i++) {
      if (usuarios[i].huellaID == finger.fingerID) {
        Serial.print(F("BIENVENIDO: ")); Serial.println(usuarios[i].nombre);
        sonidoExito();
        delay(2000); 
        return;
      }
    }
  } else {
    Serial.println(F("HUELLA NO REGISTRADA"));
    sonidoError();
    delay(1000);
  }
}

void verificarAccesoNFC(String uid) {
  for (int i = 0; i < cantidadUsuarios; i++) {
    if (String(usuarios[i].nfcUID) == uid) {
      Serial.print(F("NFC VALIDO: ")); Serial.println(usuarios[i].nombre);
      sonidoExito(); return;
    }
  }
  Serial.println(F("NFC DESCONOCIDO"));
  sonidoError();
  nfc_hardware.SAMConfig();
}

String hexUID(uint8_t* uid, uint8_t len) {
  String s = "";
  for (uint8_t i=0; i < len; i++) {
    if (i > 0) s += " ";
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

bool nfcYaExiste(String uidNuevo) {
  if (uidNuevo == "") return false;
  for (int i = 0; i < cantidadUsuarios; i++) {
    if (String(usuarios[i].nfcUID) == uidNuevo) return true;
  }
  return false;
}

void borrarTodo() {
  Serial.println(F("¿Borrar todos los datos? (S/N)"));
  while(Serial.available() == 0);
  char c = Serial.read();
  if(toupper(c) == 'S') {
    finger.emptyDatabase();
    cantidadUsuarios = 0;
    EEPROM.put(0, cantidadUsuarios);
    Serial.println(F("SISTEMA RESETEADO"));
    sonidoExito();
  }
}



void sonidoExito() { tone(PIN_BUZZER, 2500, 400); }

void sonidoError() { tone(PIN_BUZZER, 500, 300); delay(100); tone(PIN_BUZZER, 500, 300); }