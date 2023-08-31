/* 
// 
// READ all the disclaimer before you start:
//
// This project is not intended for life-saving systems, as it has not been fully tested.
//
// Its the ***PROTOTYPE*** version of my IoT refrigerator temperature alarm project code.
// (version 2 is currently under development )
//
// I hope you find this Arduino code helpful, since I wrote it quickly to test the feasibility
// of my idea. I'm currently working on a more efficient version,
// which will take some time as I'm adding more remote controlling functions, 
// designing a new PCB, and a new plastic housing.
//
// This code gets the temperature from the refrigerator and freezer using the
// Dallas Temperature 18b20 sensors. When the temperature is outside the predefined 
// range for more than 5 minutes an alarm is sent by the built-in Telegram bot.
//
// A full explanation of all projects features in this project is available,
// including thermal tests, additional information, and research results.
//
// https://github.com/RamiLup/IoT-Refrigerator-Temperature-alarm/
//
// Don't forget to read my LICENSE file and all others' libraries LICENSE files,
// since they might have different LICENSE agreements.
//
// https://github.com/RamiLup/IoT-Refrigerator-Temperature-alarm/main/LICENSE 
//
// I have used Arduino IDE 2.1.1, with ESP32WROOM-DA module.
// There are also few libraries that you must manually install in your Arduino IDE:
// Ping: https://github.com/marian-craciunescu/ESP32Ping
// OLED: https://github.com/olikraus/u8g2/
// UniversalTelegramBot: https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
//
*/

#include "secrets.h"              // Add your own BOTtoken, CHAT_ID, SSID, SSID_PASS to the secrets.h file
#include <OneWire.h>              // DallasTemperature 18b20 temperature sensors
#include <DallasTemperature.h>    // DallasTemperature 18b20 temperature sensors
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"              // Disable brownout problems
#include "soc/rtc_cntl_reg.h"     // Disable brownout problems
#include <UniversalTelegramBot.h> // Telegram bot , LICENSE - https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/blob/master/LICENSE.md
#include "esp_adc_cal.h"          // Converting analog to digital
#include <ESP32Ping.h>            // ESP32Ping - Ping library for ESP32, LICENSE: https://github.com/marian-craciunescu/ESP32Ping/blob/master/LICENSE 
#include <U8g2lib.h>              // U8glib - OLED Arduino library V2.34.22, LICENSE https://github.com/olikraus/u8g2/blob/master/LICENSE

//define LED
#define LED_Alarm 18              // IO pin no. 18 , active low, for local alarms. (330ohm is connected in series with LED to vcc)

//esp32 chip id 
uint32_t chipId = 0;              // uint32 for the esp32 internal chip id number.

//google ping success flag
boolean pingGoogle = false;       

//Wifi
const char *ssid = my_ssid;           // Add to your secret.h #define my_ssid "your_ssid"; 
const char *password = my_password;   // Add to your secret.h #define my_password "your_password"; 

// Initialize Telegram BOT
String BOTtoken = BOTtokenSecret;     // Add your BOTtoken, and CHAT_ID from your BOT FATHER
String CHAT_ID = CHAT_IDSecret;
String text = "";
WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);


// Initialize timers
int TelegramBotRequestDelay = 5000;     // Check for new telegram message every 5 seconds.
int SensorRequestDelay = 3000;          // Check sensor temperature every 3 seconds.
int WifiTestRequestDelay = 10000;       // Check for internet connectivity every 10 seconds.
unsigned long lastTimeBotRan;           // Keeps the time of the last check for new messages in Telegram.
unsigned long lastTimeWifiTestRan;      // Keeps the time of the last check internet connectivity.
unsigned long lastTimeSesnorRan;        // Keeps the time the temperature sensors were checked.
unsigned long current_millis;           // Update this value every loop with current millis(); value
unsigned long startTime;                // Save starting time.
unsigned long elapsedTime;              // Calculate elapsedTime by  current_millis - startTime

// Initialize analog to digital (voltage meter)
#define voltage_divider_offset 2.174    // Should be a value of 2.000, but ADC input impedance loads the voltage divider, requiring a correction
#define TestVoltPin 32                  

