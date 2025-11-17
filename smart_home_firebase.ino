#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>  
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <PN532_HSU.h>
#include <PN532.h>


#define MQ_PIN 34       
#define BUZZER_PIN 33   
#define PIR_PIN 12      
#define DHTPIN 4        
#define DHTTYPE DHT11   
#define GAS_SEUIL 3000  
#define SOIL_PIN 35     
#define SOIL_SEUIL 20   
#define SERVO_PIN 18    

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define DATABASE_URL ""
#define API_KEY ""
#define LEGACY_TOKEN "" 

FirebaseConfig config;
FirebaseAuth auth;
FirebaseData firebaseData;

HardwareSerial mySerial(1);  
PN532_HSU pn532hsu(mySerial);
PN532 nfc(pn532hsu);

const String validTags[] = {"179.69.178.238", "67.3.154.166"};
const int numValidTags = 2;
int servoState = 0;

Servo monServo;
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);


int LEDred = 27;  
int LEDorange = 26; 
int LEDblue = 25;


unsigned long previousMillis = 0;


void toggleDoor();
void closeDoor();
String tagToString(byte id[4]);
bool isValidTag(String tag);

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connexion au Wi-Fi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        Serial.print(".");
        delay(500);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nÉchec de la connexion Wi-Fi !");
        return;
    }

    Serial.println(" Connecté avec l'IP : " + WiFi.localIP().toString());

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    config.signer.tokens.legacy_token = LEGACY_TOKEN;  

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    mySerial.begin(115200, SERIAL_8N1, 16, 17);
    nfc.begin();
    if (!nfc.getFirmwareVersion()) {
        Serial.println("Module PN532 non détecté !");
        while (1);
    }
    nfc.SAMConfig();

    monServo.attach(SERVO_PIN);
    monServo.write(0);
    
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(MQ_PIN, INPUT);
    pinMode(PIR_PIN, INPUT);
    pinMode(SOIL_PIN, INPUT);
    pinMode(LEDred, OUTPUT);
    pinMode(LEDorange, OUTPUT);
    pinMode(LEDblue, OUTPUT);

    dht.begin();
    lcd.begin();
    lcd.backlight();
    lcd.print("MY SMART HOME");
    delay(2000);
    lcd.clear();
}
void loop() {
    if (Firebase.ready() && (millis() - previousMillis > 5000 || previousMillis == 0)) {
        previousMillis = millis();
        
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        int gasLevel = analogRead(MQ_PIN);
        int mouvement = digitalRead(PIR_PIN);
        int soilValue = analogRead(SOIL_PIN);
        int soilHumidity = map(soilValue, 4095, 0, 0, 100);
        soilHumidity = constrain(soilHumidity, 0, 100);

        Serial.print(" Température : ");
        Serial.print(temperature);
        Serial.print(" °C, Humidité : ");
        Serial.print(humidity);
        Serial.println(" %");
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Temp: ");
        lcd.print(temperature);
        lcd.print("C");
        lcd.setCursor(0, 1);
        lcd.print("Hum: ");
        lcd.print(humidity);
        lcd.print("%");

        Firebase.RTDB.setFloat(&firebaseData, "/FirebaseIOT/temperature", temperature);
        Firebase.RTDB.setFloat(&firebaseData, "/FirebaseIOT/humidity", humidity);

        digitalWrite(LEDorange, LOW); 
        digitalWrite(LEDred, LOW); 
        digitalWrite(BUZZER_PIN, LOW);

         if (soilHumidity < SOIL_SEUIL) {
            lcd.clear();
            lcd.print("Sol sec !");
        }

        if (gasLevel > GAS_SEUIL) {
            digitalWrite(LEDred, HIGH);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Gaz detecte !");
            lcd.setCursor(0, 1);
           lcd.print(gasLevel); 
            digitalWrite(BUZZER_PIN, HIGH);
            delay(2000);
            digitalWrite(BUZZER_PIN, LOW);
        }

        if (mouvement == HIGH) {  
            delay(1000);
            lcd.clear();
            lcd.print("Mouvement detecte !");
            digitalWrite( LEDorange, HIGH); 
        }

       
    }

    uint8_t uid[4];
    uint8_t uidLength;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
        String tagId = tagToString(uid);
        Serial.print("Tag détecté : ");
        Serial.println(tagId);

        if (isValidTag(tagId)) {
            toggleDoor();
        } else {
            closeDoor();
        }
        delay(1000);  
     } else {
    Serial.println("Aucun tag détecté...");
  }

    String doorStatus = "";
    if (Firebase.RTDB.getString(&firebaseData, "/FirebaseIOT/doorStatus")) {
        doorStatus = firebaseData.stringData();
    }


    if (doorStatus == "open" && servoState != 0) {
        monServo.write(0);
        servoState = 0;
        Serial.println("Porte ouverte!");
        lcd.clear();
        lcd.print("Porte ouverte!");
    } 
  
    else if (doorStatus == "close" && servoState == 0) {
        monServo.write(120);
        servoState = 120;
        Serial.println("Porte fermée!");
        lcd.clear();
        lcd.print("Porte fermée!");
    }

   
    String ledStatus = "";
    if (Firebase.RTDB.getString(&firebaseData, "/FirebaseIOT/ledStatus")) {
        ledStatus = firebaseData.stringData();
    }

    if (ledStatus == "on") {
        digitalWrite(LEDblue, HIGH);
        Serial.println("Commande Firebase: LED allumée");
    } else if (ledStatus == "off") {
        digitalWrite(LEDblue, LOW);
        Serial.println("Commande Firebase: LED éteinte");
    }
}

String tagToString(byte id[4]) {
    return String(id[0]) + "." + String(id[1]) + "." + String(id[2]) + "." + String(id[3]);
}

bool isValidTag(String tag) {
    for (int i = 0; i < numValidTags; i++) {
        if (tag == validTags[i]) {
            return true;
        }
    }
    return false;
}

void toggleDoor() {
 
     static int servoState = 0;

  if (servoState == 0) {
    monServo.write(120);
    servoState = 120;
    Serial.println("Porte ouverte !");
    lcd.clear(); 
        lcd.print("Porte fermée!");
  } else {
    monServo.write(0);
    servoState = 0;
    Serial.println("Porte fermée !");
    lcd.clear();
     lcd.print("Porte ouverte!");
  }
    
}

void closeDoor() {
    monServo.write(0);
    Serial.println("Tag invalide. Porte fermée.");
}
