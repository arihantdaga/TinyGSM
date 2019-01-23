/**************************************************************
 *
 * TinyGSM Getting Started guide:
 *   http://tiny.cc/tiny-gsm-readme
 *
 * NOTE:
 * Some of the functions may be unavailable for your modem.
 * Just comment them out.
 *
 **************************************************************/

// Select your modem:
#define TINY_GSM_MODEM_SIM800
// #define TINY_GSM_MODEM_SIM808
// #define TINY_GSM_MODEM_SIM900
// #define TINY_GSM_MODEM_A6
// #define TINY_GSM_MODEM_A7
// #define TINY_GSM_MODEM_M590

// Set serial for debug console (to the Serial Monitor, speed 115200)
// #define SerialMon Serial

// // Set serial for AT commands (to the module)
// // Use Hardware Serial on Mega, Leonardo, Micro
// #define SerialAT Serial1

HardwareSerial SerialAT(PA3, PA2);
HardwareSerial SerialMon(PA10, PA9);

// or Software Serial on Uno, Nano
//#include <SoftwareSerial.h>
//SoftwareSerial SerialAT(2, 3); // RX, TX

#define DUMP_AT_COMMANDS
// #ifdef DUMP_AT_COMMANDS
// #undef DUMP_AT_COMMANDS
// #endif

#define TINY_GSM_DEBUG SerialMon

// Set phone numbers, if you want to test SMS and Calls
//#define SMS_TARGET  "+380xxxxxxxxx"
//#define CALL_TARGET "+380xxxxxxxxx"

// Your GPRS credentials
// Leave empty, if missing user or pass
const char apn[] = "airtelgprs.com";
const char user[] = "";
const char pass[] = "";

#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif
int modem_status = 0;

void setup()
{
  // Set console baud rate
  SerialMon.begin(115200);
  delay(10);
  // pinMode(PA12, INPUT);
  // attachInterrupt(PB12, MODEM_STATUS, CHANGE);

  // Set your reset, enable, power pins here

  delay(3000);

  // Set GSM module baud rate
  TinyGsmAutoBaud(SerialAT);
  DBG("Initializing modem...");
  if (!modem.restart())
  {
    delay(5000);
    return;
  }

  // modem_status = digitalRead(PB12);
}

void loop()
{
  // if(SerialMon.available()){
  //         String str = SerialMon.readStringUntil('\n');
  //         if(str.equals("READ_MSG")){
  //                 SerialMon.println("Hello");
  //                 modem.checkUnreadMessage();
  //         }else{
  //           SerialMon.println(str);
  //         }
  // }

  //   // Restart takes quite some time
  //   // To skip it, call init() instead of restart()
  //   SerialMon.print("Initial Statte of pin ");
  //   SerialMon.println(digitalRead(PB12));

  String modemInfo = modem.getModemInfo();
  DBG("Modem:", modemInfo);

  //   // Unlock your SIM card with a PIN
  //   //modem.simUnlock("1234");

  DBG("Waiting for network...");
  if (!modem.waitForNetwork())
  {
    delay(10000);
    return;
  }

  if (modem.isNetworkConnected())
  {
    DBG("Network connected");
  }
  for (int i = 0; i < 2; i++)
  {
    Sms sms_array[20];
    int limit = 20;
    int number_of_unread = modem.checkUnreadMessage(sms_array, limit, false);
    for (int i = 0; i < number_of_unread; i++)
    {
      SerialMon.print("SMS");
      SerialMon.print(i+1);
      SerialMon.print(" is: ");
      SerialMon.println(sms_array[i].message);
      SerialMon.print("SMS index is ");
      SerialMon.println(sms_array[i].position);
    }
    delay(5000);
    // modem.readSmsMessage(7, false);
    // delay(5000);
  }

  //   DBG("Connecting to", apn);
  //   if (!modem.gprsConnect(apn, user, pass)) {
  //     delay(10000);
  //     return;
  //   }

  //   bool res;

  //   String ccid = modem.getSimCCID();
  //   DBG("CCID:", ccid);

  //   String imei = modem.getIMEI();
  //   DBG("IMEI:", imei);

  //   String cop = modem.getOperator();
  //   DBG("Operator:", cop);

  //   IPAddress local = modem.localIP();
  //   DBG("Local IP:", local);

  //   int csq = modem.getSignalQuality();
  //   DBG("Signal quality:", csq);

  //   // This is NOT supported on M590
  //   int battLevel = modem.getBattPercent();
  //   DBG("Battery lavel:", battLevel);

  //   // This is only supported on SIMxxx series
  //   float battVoltage = modem.getBattVoltage() / 1000.0F;
  //   DBG("Battery voltage:", battVoltage);

  //   // This is only supported on SIMxxx series
  //   String gsmLoc = modem.getGsmLocation();
  //   DBG("GSM location:", gsmLoc);

  //   // This is only supported on SIMxxx series
  //   String gsmTime = modem.getGSMDateTime(DATE_TIME);
  //   DBG("GSM Time:", gsmTime);
  //   String gsmDate = modem.getGSMDateTime(DATE_DATE);
  //   DBG("GSM Date:", gsmDate);

  //   String ussd_balance = modem.sendUSSD("*111#");
  //   DBG("Balance (USSD):", ussd_balance);

  //   String ussd_phone_num = modem.sendUSSD("*161#");
  //   DBG("Phone number (USSD):", ussd_phone_num);

  // #if defined(TINY_GSM_MODEM_SIM808)
  //   modem.enableGPS();
  //   String gps_raw = modem.getGPSraw();
  //   modem.disableGPS();
  //   DBG("GPS raw data:", gps_raw);
  // #endif

  // #if defined(SMS_TARGET)
  //   res = modem.sendSMS(SMS_TARGET, String("Hello from ") + imei);
  //   DBG("SMS:", res ? "OK" : "fail");

  //   // This is only supported on SIMxxx series
  //   res = modem.sendSMS_UTF16(SMS_TARGET, u"Привіііт!", 9);
  //   DBG("UTF16 SMS:", res ? "OK" : "fail");
  // #endif

  // #if defined(CALL_TARGET)
  //   DBG("Calling:", CALL_TARGET);

  //   // This is NOT supported on M590
  //   res = modem.callNumber(CALL_TARGET);
  //   DBG("Call:", res ? "OK" : "fail");

  //   if (res) {
  //     delay(1000L);

  //     // Play DTMF A, duration 1000ms
  //     modem.dtmfSend('A', 1000);

  //     // Play DTMF 0..4, default duration (100ms)
  //     for (char tone='0'; tone<='4'; tone++) {
  //       modem.dtmfSend(tone);
  //     }

  //     delay(5000);

  //     res = modem.callHangup();
  //     DBG("Hang up:", res ? "OK" : "fail");
  //   }
  // #endif

  //   modem.gprsDisconnect();
  //   if (!modem.isGprsConnected()) {
  //     DBG("GPRS disconnected");
  //   } else {
  //     DBG("GPRS disconnect: Failed.");
  //   }

  //   // Try to power-off (modem may decide to restart automatically)
  //   // To turn off modem completely, please use Reset/Enable pins
  //   // modem.poweroff();
  //   // DBG("Poweroff.");

  //   // Do nothing forevermore
  //   // while (true) {
  //   //   modem.maintain();
  //   // }
  //   modem.maintain();
  // delay(3000); */
  // // static bool modem_status = false;
  // SerialMon.println(digitalRead(PA12));
  // delay(200);

  // if(modem_status!= (bool)digitalRead(PB12)){
  //   modem_status = (bool)digitalRead(PB12);
  //   SerialMon.println(modem_status);
  // }
}