// configure the OLED display
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);   // OLED settings
#define I2C_ADDRESS 0x3C                                                        // OLED display Address
#define RST_PIN -1                                                              // OLED with no reset pin
char buffer[40];                                                                // This buffer is used for converting chars to strings.

// Data wire is plugged into digital pin 5 on the Arduino
#define ONE_WIRE_BUS_A 14                   // define OneWire pin 14 for 2 external sensors
#define ONE_WIRE_BUS_B 15                   // define OneWire pin 15 for internal sensor

// Setup a OneWire device
OneWire oneWire_A(ONE_WIRE_BUS_A);            // define OneWire for 2 external sensors
OneWire oneWire_B(ONE_WIRE_BUS_B);            // define OneWire for internal sensor

// Pass oneWire reference to DallasTemperature library
DallasTemperature sensors(&oneWire_A);        // 2 external sensors.
DallasTemperature tmpRef(&oneWire_B);         // 1 internal sensor

// init sensors temperature values and alarm counter
int sensorsCount = 0;                         // how many external sensors has been detected ?
int temp0_alarm_on_interval_counter = 1;      // it must start with 1, since I could not calculate the modulo "%" for the Check_alarm_interval_counters
int temp1_alarm_on_interval_counter = 1;      // 

float Temp0;                                  // temperature value sensor 0 (external)
float Temp1;                                  // temperature value sensor 1 (external)
float Temp2;                                  // temperature value sensor 2 (internal)

float Temp0_maximum  = 14;    // maxmimum temperature allowd for Temp0 sensor.  ***Normal refrigerator temperature range*** 
float Temp0_minimum  = 0 ;    // minimum temperature allowd for Temp0 sensor.  

float Temp1_maximum  = 0;     // maxmimum temperature allowd for Temp1 sensor.  ***Normal freezer temperature range***
float Temp1_minimum  = -25 ;  // minimum temperature allowd for Temp1 sensor.

//read voltage from analog 'TestVoltPin' 
float ReadVoltage (byte) {    
  float calibration  = 1.000; // Adjust accuracy 
  float vref = 1100;
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  vref = adc_chars.vref; // Obtain the device ADC reference voltage
  return (analogRead(TestVoltPin) / 4095.0) * 3.3 * voltage_divider_offset * (1100 / vref) * calibration;  // ESP by design reference voltage in mV
}

