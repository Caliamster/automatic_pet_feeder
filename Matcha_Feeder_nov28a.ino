//RFID SDA - D21, SCK - D18, MOSI - D23, MISO - D19, RST - D22
//IR1 - D35, IR2 - D34
//IN1 - D5, IN2 - D4, IN3 - D2, IN4 - D15
//Servo - D27
//Error LED - D32
//Empty LED - D25
//RFID MEM Full LED -

//Libraries
#include "arduino_secrets.h"
#include "thingProperties.h"
//Time
#include "time.h"
//Mail
#include <ESP_Mail_Client.h>
//RFID
#include <SPI.h>
#include <MFRC522.h>
//Stepper
#include <Stepper.h>
//Servo
#include <ESP32Servo.h>


//Constants
//Mail
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "matcha.feeder@gmail.com"
#define AUTHOR_PASSWORD "e23r2tgfdgdfAg"
#define RECIPIENT_EMAIL "teodor_cali@yahoo.com"
//RFID
#define SS_PIN  21
#define RST_PIN 22
//Stepper
#define IN1 5
#define IN2 4
#define IN3 2
#define IN4 15
//LED
#define ERROR_LED 25
#define EMPTY_LED 32


//Parameters
//Time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
//RFID
const int ipaddress[4] = {103, 97, 67, 25};
//IR
uint16_t RECV_PIN1 = 35;
uint16_t RECV_PIN2 = 34;
//Servo
int servoPin = 27;


//Variables
//Time
int curr_hour = 0;
int prev_hour = 0;
//Mail
SMTPSession smtp;
ESP_Mail_Session session;
SMTP_Message message;
String textMsg = "";
//RFID
byte nuidPICC[4] = {0, 0, 0, 0};
byte nuidPICC_saved[2][4] = {{1, 1, 1, 1},
                             {1, 1, 1, 1}};
MFRC522::MIFARE_Key key;
MFRC522 rfid = MFRC522(SS_PIN, RST_PIN);
//Stepper
Stepper myStepper(2048, IN1, IN3, IN2, IN4);
//Servo
Servo myservo;
int pos = 0;    // variable to store the servo position
bool servo_stat = 0;


void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(9600);
  // This delay gives the chance to wait for a Serial Monitor without blocking if none is found
  delay(1500);

  // Defined in thingProperties.h
  initProperties();

  // Connect to Arduino IoT Cloud
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);

  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  //Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //Mail
  /** Enable the debug via Serial port
   * none debug or 0
   * basic debug or 1
  */
  smtp.debug(1);

  /* Set the callback function to get the sending results */
  smtp.callback(smtpCallback);

  /* Set the session config */
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  /* Set the message headers */
  message.sender.name = "Matcha Feeder";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "Needing intervention";
  message.addRecipient("Teodor", RECIPIENT_EMAIL);
  
  //RFID
  SPI.begin();
  rfid.PCD_Init();

  //Stepper
  //set the speed at 5 rpm
  myStepper.setSpeed(5);

  //Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);

  //LED
  pinMode(ERROR_LED, OUTPUT);
  pinMode(EMPTY_LED, OUTPUT);
}


