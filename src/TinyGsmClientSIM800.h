/**
 * @file       TinyGsmClientSIM800.h
 * @author     Volodymyr Shymanskyy
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy
 * @date       Nov 2016
 */

#ifndef TinyGsmClientSIM800_h
#define TinyGsmClientSIM800_h

//#define TINY_GSM_DEBUG Serial
//#define TINY_GSM_USE_HEX

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 64
#endif

#define TINY_GSM_MUX_COUNT 5

#ifndef TINY_GSM_PHONEBOOK_RESULTS
#define TINY_GSM_PHONEBOOK_RESULTS 5
#endif

#include <TinyGsmCommon.h>

#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

// New SMS Callback
#if defined(ESP8266) || defined(ESP32)
#include <functional>
#define NEW_SMS_CALLBACK_SIGNATURE std::function<void(unsigned int)> callback
#else
#define NEW_SMS_CALLBACK_SIGNATURE void (*callback)(unsigned int)
#endif


enum SimStatus
{
  SIM_ERROR = 0,
  SIM_READY = 1,
  SIM_LOCKED = 2,
};

enum RegStatus
{
  REG_UNREGISTERED = 0,
  REG_SEARCHING = 2,
  REG_DENIED = 3,
  REG_OK_HOME = 1,
  REG_OK_ROAMING = 5,
  REG_UNKNOWN = 4,
};

enum TinyGSMDateTimeFormat
{
  DATE_FULL = 0,
  DATE_TIME = 1,
  DATE_DATE = 2
};

enum class PhonebookStorageType : uint8_t
{
  SIM,   // Typical size: 250
  Phone, // Typical size: 100
  Invalid
};

struct PhonebookStorage
{
  PhonebookStorageType type = PhonebookStorageType::Invalid;
  uint8_t used = {0};
  uint8_t total = {0};
};

struct PhonebookEntry
{
  String number;
  String text;
};

struct PhonebookMatches
{
  uint8_t index[TINY_GSM_PHONEBOOK_RESULTS] = {0};
  int no_of_matches;
};

enum class MessageStorageType : uint8_t
{
  SIM,                // SM
  Phone,              // ME
  SIMPreferred,       // SM_P
  PhonePreferred,     // ME_P
  Either_SIMPreferred // MT (use both)
};

struct MessageStorage
{
  /*
   * [0]: Messages to be read and deleted from this memory storage
   * [1]: Messages will be written and sent to this memory storage
   * [2]: Received messages will be placed in this memory storage
   */
  MessageStorageType type[3];
  uint8_t used[3] = {0};
  uint8_t total[3] = {0};
};

enum class DeleteAllSmsMethod : uint8_t
{
  Read = 1,
  Unread = 2,
  Sent = 3,
  Unsent = 4,
  Received = 5,
  All = 6
};

class TinyGsmSim800
{

public:
#ifndef TINY_GSM_NO_GPRS
  class GsmClient : public Client
  {
    friend class TinyGsmSim800;
    typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

  public:
    GsmClient() {}

    GsmClient(TinyGsmSim800 &modem, uint8_t mux = 1)
    {
      init(&modem, mux);
    }

    bool init(TinyGsmSim800 *modem, uint8_t mux = 1)
    {
      this->at = modem;
      this->mux = mux;
      sock_available = 0;
      prev_check = 0;
      sock_connected = false;
      got_data = false;

      at->sockets[mux] = this;

      return true;
    }

  public:
    virtual int connect(const char *host, uint16_t port)
    {
      stop();
      TINY_GSM_YIELD();
      rx.clear();
      sock_connected = at->modemConnect(host, port, mux);
      return sock_connected;
    }

    virtual int connect(IPAddress ip, uint16_t port)
    {
      String host;
      host.reserve(16);
      host += ip[0];
      host += ".";
      host += ip[1];
      host += ".";
      host += ip[2];
      host += ".";
      host += ip[3];
      return connect(host.c_str(), port);
    }

    virtual void stop()
    {
      TINY_GSM_YIELD();
      at->sendAT(GF("+CIPCLOSE="), mux);
      sock_connected = false;
      at->waitResponse();
      rx.clear();
    }

    virtual size_t write(const uint8_t *buf, size_t size)
    {
      TINY_GSM_YIELD();
      at->maintain();
      return at->modemSend(buf, size, mux);
    }

    virtual size_t write(uint8_t c)
    {
      return write(&c, 1);
    }

    virtual size_t write(const char *str)
    {
      if (str == NULL)
        return 0;
      return write((const uint8_t *)str, strlen(str));
    }

    virtual int available()
    {
      TINY_GSM_YIELD();
      if (!rx.size() && sock_connected)
      {
        // Workaround: sometimes SIM800 forgets to notify about data arrival.
        // TODO: Currently we ping the module periodically,
        // but maybe there's a better indicator that we need to poll
        if (millis() - prev_check > 500)
        {
          got_data = true;
          prev_check = millis();
        }
        at->maintain();
      }
      return rx.size() + sock_available;
    }

    virtual int read(uint8_t *buf, size_t size)
    {
      TINY_GSM_YIELD();
      at->maintain();
      size_t cnt = 0;
      while (cnt < size && sock_connected)
      {
        size_t chunk = TinyGsmMin(size - cnt, rx.size());
        if (chunk > 0)
        {
          rx.get(buf, chunk);
          buf += chunk;
          cnt += chunk;
          continue;
        }
        // TODO: Read directly into user buffer?
        at->maintain();
        if (sock_available > 0)
        {
          at->modemRead(rx.free(), mux);
        }
        else
        {
          break;
        }
      }
      return cnt;
    }

    virtual int read()
    {
      uint8_t c;
      if (read(&c, 1) == 1)
      {
        return c;
      }
      return -1;
    }