//Check new telegram messages
void handleNewMessages(int numNewMessages) {
  Serial.print("Handle New Messages: ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);   
    String text = bot.messages[i].text;                 
    String from_name = bot.messages[i].from_name; 
        

    if (chat_id != CHAT_ID)
    // if true the current message sender (chat_id) is not authenticated member in this bot. 
    // ignore command, and send warning message to real authenticated (CHAT_ID) user.
      {
      bot.sendMessage(CHAT_ID, "got telegram message:" + text + " from Unauthorized user:" + chat_id + " name: " + from_name + "\n", "");
      delay(100);
      bot.sendMessage(chat_id, "Unauthorized user " + from_name + "\n", ""); 
      delay(100);
      Serial.println("got telegram message from Unauthorized user: " + chat_id + " Text: " + text + " name: " + from_name);
      continue;
      }
      
       else if (text == "/start") //send commands menu
            {
            String welcome = "Welcome to Temp_Sensor_V1, " + from_name + "\n";
            welcome += "Elapsed time from start up: " + String(elapsedTime/1000) + " Seconds\n";  
            welcome += "Use the following commands to interact with this bot\n";
            welcome += "/start  : This menu:\n";
            welcome += "/temps  : Get sensors last reading \n";
            welcome += "/wifi   : Get wifi ssid and level \n";
            welcome += "/switch : Switch between sensors \n";
            welcome += "/ledhigh : Set led high \n";
            welcome += "/ledlow  : Set led low \n";                
            bot.sendMessage(CHAT_ID, welcome, "");
            }
    
            else if (text == "/temps")  //send current temperature of all sensors.
              {
              Serial.println("got temps telegram command");
              String text_replay = "replay to received command: " + text + "\n";
              text_replay += "Temp0 Sensor reading is: " + String(Temp0)+"c" + "\n";
              text_replay += "Temp1 Sensor reading is: " + String(Temp1)+"c" + "\n";    
              text_replay += "Temp2 Sensor reading is: " + String(Temp2)+"c" + "\n";                                      
              bot.sendMessage(CHAT_ID, text_replay , "");    
              }

                      else if (text == "/wifi") //send wifi details.
                        {
                        Serial.println("got wifi telegram command");
                        String text_replay = "replay to received command: " + text + "\n";
                        text_replay += "Wifi network ssid is: " + String(ssid) + "\n";
                        text_replay += "Wifi network rssi is: " + String(WiFi.RSSI()) + "\n";      
                        bot.sendMessage(CHAT_ID, text_replay , "");      
                        }

                          else if (text == "/ledlow") // turn led ON
                                {
                                Serial.println("got ledlow telegram command");
                                digitalWrite(LED_Alarm, HIGH); // Turn the LED ON 
                                String text_replay = "replay to received command: " + text + "\n";     
                                bot.sendMessage(CHAT_ID, text_replay , "");                                    
                                }
    
                                  else if (text == "/ledhigh") // turn led Off
                                    {
                                    Serial.println("got ledhigh telegram command");
                                    digitalWrite(LED_Alarm, LOW); // Turn the LED OFF
                                    String text_replay = "replay to received command: " + text + "\n";     
                                    bot.sendMessage(CHAT_ID, text_replay , "");                                         
                                    }
                                                
                                      else if (text == "/switch")  //that will switch between the temp1 and temp0 max and min values.
                                          {
                                          Serial.println("got switch telegram command");
                                          String text_replay = "replay to received command: " + text + "\n";   
                                          bot.sendMessage(CHAT_ID, text_replay , ""); 
                                          float temp_float_value;
                                          temp_float_value = Temp0_maximum;
                                          Temp0_maximum = Temp1_maximum;
                                          Temp1_maximum = temp_float_value;
                                          
                                          temp_float_value = Temp0_minimum;
                                          Temp0_minimum = Temp1_minimum;
                                          Temp1_minimum = temp_float_value;
                                          }  
                                            else
                                                {
                                                // Print the received message
                                                Serial.println("got unknown telegram command: " + text + " from user name: " + from_name + " user id: "  + chat_id );
                                                }
  }
} 