void loop() {
  ArduinoCloud.update();

  struct tm timeinfo;
  
  getLocalTime(&timeinfo);
  prev_hour = curr_hour;
  curr_hour = timeinfo.tm_hour;
  if (curr_hour != prev_hour) { // Hour changed
    //Stepper
    if (curr_hour == schedule) {
      for (int i = 0; i < 4; i++) { // Give portion in 5 rounds
        myStepper.step(quantity*5);
        if (analogRead(RECV_PIN2) < 3000) {
          error = 1;
          digitalWrite(ERROR_LED, LOW);
          Serial.println("Working fine");
        } else {
          error = 0;
          Serial.println("Error, try to unlock");
          myStepper.step(-100);
          myStepper.step(100);
          myStepper.step(-100);
          myStepper.step(quantity*5);
          if (analogRead(RECV_PIN2) < 3000) {
            error = 1;
            digitalWrite(ERROR_LED, LOW);
            Serial.println("Working fine now");
          } else {
            error = 0;
            digitalWrite(ERROR_LED, HIGH);
            Serial.println("Error, needs assistance");
            textMsg = "Error: food stuck, needing assistance!";
            message.text.content = textMsg.c_str();
            message.text.charSet = "us-ascii";
            message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
            message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
            message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

            // Connect to server with the session config
            if (!smtp.connect(&session))
              return;

            // Start sending Email and close the session
            if (!MailClient.sendMail(&smtp, &message))
              Serial.println("Error sending Email, " + smtp.errorReason());
    
            break;
          }
          delay(500);
        }
      }
    }
    
    //IR
    if (analogRead(RECV_PIN1) < 3800) {
      almost_empty = 1;
      digitalWrite(EMPTY_LED, LOW);
      Serial.println("Full");
    } else {
      almost_empty = 0;
      digitalWrite(EMPTY_LED, HIGH);
      Serial.println("Almost Empty");
      textMsg = "WARNING: Food supply almost gone, needing refill!";
      message.text.content = textMsg.c_str();
      message.text.charSet = "us-ascii";
      message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
      message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
      message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

      // Connect to server with the session config
      if (!smtp.connect(&session))
        return;

      // Start sending Email and close the session
      if (!MailClient.sendMail(&smtp, &message))
        Serial.println("Error sending Email, " + smtp.errorReason());
    }
  }

  //RFID
  readRFID();
  bool rfid_not_known = 1;

  for (byte i = 0; i < 2; i++) {
    bool rfid_different_byte = 0;
      
    for (byte j = 0; j < 4; j++) {
      if (nuidPICC_saved[i][j] != nuidPICC[j])
        rfid_different_byte = 1;
    }
    
    //Servo
    if (!rfid_different_byte) {
      rfid_not_known = 0;
      if (!servo_stat) {
        open_bowl();
        Serial.println("Open");
      }
    }
  }
  
  //Servo
  if (rfid_not_known && servo_stat) {
    close_bowl();
    Serial.println("Close");
  }
  
  delay(100);
}


void onQuantityChange()  {
}

void onScheduleChange()  {
}

void onAddRemovePetChange()  {
  if (add_remove_pet == 1) {
    bool empty_saved_found = 0;
    
    readRFID();
    for (byte i = 0; i < 2; i++) {
      bool not_empty = 0;
      bool not_saved = 0;
      
      for (byte j = 0; j < 4; j++) {
        Serial.println(nuidPICC_saved[i][j]);
        if (nuidPICC_saved[i][j] != nuidPICC[j])
          not_saved = 1;
        if (nuidPICC_saved[i][j] != 1)
          not_empty = 1;
      }
    
      if (!not_empty || !not_saved) {
        empty_saved_found = 1;
        for (byte j = 0; j < 4; j++)
          nuidPICC_saved[i][j] = nuidPICC[j];
          
        delay(100);
        add_remove_pet = 0;
        return;
      }
    }
    
    if (!empty_saved_found) {
      // FIXME: Add LED
      Serial.println("Memory is full, you need to remove");
    }
  }
  
  if (add_remove_pet == 2) {
    readRFID();
    for (byte i = 0; i < 2; i++) {
      bool diff_byte = 0;
      for (byte j = 0; j < 4; j++) {
        if (nuidPICC_saved[i][j] != nuidPICC[j])
          diff_byte = 1;
      }

      if (!diff_byte) {
        for (byte j = 0; j < 4; j++)
          nuidPICC_saved[i][j] = 1;
      }
    }
  }

  delay(100);
  add_remove_pet = 0;
}

//Servo
void open_bowl() {
  myservo.attach(servoPin, 500, 2400);
  servo_stat = 1;
  for (pos = 0; pos <= 100; pos += 1) {
    myservo.write(pos);
    delay(10);
  }
  myservo.detach();

}

void close_bowl() {
  myservo.attach(servoPin, 500, 2400);
  servo_stat = 0;
  for (pos = 100; pos >= 0; pos -= 1) {
    myservo.write(pos);
    delay(10);
  }
  myservo.detach();
}

//RFID
void readRFID(void ) {
  ////Read RFID card

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  // Look for new 1 cards
  if ( ! rfid.PICC_IsNewCardPresent())
    return;

  // Verify if the NUID has been readed
  if (  !rfid.PICC_ReadCardSerial())
    return;

  // Store NUID into nuidPICC array
  for (byte i = 0; i < 4; i++) {
    nuidPICC[i] = rfid.uid.uidByte[i];
  }

  Serial.print(F("RFID In dec: "));
  printDec(rfid.uid.uidByte, rfid.uid.size);
  Serial.println();

  // Halt PICC
  rfid.PICC_HaltA();

  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();

}

void printDec(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], DEC);
  }
}

//Mail
/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients);
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
    }
    Serial.println("----------------\n");
  }
}