    virtual int peek() { return -1; } //TODO
    virtual void flush() { at->stream.flush(); }

    virtual uint8_t connected()
    {
      if (available())
      {
        return true;
      }
      return sock_connected;
    }
    virtual operator bool() { return connected(); }

    /*
   * Extended API
   */

    String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;

  private:
    TinyGsmSim800 *at;
    uint8_t mux;
    uint16_t sock_available;
    uint32_t prev_check;
    bool sock_connected;
    bool got_data;
    RxFifo rx;
  };

  class GsmClientSecure : public GsmClient
  {
  public:
    GsmClientSecure() {}

    GsmClientSecure(TinyGsmSim800 &modem, uint8_t mux = 1)
        : GsmClient(modem, mux)
    {
    }

  public:
    virtual int connect(const char *host, uint16_t port)
    {
      stop();
      TINY_GSM_YIELD();
      rx.clear();
      sock_connected = at->modemConnect(host, port, mux, true);
      return sock_connected;
    }
  };
#endif // TINY_GSM_NO_GPRS

public:
  TinyGsmSim800(Stream &stream)
      : stream(stream)
  {
#ifndef TINY_GSM_NO_GPRS
    memset(sockets, 0, sizeof(sockets));
#endif // TINY_GSM_NO_GPRS

setNewSMSCallback(NULL);
  }

  /*
   * Basic functions
   */
  bool begin()
  {
    return init();
  }

  bool init()
  {
    if (!testAT())
    {
      return false;
    }

    sendAT(GF("&FZ")); // Factory + Reset
    waitResponse();

    sendAT(GF("E0")); // Echo Off
    if (waitResponse() != 1)
    {
      return false;
    }

    const SimStatus simStatus = getSimStatus();
    if (simStatus == SimStatus::SIM_READY)
    {
      // if (waitResponse(10000L, GF("SMS Ready")) != 1)
      // {
      //   return false;
      // }

      sendAT(GF("+CMGF=1")); // Select SMS Message Format: Text mode
      if (waitResponse() != 1)
      {
        return false;
      }

      // sendAT(GF("+CSDH=1")); // Show SMS Text Mode Parameters
      // if (waitResponse() != 1)
      // {
      //   return false;
      // }
    }

    return true;
  }

  void setBaud(unsigned long baud)
  {
    sendAT(GF("+IPR="), baud);
  }

  bool testAT(unsigned long timeout = 10000L)
  {
    //streamWrite(GF("AAAAA" GSM_NL));  // TODO: extra A's to help detect the baud rate
    for (unsigned long start = millis(); millis() - start < timeout;)
    {
      sendAT(GF(""));
      if (waitResponse(200) == 1)
      {
        delay(100);
        return true;
      }
      delay(100);
    }
    return false;
  }

  void maintain()
  {
#ifndef TINY_GSM_NO_GPRS
    for (int mux = 0; mux < TINY_GSM_MUX_COUNT; mux++)
    {
      GsmClient *sock = sockets[mux];
      if (sock && sock->got_data)
      {
        sock->got_data = false;
        sock->sock_available = modemGetAvailable(mux);
      }
    }
#endif // TINY_GSM_NO_GPRS
    while (stream.available())
    {
      waitResponse(10, NULL, NULL);
    }
  }

  bool factoryDefault()
  {
    sendAT(GF("&FZE0&W")); // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("+IPR=0")); // Auto-baud
    waitResponse();
    sendAT(GF("+IFC=0,0")); // No Flow Control
    waitResponse();
    sendAT(GF("+ICF=3,3")); // 8 data 0 parity 1 stop
    waitResponse();
    sendAT(GF("+CSCLK=0")); // Disable Slow Clock
    waitResponse();
    sendAT(GF("&W")); // Write configuration
    return waitResponse() == 1;
  }

  String getModemInfo()
  {
    sendAT(GF("I"));
    String res;
    if (waitResponse(1000L, res) != 1)
    {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }

  bool hasSSL()
  {
#if defined(TINY_GSM_MODEM_SIM900)
    return false;
#else
    sendAT(GF("+CIPSSL=?"));
    if (waitResponse(GF(GSM_NL "+CIPSSL:")) != 1)
    {
      return false;
    }
    return waitResponse() == 1;
#endif
  }

  /*
   * Power functions
   */

  bool restart()
  {
    if (!testAT())
    {
      return false;
    }
    //Enable Local Time Stamp for getting network time
    // TODO: Find a better place for this
    sendAT(GF("+CLTS=1"));
    if (waitResponse(10000L) != 1)
    {
      return false;
    }
    sendAT(GF("&W"));
    waitResponse();
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1)
    {
      return false;
    }
    sendAT(GF("+CFUN=1,1"));
    if (waitResponse(10000L) != 1)
    {
      return false;
    }
    delay(3000);
    return init();
  }

  bool saveConfig(){
    sendAT(GF("&W"));
    return waitResponse() == 1;
  }

  bool poweroff()
  {
    sendAT(GF("+CPOWD=1"));
    return waitResponse(GF("NORMAL POWER DOWN")) == 1;
  }