void setup(void)
{
  pinMode(LED_Alarm, OUTPUT);
  digitalWrite(LED_Alarm, LOW);  // Turn LED ON  
  delay(500);
  digitalWrite(LED_Alarm, HIGH); // Turn LED OFF
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // fix Brownout during wifi startup. more info:  https://github.com/espressif/arduino-esp32/issues/863, if it does't help, replace usb cable, power supply...
  Serial.begin(115200); 

  u8g2.begin();               //OLED init
  delay(100);
  u8g2.clearBuffer();                 // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB08_tr); // choose oled font
  
  u8g2.drawStr(2,32,"Starting...");
  u8g2.sendBuffer();                  // transfer memory to the display  
  delay(1000);

  u8g2.clearBuffer();                 // clear the internal memory
  u8g2.drawStr(2,32,"Connecting to WiFi");
  u8g2.sendBuffer();                  // transfer memory to the display  
  delay(1000);
  
  
  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  delay(2000);
  while (WiFi.status() != WL_CONNECTED) 
    {
    WiFi.disconnect();
    delay(2000); 
    Serial.print(".");
    digitalWrite(LED_Alarm, LOW); // Turn LED ON
    WiFi.begin(ssid, password);
    delay(5000);       
    } 
      digitalWrite(LED_Alarm, HIGH); // Turn LED OFF
      Serial.println();
      if(Ping.ping("www.google.com")) 
          {
          Serial.println("Setup Ping Success!!");
          pingGoogle = true;
          } 
          else 
              {
              Serial.println("Ping Error!!");
              pingGoogle = false;
              WiFi.reconnect();
              delay(3000);
              }

  Serial.println("Connected to WiFi: " + String(ssid));
  Serial.println("RSSI: " + String(WiFi.RSSI()));  

  u8g2.clearBuffer();         // clear the internal memory 
  sprintf(buffer, "Connected to %s", String(ssid));
  u8g2.drawStr(2,32,buffer);

  sprintf(buffer, "RSSI:   %s", String(WiFi.RSSI()));
  u8g2.drawStr(2,42,buffer);
  
  u8g2.sendBuffer();            // transfer memory to the display  
  delay(1000); 
    
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org  
  
  sensors.begin();                  // Start up the external sensors library, this one defined as oneWire_A and has 2 sensors.
  tmpRef.begin();                   // Start up the internal sensor library, this one is oneWire_B and has 1 sensor
  delay(1000);

  
  sensors.setResolution(10);        //10 bit resolution (0.25°C step)
  tmpRef.setResolution(10);         //10 bit resolution (0.25°C step)
  delay(1000);

  //get temperature values for each sensor   
  sensors.requestTemperatures(); //request the temperature
  tmpRef.requestTemperatures();
  delay(500);
  
    //Starting timer for the elapsed time
    startTime = millis();
    
    //get current temperatures
     Temp0 = sensors.getTempCByIndex(0);  //oneWire_A     
     Temp1 = sensors.getTempCByIndex(1);  //oneWire_A
     Temp2 = tmpRef.getTempCByIndex(0);   //oneWire_B

     Serial.println (("TEMP0: ") + String(Temp0, 2));
     Serial.println (("TEMP1: ") + String(Temp1, 2));  
     Serial.println (("TEMP2: ") + String(Temp2, 2)); 
          
//---------------test TEMP0 status-----------------
//if sensor temperature is "-127.00" that because sensor is not connected
    if(Temp0 == -127.00)
     {
     Serial.println (("TEMP0 not connected ") + String(Temp0, 2)); 
     u8g2.drawStr(2,12,"TEMP0 not connected");
     u8g2.sendBuffer();          
     delay(1000); 
     }

      else if((Temp0 < -25) || (Temp0 > 30))
       {
       Serial.println(String("Temp0 is ") + String(Temp0, 2) + ("which is too cold or too hot for start"));
       u8g2.drawStr(2,12,"Temp0 too cold or hot");
       u8g2.sendBuffer();          // transfer internal memory to the display  
       }      
          else if((Temp0 > -30) || (Temp0 < 30))
          {
           Serial.println("Temp0 OK"); 
           u8g2.drawStr(2,12,"Temp0 OK");
           u8g2.sendBuffer();         
           sensorsCount = sensorsCount +1; 
          }
            else
              {
             Serial.println("unknown temp0 state"); 
             u8g2.drawStr(2,12,"unknown temp0 state");
             u8g2.sendBuffer();         
              }

//---------------test TEMP1 status-----------------

    if(Temp1 == -127.00)
     {
     Serial.println (("TEMP1 not connected ") + String(Temp1, 2)); 
     u8g2.drawStr(2,22,"TEMP1 not connected");
     u8g2.sendBuffer();          
     delay(1000); 
     }

      else if((Temp1 < -25) || (Temp1 > 30))
       {
       Serial.println(String("Temp1 is ") + String(Temp1, 2) + ("which is too cold or too hot for start"));
       u8g2.sendBuffer();          // transfer internal memory to the display  
       }      
          else if((Temp1 > -30) || (Temp1 < 30))
            {
            Serial.println("Temp1 OK"); 
            u8g2.drawStr(2,22,"Temp1 OK");
            u8g2.sendBuffer();         
            sensorsCount = sensorsCount +1; 
            }
              else
                  {
                  Serial.println("unknown temp1 state"); 
                  u8g2.drawStr(2,22,"unknown temp1 state");
                  u8g2.sendBuffer();         
                  }
  //------------------------------ end of temp0 && temp1 tetsing.

  delay(2000);
  if (sensorsCount == 2)
  {
  u8g2.clearBuffer(); 
  sprintf(buffer, "Found %d ext. sensors", sensorsCount);
  u8g2.drawStr(2,42,buffer);  
  u8g2.sendBuffer();          // transfer internal memory to the display 
  delay(2000);
  }

  if (sensorsCount < 2)
  {
  u8g2.clearBuffer(); 
  sprintf(buffer, "Found %d sensors", sensorsCount);
  u8g2.drawStr(2,42,buffer);  
  u8g2.drawStr(2,52,"Restarting....");  
  Serial.println("waiting for sensors! Restarting in 5 seconds");
  u8g2.sendBuffer();          // transfer internal memory to the display  
  delay(4000);

  u8g2.sendBuffer();          // transfer internal memory to the display  

  SendResartInfoWithTelegram(); // send a telegram message with current info.
  delay(2000);  
  
  ESP.restart();                // reset esp if sensorsCount is less than 2
  }

  delay(3000);
  SendWakeupHelloWithTelegram();
  delay(1000); 
}

void SendResartInfoWithTelegram()
  {
      Temp0 = sensors.getTempCByIndex(0); 
      Temp1 = sensors.getTempCByIndex(1);
      Temp2 = tmpRef.getTempCByIndex(0);
                         
      for (int i = 0; i < 17; i = i + 8) 
        {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
        }

      float adjusted_voltage = ReadVoltage(TestVoltPin);
      
      Serial.println("Send current data to Telegram. since I can't start sensors, ESP will be resatarted in few seconds.");
      String text_replay = "Module can't be started ! \n";

      text_replay += "External sensors: " + String(sensorsCount) + "\n"; 
      text_replay += "Temp0 Sensor last reading: " + String(Temp0)+"c" + "\n";      
      text_replay += "Temp1 Sensor last reading: " + String(Temp1)+"c" + "\n";
      text_replay += "Temp2 Sensor last reading: " + String(Temp2)+"c" + "\n"; 
      text_replay += "Wifi network ssid is: " + String(ssid) + "\n";
      text_replay += "Wifi network rssi is: " + String(WiFi.RSSI()) + "\n";   
      text_replay += "adjusted_voltage: " + String(adjusted_voltage) + "\n";   
      text_replay += "ESP32 Chip model is: \n" + String(ESP.getChipModel())+ "\n" + "Chip Rev: "+ String (ESP.getChipRevision()) + "\n";  
      text_replay += "This ESP32 chip has: " + String(ESP.getChipCores()) + " cores" + "\n";   
      text_replay += "Chip ID: " + String(chipId) + "\n";       
      text_replay += "Mac Address: " + String(WiFi.macAddress()) + "\n";  
      //text_replay += "CPU0 Reset_reason: \n" + String(get_reset_reason(rtc_get_reset_reason(0))) + "\n";  
      //text_replay += "CPU1 Reset_reason: \n" + String(get_reset_reason(rtc_get_reset_reason(1))) + "\n";  
      text_replay += "elapsedTime in seconds: " + String(elapsedTime/1000) + "\n";  
      bot.sendMessage(CHAT_ID, text_replay , "");        
  }
  

void SendWakeupHelloWithTelegram()
  {

    sensors.requestTemperatures(); //request the temperature   
    delay(100);
    tmpRef.requestTemperatures();   
    delay(100);
  
    Temp0 = sensors.getTempCByIndex(0); 
    delay(100);
    Temp1 = sensors.getTempCByIndex(1);
    delay(100);
    Temp2 = tmpRef.getTempCByIndex(0);
    delay(100);    
              
      for (int i = 0; i < 17; i = i + 8) 
        {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
        }

      float adjusted_voltage   = ReadVoltage(TestVoltPin);
      
      Serial.println("Starting...send telegram message with current parameters.");
      String text_replay = "Temperature monitoring system has started \n";
      text_replay += "Module wakeup took: " +  String(startTime/1000)  + " seconds \n";  
      text_replay += "External sensors: " + String(sensorsCount) + "\n"; 
      text_replay += "Temp0 Sensor last reading: " + String(Temp0)+"c" + "\n";      
      text_replay += "Temp1 Sensor last reading: " + String(Temp1)+"c" + "\n";
      text_replay += "Temp2 Sensor last reading: " + String(Temp2)+"c" + "\n";        
      text_replay += "Wifi network ssid is: " + String(ssid) + "\n";
      text_replay += "Wifi network rssi is: " + String(WiFi.RSSI()) + "\n";   
      text_replay += "adjusted_voltage: " + String(adjusted_voltage) + "\n";   
      text_replay += "ESP32 Chip model is: \n" + String(ESP.getChipModel())+ "\n" + "Chip Rev: "+ String (ESP.getChipRevision()) + "\n";
      text_replay += "This ESP32 chip has: " + String(ESP.getChipCores()) + " cores" + "\n";   
      text_replay += "Chip ID: " + String(chipId) + "\n";       
      text_replay += "Mac Address: " + String(WiFi.macAddress()) + "\n";  
      bot.sendMessage(CHAT_ID, text_replay , "");        
  }