  bool radioOff()
  {
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1)
    {
      return false;
    }
    delay(3000);
    return true;
  }

  /*
    During sleep, the SIM800 module has its serial communication disabled. In order to reestablish communication
    pull the DRT-pin of the SIM800 module LOW for at least 50ms. Then use this function to disable sleep mode.
    The DTR-pin can then be released again.
  */
  bool sleepEnable(bool enable = true)
  {
    sendAT(GF("+CSCLK="), enable);
    return waitResponse() == 1;
  }

  bool netlightEnable(bool enable = true)
  {
    sendAT(GF("+CNETLIGHT="), enable);
    bool ok = waitResponse() == 1;

    sendAT(GF("+CSGS="), enable);
    ok &= waitResponse() == 1;

    return ok;
  }

  /*
   * SIM card functions
   */

  bool simUnlock(const char *pin)
  {
    sendAT(GF("+CPIN=\""), pin, GF("\""));
    return waitResponse() == 1;
  }

  String getSimCCID()
  {
    sendAT(GF("+ICCID"));
    if (waitResponse(GF(GSM_NL "+ICCID:")) != 1)
    {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  String getIMEI()
  {
    sendAT(GF("+GSN"));
    if (waitResponse(GF(GSM_NL)) != 1)
    {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  SimStatus getSimStatus(unsigned long timeout = 10000L)
  {
    for (unsigned long start = millis(); millis() - start < timeout;)
    {
      sendAT(GF("+CPIN?"));
      if (waitResponse(GF(GSM_NL "+CPIN:")) != 1)
      {
        delay(1000);
        continue;
      }
      int status = waitResponse(GF("READY"), GF("SIM PIN"), GF("SIM PUK"), GF("NOT INSERTED"));
      waitResponse();
      switch (status)
      {
      case 2:
      case 3:
        return SIM_LOCKED;
      case 1:
        return SIM_READY;
      default:
        return SIM_ERROR;
      }
    }
    return SIM_ERROR;
  }

  RegStatus getRegistrationStatus()
  {
    sendAT(GF("+CREG?"));
    if (waitResponse(GF(GSM_NL "+CREG:")) != 1)
    {
      return REG_UNKNOWN;
    }
    streamSkipUntil(','); // Skip format (0)
    int status = stream.readStringUntil('\n').toInt();
    waitResponse();
    return (RegStatus)status;
  }

  String getOperator()
  {
    sendAT(GF("+COPS?"));
    if (waitResponse(GF(GSM_NL "+COPS:")) != 1)
    {
      return "";
    }
    streamSkipUntil('"'); // Skip mode and format
    String res = stream.readStringUntil('"');
    waitResponse();
    return res;
  }

  /*
   * Generic network functions
   */

  int getSignalQuality()
  {
    sendAT(GF("+CSQ"));
    if (waitResponse(GF(GSM_NL "+CSQ:")) != 1)
    {
      return 99;
    }
    int res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

  bool isNetworkConnected()
  {
    RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

  bool waitForNetwork(unsigned long timeout = 60000L)
  {
    for (unsigned long start = millis(); millis() - start < timeout;)
    {
      if (isNetworkConnected())
      {
        return true;
      }
      delay(250);
    }
    return false;
  }

  /*
   * GPRS functions
   */
  bool gprsConnect(const char *apn, const char *user = NULL, const char *pwd = NULL)
  {
    gprsDisconnect();

    // Set the Bearer for the IP
    sendAT(GF("+SAPBR=3,1,\"Contype\",\"GPRS\"")); // Set the connection type to GPRS
    waitResponse();

    sendAT(GF("+SAPBR=3,1,\"APN\",\""), apn, '"'); // Set the APN
    waitResponse();

    if (user && strlen(user) > 0)
    {
      sendAT(GF("+SAPBR=3,1,\"USER\",\""), user, '"'); // Set the user name
      waitResponse();
    }
    if (pwd && strlen(pwd) > 0)
    {
      sendAT(GF("+SAPBR=3,1,\"PWD\",\""), pwd, '"'); // Set the password
      waitResponse();
    }

    // Define the PDP context
    sendAT(GF("+CGDCONT=1,\"IP\",\""), apn, '"');
    waitResponse();

    // Activate the PDP context
    sendAT(GF("+CGACT=1,1"));
    waitResponse(60000L);

    // Open the definied GPRS bearer context
    sendAT(GF("+SAPBR=1,1"));
    waitResponse(85000L);
    // Query the GPRS bearer context status
    sendAT(GF("+SAPBR=2,1"));
    if (waitResponse(30000L) != 1)
      return false;

    // Attach to GPRS
    sendAT(GF("+CGATT=1"));
    if (waitResponse(60000L) != 1)
      return false;

    // TODO: wait AT+CGATT?

    // Set to multi-IP
    sendAT(GF("+CIPMUX=1"));
    if (waitResponse() != 1)
    {
      return false;
    }

    // Put in "quick send" mode (thus no extra "Send OK")
    sendAT(GF("+CIPQSEND=1"));
    if (waitResponse() != 1)
    {
      return false;
    }

    // Set to get data manually
    sendAT(GF("+CIPRXGET=1"));
    if (waitResponse() != 1)
    {
      return false;
    }

    // Start Task and Set APN, USER NAME, PASSWORD
    sendAT(GF("+CSTT=\""), apn, GF("\",\""), user, GF("\",\""), pwd, GF("\""));
    if (waitResponse(60000L) != 1)
    {
      return false;
    }

    // Bring Up Wireless Connection with GPRS or CSD
    sendAT(GF("+CIICR"));
    if (waitResponse(60000L) != 1)
    {
      return false;
    }

    // Get Local IP Address, only assigned after connection
    sendAT(GF("+CIFSR;E0"));
    if (waitResponse(10000L) != 1)
    {
      return false;
    }

    // Configure Domain Name Server (DNS)
    sendAT(GF("+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\""));
    if (waitResponse() != 1)
    {
      return false;
    }

    return true;
  }

  bool gprsDisconnect()
  {
    // Shut the TCP/IP connection
    sendAT(GF("+CIPSHUT"));
    if (waitResponse(60000L) != 1)
      return false;

    sendAT(GF("+CGATT=0")); // Deactivate the bearer context
    if (waitResponse(60000L) != 1)
      return false;

    return true;
  }

  bool isGprsConnected()
  {
    sendAT(GF("+CGATT?"));
    if (waitResponse(GF(GSM_NL "+CGATT:")) != 1)
    {
      return false;
    }
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    if (res != 1)
      return false;

    sendAT(GF("+CIFSR;E0")); // Another option is to use AT+CGPADDR=1
    if (waitResponse() != 1)
      return false;

    return true;
  }
#ifndef TINY_GSM_NO_GPRS
  String getLocalIP()
  {
    sendAT(GF("+CIFSR;E0"));
    String res;
    if (waitResponse(10000L, res) != 1)
    {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, "");
    res.trim();
    return res;
  }

  IPAddress localIP()
  {
    return TinyGsmIpFromString(getLocalIP());
  }
#endif // TINY_GSM_NO_GPRS
  /*
   * Phone Call functions
   */

  bool setGsmBusy(bool busy = true)
  {
    sendAT(GF("+GSMBUSY="), busy ? 1 : 0);
    return waitResponse() == 1;
  }

  bool callAnswer()
  {
    sendAT(GF("A"));
    return waitResponse() == 1;
  }

  // Returns true on pick-up, false on error/busy
  bool callNumber(const String &number, const uint32_t &waitMs = 60000UL)
  {
    if (number == GF("last"))
    {
      sendAT(GF("DL"));
    }
    else
    {
      sendAT(GF("D"), number, ";");
    }
    int status = waitResponse(waitMs,
                              GFP(GSM_OK),
                              GF("BUSY" GSM_NL),
                              GF("NO ANSWER" GSM_NL),
                              GF("NO CARRIER" GSM_NL));
    switch (status)
    {
    case 1:
      return true;
    case 2:
    case 3:
      return false;
    default:
      return false;
    }
  }

  bool callHangup()
  {
    sendAT(GF("H"));
    return waitResponse(20000L) == 1;
  }

  bool receiveCallerIdentification(const bool receive)
  {
    sendAT(GF("+CLIP="), receive); // Calling Line Identification Presentation

    // Unsolicited result code format:
    // +CLIP: <number>,<type>[,<subaddr>,<satype>,<alphaId>,<CLIvalidity>]

    return waitResponse(15000L) == 1;
  }

  // 0-9,*,#,A,B,C,D
  bool dtmfSend(char cmd, int duration_ms = 100)
  {
    duration_ms = constrain(duration_ms, 100, 1000);

    sendAT(GF("+VTD="), duration_ms / 100); // VTD accepts in 1/10 of a second
    waitResponse();

    sendAT(GF("+VTS="), cmd);
    return waitResponse(10000L) == 1;
  }

  /*
   * Messaging functions
   */

  String sendUSSD(const String &code)
  {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    changeCharacterSet(GF("HEX"));
    sendAT(GF("+CUSD=1,\""), code, GF("\""));
    if (waitResponse() != 1)
    {
      return "";
    }
    if (waitResponse(10000L, GF(GSM_NL "+CUSD:")) != 1)
    {
      return "";
    }
    stream.readStringUntil('"');
    String hex = stream.readStringUntil('"');
    stream.readStringUntil(',');
    int dcs = stream.readStringUntil('\n').toInt();

    if (dcs == 15)
    {
      return TinyGsmDecodeHex8bit(hex);
    }
    else if (dcs == 72)
    {
      return TinyGsmDecodeHex16bit(hex);
    }
    else
    {
      return hex;
    }
  }

  bool initSMS()
  {
    DEBUG_PORT.println("IM HEREEE, SEE THIS, INIT SMS");
    sendAT(GF("+CMGF=1"));
    if (waitResponse() != 1)
    {
      return false;
    }
    sendAT(GF("+CPMS=\"SM\""));
    if (waitResponse() != 1)
    {
      return false;
    }
    sendAT(GF("+CSAS"));
    if (waitResponse() != 1)
    {
      return false;
    }
    sendAT(GF("+CSCS=\"GSM\""));
    if (waitResponse() != 1)
    {
      return false;
    }
    sendAT(GF("+CSDH=1")); // Show SMS Text Mode Parameters
    if (waitResponse() != 1)
    {
      return false;
    }


    return true;
  }

  bool sendSMS(const String &number, const String &text)
  {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    //Set GSM 7 bit default alphabet (3GPP TS 23.038)
    changeCharacterSet(GF("GSM"));
    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1)
    {
      return false;
    }
    stream.print(text);
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  bool sendSMS_UTF16(const String &number, const void *text, size_t len)
  {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    changeCharacterSet(GF("HEX"));
    sendAT(GF("+CSMP=17,167,0,8"));
    waitResponse();

    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1)
    {
      return false;
    }

    uint16_t *t = (uint16_t *)text;
    for (size_t i = 0; i < len; i++)
    {
      uint8_t c = t[i] >> 8;
      if (c < 0x10)
      {
        stream.print('0');
      }
      stream.print(c, HEX);
      c = t[i] & 0xFF;
      if (c < 0x10)
      {
        stream.print('0');
      }
      stream.print(c, HEX);
    }
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  Sms readSmsMessage(const uint8_t index, const bool changeStatusToRead = true)
  {
    sendAT(GF("+CMGR="), index, GF(","), static_cast<const uint8_t>(!changeStatusToRead)); // Read SMS Message
    if (waitResponse(5000L, GF(GSM_NL "+CMGR: \"")) != 1)
    {
      stream.readString();
      return {};
    }

    Sms sms;

    // AT reply:
    // <stat>,<oa>[,<alpha>],<scts>[,<tooa>,<fo>,<pid>,<dcs>,<sca>,<tosca>,<length>]<CR><LF><data>

    //<stat>
    const String res = stream.readStringUntil('"');
    if (res == GF("REC READ"))
    {
      sms.status = SmsStatus::REC_READ;
    }
    else if (res == GF("REC UNREAD"))
    {
      sms.status = SmsStatus::REC_UNREAD;
    }
    else if (res == GF("STO UNSENT"))
    {
      sms.status = SmsStatus::STO_UNSENT;
    }
    else if (res == GF("STO SENT"))
    {
      sms.status = SmsStatus::STO_SENT;
    }
    else if (res == GF("ALL"))
    {
      sms.status = SmsStatus::ALL;
    }
    else
    {
      stream.readString();
      return {};
    }

    // <oa>
    streamSkipUntil('"');
    sms.originatingAddress = stream.readStringUntil('"');

    // <alpha>
    streamSkipUntil('"');
    sms.phoneBookEntry = stream.readStringUntil('"');

    // <scts>
    streamSkipUntil('"');
    sms.serviceCentreTimeStamp = stream.readStringUntil('"');
    streamSkipUntil(',');

    streamSkipUntil(','); // <tooa>
    streamSkipUntil(','); // <fo>
    streamSkipUntil(','); // <pid>

    // <dcs>
    const uint8_t alphabet = (stream.readStringUntil(',').toInt() >> 2) & B11;
    switch (alphabet)
    {
    case B00:
      sms.alphabet = SmsAlphabet::GSM_7bit;
      break;
    case B01:
      sms.alphabet = SmsAlphabet::Data_8bit;
      break;
    case B10:
      sms.alphabet = SmsAlphabet::UCS2;
      break;
    case B11:
    default:
      sms.alphabet = SmsAlphabet::Reserved;
      break;
    }

    streamSkipUntil(','); // <sca>
    streamSkipUntil(','); // <tosca>

    // <length>, CR, LF
    const long length = stream.readStringUntil('\n').toInt();

    // <data>
    String data = stream.readString();
    data.remove(static_cast<const unsigned int>(length));
    switch (sms.alphabet)
    {
    case SmsAlphabet::GSM_7bit:
      sms.message = data;
      break;
    case SmsAlphabet::Data_8bit:
      sms.message = TinyGsmDecodeHex8bit(data);
      break;
    case SmsAlphabet::UCS2:
      sms.message = TinyGsmDecodeHex16bit(data);
      break;
    case SmsAlphabet::Reserved:
      return {};
    }

    return sms;
  }

  int checkUnreadMessage(Sms *sms_array, int limit = 0, bool changeStatusToRead = true)
  {
    sendAT(GF("+CMGL=\"REC UNREAD\","), static_cast<const uint8_t>(!changeStatusToRead));

    int ind = 0;
    bool run_loop = true;

    while (run_loop)
    {

      if (waitResponse(5000L, GFP(GSM_OK), GFP(GSM_ERROR), GF(GSM_NL "+CMGL: ")) != 3)
      {
        stream.readString();
        break;
      }

      Sms sms;

      // AT reply:
      // <stat>,<oa>[,<alpha>],<scts>[,<tooa>,<fo>,<pid>,<dcs>,<sca>,<tosca>,<length>]<CR><LF><data>

      //<stat>
      const String sms_ind = stream.readStringUntil(',');
      if (!sms_ind)
      {
        break;
      }
      sms.position = sms_ind.toInt();
      const String res = stream.readStringUntil(',');

      if (res == GF("\"REC UNREAD\""))
      {
        sms.status = SmsStatus::REC_UNREAD;
      }
      else
      {
        stream.readString();
        run_loop = false;
        if (!res)
        {
          run_loop = false;
        }
        continue;
      }

      // <oa>
      streamSkipUntil('"');
      sms.originatingAddress = stream.readStringUntil('"');

      // <alpha>
      streamSkipUntil('"');
      sms.phoneBookEntry = stream.readStringUntil('"');

      // <scts>
      streamSkipUntil('"');
      sms.serviceCentreTimeStamp = stream.readStringUntil('"');
      streamSkipUntil(',');

      streamSkipUntil(','); // <tooa>
      streamSkipUntil(','); // <fo>
      streamSkipUntil(','); // <pid>

      // <dcs>
      const uint8_t alphabet = (stream.readStringUntil(',').toInt() >> 2) & B11;
      switch (alphabet)
      {
      case B00:
      {
        sms.alphabet = SmsAlphabet::GSM_7bit;
        break;
      }
      case B01:
      {
        sms.alphabet = SmsAlphabet::Data_8bit;
        break;
      }
      case B10:
      {
        sms.alphabet = SmsAlphabet::UCS2;
        break;
      }
      case B11:
      default:
      {
        sms.alphabet = SmsAlphabet::Reserved;
        break;
      }
      }

      streamSkipUntil(','); // <sca>
      streamSkipUntil(','); // <tosca>

      // <length>, CR, LF
      const long length = stream.readStringUntil('\n').toInt();

      // <data>
      String data = stream.readStringUntil('\n');
      data.remove(static_cast<const unsigned int>(length));
      // switch (sms.alphabet)
      // {
      // case SmsAlphabet::GSM_7bit:
      //   sms.message = data;
      //   break;
      // case SmsAlphabet::Data_8bit:
      //   sms.message = TinyGsmDecodeHex8bit(data);
      //   break;
      // case SmsAlphabet::UCS2:
      //   sms.message = TinyGsmDecodeHex16bit(data);
      //   break;
      // case SmsAlphabet::Reserved:
      //   break;
      // }
      sms.message = data;
      DEBUG_PORT.print("SMS message is ");
      DEBUG_PORT.println(sms.message);
      sms_array[ind] = sms;
      ind++;
      if (limit && ind >= limit)
      {
        run_loop = false;
      }
    }

    return ind;
  }
  void setNewSMSCallback(NEW_SMS_CALLBACK_SIGNATURE){
    sms_callback = callback;
  }


  MessageStorage getPreferredMessageStorage()
  {
    sendAT(GF("+CPMS?")); // Preferred SMS Message Storage
    if (waitResponse(GF(GSM_NL "+CPMS:")) != 1)
    {
      stream.readString();
      return {};
    }

    // AT reply:
    // +CPMS: <mem1>,<used1>,<total1>,<mem2>,<used2>,<total2>,<mem3>,<used3>,<total3>

    MessageStorage messageStorage;
    for (uint8_t i = 0; i < 3; ++i)
    {
      // type
      streamSkipUntil('"');
      const String mem = stream.readStringUntil('"');
      if (mem == GF("SM"))
      {
        messageStorage.type[i] = MessageStorageType::SIM;
      }
      else if (mem == GF("ME"))
      {
        messageStorage.type[i] = MessageStorageType::Phone;
      }
      else if (mem == GF("SM_P"))
      {
        messageStorage.type[i] = MessageStorageType::SIMPreferred;
      }
      else if (mem == GF("ME_P"))
      {
        messageStorage.type[i] = MessageStorageType::PhonePreferred;
      }
      else if (mem == GF("MT"))
      {
        messageStorage.type[i] = MessageStorageType::Either_SIMPreferred;
      }
      else
      {
        stream.readString();
        return {};
      }

      // used
      streamSkipUntil(',');
      messageStorage.used[i] = static_cast<uint8_t>(stream.readStringUntil(',').toInt());

      // total
      if (i < 2)
      {
        messageStorage.total[i] = static_cast<uint8_t>(stream.readStringUntil(',').toInt());
      }
      else
      {
        messageStorage.total[i] = static_cast<uint8_t>(stream.readString().toInt());
      }
    }

    return messageStorage;
  }

  bool setPreferredMessageStorage(const MessageStorageType type[3])
  {
    const auto convertMstToString = [](const MessageStorageType &type) -> auto
    {
      switch (type)
      {
      case MessageStorageType::SIM:
        return GF("\"SM\"");
      case MessageStorageType::Phone:
        return GF("\"ME\"");
      case MessageStorageType::SIMPreferred:
        return GF("\"SM_P\"");
      case MessageStorageType::PhonePreferred:
        return GF("\"ME_P\"");
      case MessageStorageType::Either_SIMPreferred:
        return GF("\"MT\"");
      }

      return GF("");
    };

    sendAT(GF("+CPMS="),
           convertMstToString(type[0]), GF(","),
           convertMstToString(type[1]), GF(","),
           convertMstToString(type[2]));

    return waitResponse() == 1;
  }

  bool deleteSmsMessage(const uint8_t index)
  {
    sendAT(GF("+CMGD="), index, GF(","), 0); // Delete SMS Message from <mem1> location
    return waitResponse(5000L) == 1;
  }

  bool deleteAllSmsMessages(const DeleteAllSmsMethod method)
  {
    // Select SMS Message Format: PDU mode. Spares us space now
    sendAT(GF("+CMGF=0"));
    if (waitResponse() != 1)
    {
      return false;
    }

    sendAT(GF("+CMGDA="), static_cast<const uint8_t>(method)); // Delete All SMS
    const bool ok = waitResponse(25000L) == 1;

    sendAT(GF("+CMGF=1"));
    waitResponse();

    return ok;
  }

  bool receiveNewMessageIndication(const bool enabled = true, const bool cbmIndication = false, const bool statusReport = false)
  {
    sendAT(GF("+CNMI=2,"),          // New SMS Message Indications
           enabled, GF(","),        // format: +CMTI: <mem>,<index>
           cbmIndication, GF(","),  // format: +CBM: <sn>,<mid>,<dcs>,<page>,<pages><CR><LF><data>
           statusReport, GF(",0")); // format: +CDS: <fo>,<mr>[,<ra>][,<tora>],<scts>,<dt>,<st>

    return waitResponse() == 1;
  }

  /*
   * Phonebook functions
   */

  PhonebookStorage getPhonebookStorage()
  {
    sendAT(GF("+CPBS?")); // Phonebook Memory Storage
    if (waitResponse(GF(GSM_NL "+CPBS: \"")) != 1)
    {
      stream.readString();
      return {};
    }

    // AT reply:
    // +CPBS: <storage>,<used>,<total>

    PhonebookStorage phonebookStorage;

    const String mem = stream.readStringUntil('"');
    if (mem == GF("SM"))
    {
      phonebookStorage.type = PhonebookStorageType::SIM;
    }
    else if (mem == GF("ME"))
    {
      phonebookStorage.type = PhonebookStorageType::Phone;
    }
    else
    {
      stream.readString();
      return {};
    }

    // used, total
    streamSkipUntil(',');
    phonebookStorage.used = static_cast<uint8_t>(stream.readStringUntil(',').toInt());
    phonebookStorage.total = static_cast<uint8_t>(stream.readString().toInt());

    return phonebookStorage;
  }

  bool setPhonebookStorage(const PhonebookStorageType type)
  {
    if (type == PhonebookStorageType::Invalid)
    {
      return false;
    }

    const auto storage = type == PhonebookStorageType::SIM ? GF("\"SM\"") : GF("\"ME\"");
    sendAT(GF("+CPBS="), storage); // Phonebook Memory Storage

    return waitResponse() == 1;
  }

  bool addPhonebookEntry(const String &number, const String &text)
  {
    // Always use international phone number style (+12345678910).
    // Never use double quotes or backslashes in `text`, not even in escaped form.
    // Use characters found in the GSM alphabet.

    // Typical maximum length of `number`: 38
    // Typical maximum length of `text`:   14

    changeCharacterSet(GF("GSM"));

    // AT format:
    // AT+CPBW=<index>[,<number>,[<type>,[<text>]]]
    sendAT(GF("+CPBW=,\""), number, GF("\",145,\""), text, '"'); // Write Phonebook Entry

    return waitResponse(3000L) == 1;
  }

  bool deletePhonebookEntry(const uint8_t index)
  {
    // AT+CPBW=<index>
    sendAT(GF("+CPBW="), index); // Write Phonebook Entry

    // Returns OK even if an empty index is deleted in the valid range
    return waitResponse(3000L) == 1;
  }

  PhonebookEntry readPhonebookEntry(const uint8_t index)
  {
    changeCharacterSet(GF("GSM"));
    sendAT(GF("+CPBR="), index); // Read Current Phonebook Entries

    // AT response:
    // +CPBR:<index1>,<number>,<type>,<text>
    if (waitResponse(3000L, GF(GSM_NL "+CPBR: ")) != 1)
    {
      stream.readString();
      return {};
    }

    PhonebookEntry phonebookEntry;
    streamSkipUntil('"');
    phonebookEntry.number = stream.readStringUntil('"');
    streamSkipUntil('"');
    phonebookEntry.text = stream.readStringUntil('"');

    waitResponse();

    return phonebookEntry;
  }

  PhonebookMatches findPhonebookEntries(const String &needle)
  {
    // Search among the `text` entries only.
    // Only the first TINY_GSM_PHONEBOOK_RESULTS indices are returned.
    // Make your query more specific if you have more results than that.
    // Use characters found in the GSM alphabet.

    changeCharacterSet(GF("GSM"));
    sendAT(GF("+CPBF=\""), needle, '"'); // Find Phonebook Entries

    // AT response:
    // [+CPBF:<index1>,<number>,<type>,<text>]
    // [[...]<CR><LF>+CBPF:<index2>,<number>,<type>,<text>]
    if (waitResponse(30000L, GF(GSM_NL "+CPBF: ")) != 1)
    {
      stream.readString();
      return {};
    }

    PhonebookMatches matches;
    matches.no_of_matches = 0;
    for (uint8_t i = 0; i < TINY_GSM_PHONEBOOK_RESULTS; ++i)
    {
      matches.index[i] = static_cast<uint8_t>(stream.readStringUntil(',').toInt());
      matches.no_of_matches++;
      if (waitResponse(GF(GSM_NL "+CPBF: ")) != 1)
      {
        break;
      }
    }

    waitResponse();

    return matches;
  }

  /*
   * Location functions
   */

  String getGsmLocation()
  {
    sendAT(GF("+CIPGSMLOC=1,1"));
    if (waitResponse(10000L, GF(GSM_NL "+CIPGSMLOC:")) != 1)
    {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  /*
   * Time functions
   */
  String getGSMDateTime(TinyGSMDateTimeFormat format)
  {
    sendAT(GF("+CCLK?"));
    if (waitResponse(2000L, GF(GSM_NL "+CCLK: \"")) != 1)
    {
      return "";
    }

    String res;

    switch (format)
    {
    case DATE_FULL:
      res = stream.readStringUntil('"');
      break;
    case DATE_TIME:
      streamSkipUntil(',');
      res = stream.readStringUntil('"');
      break;
    case DATE_DATE:
      res = stream.readStringUntil(',');
      break;
    }
    return res;
  }

  /*
   * Battery functions
   */
  // Use: float vBatt = modem.getBattVoltage() / 1000.0;
  uint16_t getBattVoltage()
  {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1)
    {
      return 0;
    }
    streamSkipUntil(','); // Skip
    streamSkipUntil(','); // Skip

    uint16_t res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

  int getBattPercent()
  {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1)
    {
      return false;
    }
    stream.readStringUntil(',');
    int res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

protected:
#ifndef TINY_GSM_NO_GPRS
  bool modemConnect(const char *host, uint16_t port, uint8_t mux, bool ssl = false)
  {
    int rsp;
#if !defined(TINY_GSM_MODEM_SIM900)
    sendAT(GF("+CIPSSL="), ssl);
    rsp = waitResponse();
    if (ssl && rsp != 1)
    {
      return false;
    }
#endif
    sendAT(GF("+CIPSTART="), mux, ',', GF("\"TCP"), GF("\",\""), host, GF("\","), port);
    rsp = waitResponse(75000L,
                       GF("CONNECT OK" GSM_NL),
                       GF("CONNECT FAIL" GSM_NL),
                       GF("ALREADY CONNECT" GSM_NL),
                       GF("ERROR" GSM_NL),
                       GF("CLOSE OK" GSM_NL) // Happens when HTTPS handshake fails
    );
    return (1 == rsp);
  }

  int modemSend(const void *buff, size_t len, uint8_t mux)
  {
    sendAT(GF("+CIPSEND="), mux, ',', len);
    if (waitResponse(GF(">")) != 1)
    {
      return 0;
    }
    stream.write((uint8_t *)buff, len);
    stream.flush();
    if (waitResponse(GF(GSM_NL "DATA ACCEPT:")) != 1)
    {
      return 0;
    }
    streamSkipUntil(','); // Skip mux
    return stream.readStringUntil('\n').toInt();
  }

  size_t modemRead(size_t size, uint8_t mux)
  {
#ifdef TINY_GSM_USE_HEX
    sendAT(GF("+CIPRXGET=3,"), mux, ',', size);
    if (waitResponse(GF("+CIPRXGET:")) != 1)
    {
      return 0;
    }
#else
    sendAT(GF("+CIPRXGET=2,"), mux, ',', size);
    if (waitResponse(GF("+CIPRXGET:")) != 1)
    {
      return 0;
    }
#endif
    streamSkipUntil(','); // Skip mode 2/3
    streamSkipUntil(','); // Skip mux
    size_t len = stream.readStringUntil(',').toInt();
    sockets[mux]->sock_available = stream.readStringUntil('\n').toInt();

    for (size_t i = 0; i < len; i++)
    {
#ifdef TINY_GSM_USE_HEX
      while (stream.available() < 2)
      {
        TINY_GSM_YIELD();
      }
      char buf[4] = {
          0,
      };
      buf[0] = stream.read();
      buf[1] = stream.read();
      char c = strtol(buf, NULL, 16);
#else
      while (!stream.available())
      {
        TINY_GSM_YIELD();
      }
      char c = stream.read();
#endif
      sockets[mux]->rx.put(c);
    }
    waitResponse();
    return len;
  }

  size_t modemGetAvailable(uint8_t mux)
  {
    sendAT(GF("+CIPRXGET=4,"), mux);
    size_t result = 0;
    if (waitResponse(GF("+CIPRXGET:")) == 1)
    {
      streamSkipUntil(','); // Skip mode 4
      streamSkipUntil(','); // Skip mux
      result = stream.readStringUntil('\n').toInt();
      waitResponse();
    }
    if (!result)
    {
      sockets[mux]->sock_connected = modemGetConnected(mux);
    }
    return result;
  }

  bool modemGetConnected(uint8_t mux)
  {
    sendAT(GF("+CIPSTATUS="), mux);
    int res = waitResponse(GF(",\"CONNECTED\""), GF(",\"CLOSED\""), GF(",\"CLOSING\""), GF(",\"INITIAL\""));
    waitResponse();
    return 1 == res;
  }
#endif // TINY_GSM_NO_GPRS
public:
  /* Utilities */

  template <typename T>
  void streamWrite(T last)
  {
    stream.print(last);
  }

  template <typename T, typename... Args>
  void streamWrite(T head, Args... tail)
  {
    stream.print(head);
    streamWrite(tail...);
  }

  bool streamSkipUntil(const char c, const unsigned long timeout = 3000L)
  {
    unsigned long startMillis = millis();
    while (millis() - startMillis < timeout)
    {
      while (millis() - startMillis < timeout && !stream.available())
      {
        TINY_GSM_YIELD();
      }
      if (stream.read() == c)
        return true;
    }
    return false;
  }

  template <typename... Args>
  void sendAT(Args... cmd)
  {
    streamWrite("AT", cmd..., GSM_NL);
    stream.flush();
    TINY_GSM_YIELD();
    //DBG("### AT:", cmd...);
  }

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout, String &data,
                       GsmConstStr r1 = GFP(GSM_OK), GsmConstStr r2 = GFP(GSM_ERROR),
                       GsmConstStr r3 = NULL, GsmConstStr r4 = NULL, GsmConstStr r5 = NULL)
  {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do
    {
      TINY_GSM_YIELD();
      while (stream.available() > 0)
      {
        int a = stream.read();
        if (a <= 0)
          continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        // Handling Automatic Updates first 
        if(data.endsWith(GF(GSM_NL "+CMTI:"))){
          String mem = stream.readStringUntil(',');
          unsigned int index = stream.readStringUntil('\n').toInt();

          DBG("New Message: " ,mem, index);
          if(sms_callback!= NULL){
            sms_callback(index);
          }
          data = "";
        }

        else if (r1 && data.endsWith(r1))
        {
          index = 1;
          goto finish;
        }
        else if (r2 && data.endsWith(r2))
        {
          index = 2;
          goto finish;
        }
        else if (r3 && data.endsWith(r3))
        {
          index = 3;
          goto finish;
        }
        else if (r4 && data.endsWith(r4))
        {
          index = 4;
          goto finish;
        }
        else if (r5 && data.endsWith(r5))
        {
          index = 5;
          goto finish;
        }
#ifndef TINY_GSM_NO_GPRS
        else if (data.endsWith(GF(GSM_NL "+CIPRXGET:")))
        {
          String mode = stream.readStringUntil(',');
          if (mode.toInt() == 1)
          {
            int mux = stream.readStringUntil('\n').toInt();
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux])
            {
              sockets[mux]->got_data = true;
            }
            data = "";
          }
          else
          {
            data += mode;
          }
        }
        else if (data.endsWith(GF("CLOSED" GSM_NL)))
        {
          int nl = data.lastIndexOf(GSM_NL, data.length() - 8);
          int coma = data.indexOf(',', nl + 2);
          int mux = data.substring(nl + 2, coma).toInt();
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux])
          {
            sockets[mux]->sock_connected = false;
          }
          data = "";
          DBG("### Closed: ", mux);
        }
#endif // TINY_GSM_NO_GPRS
      }
    } while (millis() - startMillis < timeout);
  finish:
    if (!index)
    {
      data.trim();
      if (data.length())
      {
        DBG("### Unhandled:", data);
      }
      data = "";
    }
    return index;
  }

  uint8_t waitResponse(uint32_t timeout,
                       GsmConstStr r1 = GFP(GSM_OK), GsmConstStr r2 = GFP(GSM_ERROR),
                       GsmConstStr r3 = NULL, GsmConstStr r4 = NULL, GsmConstStr r5 = NULL)
  {
    String data;
    return waitResponse(timeout, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1 = GFP(GSM_OK), GsmConstStr r2 = GFP(GSM_ERROR),
                       GsmConstStr r3 = NULL, GsmConstStr r4 = NULL, GsmConstStr r5 = NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

public:
  Stream &stream;

protected:
#ifndef TINY_GSM_NO_GPRS
  GsmClient *sockets[TINY_GSM_MUX_COUNT];
#endif // TINY_GSM_NO_GPRS

  bool changeCharacterSet(const String &alphabet)
  {
    sendAT(GF("+CSCS=\""), alphabet, '"');
    return waitResponse() == 1;
  }

  private:
    std::function<void(unsigned int)> sms_callback;
};


#endif