void update_millis_time()
{
    current_millis = millis();
}

void loop()
{ 
  update_millis_time();
  Serial.println("Loop time: " + String(current_millis));    
  PrintOLED();
  
//-----------------------------------sensor test--------------  

  if (current_millis > lastTimeSesnorRan + SensorRequestDelay)  
    {     
      Serial.println("Sensor event");   
      ReadSensors();
      PrintSerial();
      Check_alarm_interval_counters();  
      lastTimeSesnorRan = current_millis;
    }
///-----------------------------------------
  
  if (current_millis > lastTimeBotRan + TelegramBotRequestDelay)  
    {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1); 
    Serial.println("Telegram new messages test event");
    while (numNewMessages) 
      {
      Serial.println("got new telegram message");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
    lastTimeBotRan = current_millis;
    }

  
  if (current_millis > lastTimeWifiTestRan + WifiTestRequestDelay)   
    {
      Serial.println();
      Serial.print("My ip: ");  
      Serial.println(WiFi.localIP());
      Serial.print("Pinging ip: ");
      Serial.println("www.google.com");

      if(Ping.ping("www.google.com")) 
          {
          Serial.println("Ping Success!!");
          pingGoogle = true;
          } 
          else 
              {
              Serial.println("Ping Error!!");
              pingGoogle = false;
              WiFi.reconnect();
              }

              lastTimeWifiTestRan = current_millis;
    }
}    

// Out of range: first warning (local)
// if any x_alarm_on_interval_counter is more than 5, (which is equal to 15 seconds):
//    Send a local alarm to the serial monitor port.

// Out of range: second warning (Telegram)
// If one of the xxx_alarm_on_interval_counter counters is more than 5.
// If The last alarm was sent more than 5 minutes ago (that is the reason for the modulo 111).
//    Send telegram Alarm

// Telegram alarm time calculation:
//    * temperature sensor check up every 3 seconds.
//    * Check if the counter is divisible by 111 (which means 111 * 3 seconds = 333 seconds = 5.5 minutes).


void Check_alarm_interval_counters()  
{
  if ((temp0_alarm_on_interval_counter  > 5) || (temp1_alarm_on_interval_counter  > 5))
	  {
    Serial.println("tempX_alarm_on_interval_counter > 5"); 
	  }

	    if ((temp0_alarm_on_interval_counter % 111 == 0) || (temp1_alarm_on_interval_counter %  111 == 0))  /////// [[[ % 111 is about 5 min between telegram messages sending]]]
      		{	
      		Serial.println("sendStatusViaTelegram now...");
      		Serial.println("temp0_alarm_on_interval_counter " + String(temp0_alarm_on_interval_counter));
      		Serial.println("temp1_alarm_on_interval_counter " + String(temp1_alarm_on_interval_counter));  
          String text_replay = "Temperature Warnning! \n" ;
      		text_replay += "Temp0 Sensor reading is: " + String(Temp0)+"c" + "\n";   
      		text_replay += "Temp1 Sensor reading is: " + String(Temp1)+"c" + "\n";
          text_replay += "Temp2 Sensor reading is: " + String(Temp2)+"c" + "\n";          
      		text_replay += "Temp0 on_interval_counters: " + String(temp0_alarm_on_interval_counter) + "\n"; 
      		text_replay += "Temp1 on_interval_counters: " + String(temp1_alarm_on_interval_counter) + "\n";       
      		text_replay += "Elapsed Time in seconds: " + String(elapsedTime/1000) + "\n";  
      		bot.sendMessage(CHAT_ID, text_replay , "");  
      		}
}

void ReadSensors()
{  
  elapsedTime = current_millis - startTime;

  Serial.println("elapsedTime: " + String(elapsedTime));  
  Serial.println("current_millis: " + String(current_millis));  
  Serial.println("startTime: " + String(startTime));  
              
  //Collect the values for each sensors    
  sensors.requestTemperatures(); //request the temperature   
  delay(100);
  tmpRef.requestTemperatures();   
  delay(100);
  
  //Filling up the variables
  Temp0 = sensors.getTempCByIndex(0);
  Temp1 = sensors.getTempCByIndex(1); 
  Temp2 = tmpRef.getTempCByIndex(0); 
  
    if((Temp0 < Temp0_minimum) || (Temp0 > Temp0_maximum))
    {
     Serial.println (("Warning! Temp0 is: ") + String(Temp0, 2));
     Serial.println (("Warning! Temp1 is: ") + String(Temp1, 2)); 
     Serial.println (("Warning! Temp2 is: ") + String(Temp2, 2));
     temp0_alarm_on_interval_counter = temp0_alarm_on_interval_counter +1;
    }
      else
        {
        temp0_alarm_on_interval_counter = 1; 
        delay(100);                   
        }    

    if((Temp1 <Temp1_minimum) || (Temp1 > Temp1_maximum))
    {
     Serial.println (("Warning! Temp0 is: ") + String(Temp0, 2));
     Serial.println (("Warning! Temp1 is: ") + String(Temp1, 2)); 
     Serial.println (("Warning! Temp2 is: ") + String(Temp2, 2));
    temp1_alarm_on_interval_counter = temp1_alarm_on_interval_counter +1;  
    }     
    else
      {
      temp1_alarm_on_interval_counter = 1;    
      delay(100);                        
      }
}


void PrintOLED()
{
  u8g2.clearBuffer();                       // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB08_tr);       // choose a suitable font

  sprintf(buffer, "Temp0 %.1f (%.0f...+%.0f)", Temp0,Temp0_minimum,Temp0_maximum);
  u8g2.drawStr(2,12,buffer);

  sprintf(buffer, "Temp1 %.1f (%.0f...+%.0f)", Temp1,Temp1_minimum,Temp1_maximum);
  u8g2.drawStr(2,22,buffer);

  sprintf(buffer, "Time: %d", current_millis/1000);
  u8g2.drawStr(2,42,buffer);

  
  if (temp0_alarm_on_interval_counter > 1) 
    {
    sprintf(buffer, "Temp0 Counter   %d", temp0_alarm_on_interval_counter);
    u8g2.drawStr(2,52,buffer);
    }

  if (temp1_alarm_on_interval_counter > 1)
    {
    sprintf(buffer, "Temp1 Counter   %d", temp1_alarm_on_interval_counter);
    u8g2.drawStr(2,62,buffer);    
    }

  if (pingGoogle == true)
    {
    sprintf(buffer, "Ping Test OK ");
    u8g2.drawStr(2,32,buffer); 
    digitalWrite(LED_Alarm, HIGH); // Turn LED OFF   
    }
    else
        {
        sprintf(buffer, "Ping Test Failed");
        u8g2.drawStr(2,32,buffer);
        digitalWrite(LED_Alarm, LOW); // Turn LED ON
        }          
    u8g2.sendBuffer();          // transfer internal memory to the display   
}

void PrintSerial()
{
  Serial.print(elapsedTime/1000); //time in seconds
  Serial.print(" ");  //space
  Serial.print(Temp0,2); //temperature, 2 digits 
  Serial.print(" ");  
  Serial.print(Temp1,2);
  Serial.println(" ");
}
