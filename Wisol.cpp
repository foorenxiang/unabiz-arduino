//  Library for sending and receiving SIGFOX messages with Arduino shield based on Wisol WSSFM10R.
#ifdef ARDUINO
  #if (ARDUINO >= 100)
    #include <Arduino.h>
  #else  //  ARDUINO >= 100
    #include <WProgram.h>
  #endif  //  ARDUINO  >= 100
#endif  //  ARDUINO

#include "SIGFOX.h"

//  Use a macro for logging because Flash strings not supported with String class in Bean+
#define log1(x) { echoPort->println(x); }
#define log2(x, y) { echoPort->print(x); echoPort->println(y); }
#define log3(x, y, z) { echoPort->print(x); echoPort->print(y); echoPort->println(z); }
#define log4(x, y, z, a) { echoPort->print(x); echoPort->print(y); echoPort->print(z); echoPort->println(a); }

#define MODEM_BITS_PER_SECOND 9600  //  Connect to modem at this bps.
#define END_OF_RESPONSE '\r'  //  Character '\r' marks the end of response.
#define CMD_SEND_MESSAGE "AT$SF="  //  Prefix to send a message to SIGFOX cloud.
#define CMD_GET_ID "AT$I=10"  //  Get SIGFOX device ID.
#define CMD_GET_PAC "AT$I=11"  //  Get SIGFOX device PAC, used for registering the device.
#define CMD_GET_TEMPERATURE "AT$T?"  //  Get the module temperature.
#define CMD_GET_VOLTAGE "AT$V?"  //  Get the module voltage.
#define CMD_SLEEP "AT$P=1"  //  TODO: Switch to sleep mode : consumption is < 1.5uA
#define CMD_WAKEUP "AT$P=0"  //  TODO: Switch back to normal mode : consumption is 0.5 mA
#define CMD_END "\r"

static NullPort nullPort;
static uint8_t markers = 0;

//  Remember where in response the '>' markers were seen.
const uint8_t markerPosMax = 5;
static uint8_t markerPos[markerPosMax];

bool Wisol::sendBuffer(const String &buffer, const int timeout,
                       uint8_t expectedMarkerCount, String &response,
                       uint8_t &actualMarkerCount) {
  //  buffer contains a string of ASCII chars to be sent to the modem.
  //  We send the buffer to the modem.  Return true if successful.
  //  expectedMarkerCount is the number of end-of-command markers '\r' we
  //  expect to see.  actualMarkerCount contains the actual number seen.
  log2(F(" - Wisol.sendBuffer: "), buffer);
  response = "";
  if (useEmulator) return true;

  actualMarkerCount = 0;
  //  Start serial interface.
  serialPort->begin(MODEM_BITS_PER_SECOND);
#ifdef BEAN_BEAN_BEAN_H
  Bean.sleep(200);
#else  // BEAN_BEAN_BEAN_H
  delay(200);
#endif // BEAN_BEAN_BEAN_H
  serialPort->flush();
  serialPort->listen();

  //  Send the buffer: need to write/read char by char because of echo.
  const char *rawBuffer = buffer.c_str();
  //  Send buffer and read response.  Loop until timeout or we see the end of response marker.
  unsigned long startTime = millis(); int i = 0;
  //  Previous code for verifying that data was sent correctly.
  //static String echoSend = "", echoReceive = "";
  for (;;) {
    //  If there is data to send, send it.
    if (i < buffer.length()) {
      //  Send the char.
      uint8_t txChar = rawBuffer[i];
      //echoSend.concat(toHex((char) txChar) + ' ');
      serialPort->write(txChar);
#ifdef BEAN_BEAN_BEAN_H
      Bean.sleep(10);
#else  // BEAN_BEAN_BEAN_H
      delay(10);  //  Need to wait a while because SoftwareSerial has no FIFO and may overflow.
#endif // BEAN_BEAN_BEAN_H
      i = i + 1;
      startTime = millis();  //  Start the timer only when all data has been sent.
    }
    //  If timeout, quit.
    const unsigned long currentTime = millis();
    if (currentTime - startTime > timeout) break;

    //  If data is available to receive, receive it.
    if (serialPort->available() > 0) {
      int rxChar = serialPort->read();
      //  echoReceive.concat(toHex((char) rxChar) + ' ');
      if (rxChar == -1) continue;
      if (rxChar == END_OF_RESPONSE) {
        if (actualMarkerCount < markerPosMax)
          markerPos[actualMarkerCount] = response.length();  //  Remember the marker pos.
        actualMarkerCount++;  //  Count the number of end markers.
        if (actualMarkerCount >= expectedMarkerCount) break;  //  Seen all markers already.
      } else {
        // log2(F("rxChar "), rxChar);
        response.concat(String((char) rxChar));
      }
    }
    //  TODO: Check for downlink response.
  }
  serialPort->end();
  //  Log the actual bytes sent and received.
  //log2(F(">> "), echoSend);
  //  if (echoReceive.length() > 0) { log2(F("<< "), echoReceive); }
  logBuffer(F(">> "), rawBuffer, 0, 0);
  logBuffer(F("<< "), response.c_str(), markerPos, actualMarkerCount);

  //  If we did not see the terminating '\r', something is wrong.
  if (actualMarkerCount < expectedMarkerCount) {
    if (response.length() == 0) {
      log1(F(" - Wisol.sendBuffer: Error: No response"));  //  Response timeout.
    } else {
      log2(F(" - Wisol.sendBuffer: Error: Unknown response: "), response);
    }
    return false;
  }
  log2(F(" - Wisol.sendBuffer: response: "), response);
  //  TODO: Parse the downlink response.
  return true;
}

bool Wisol::sendMessage(const String &payload) {
  //  Payload contains a string of hex digits, up to 24 digits / 12 bytes.
  //  We prefix with AT$SF= and send to SIGFOX.  Return true if successful.
  log2(F(" - Wisol.sendMessage: "), device + ',' + payload);
  if (!isReady()) return false;  //  Prevent user from sending too many messages.
  //  Exit command mode and prepare to send message.
  if (!exitCommandMode()) return false;

  //  Decode and send the data.
  String message = String(CMD_SEND_MESSAGE) + payload + CMD_END, data;
  if (sendBuffer(message, WISOL_COMMAND_TIMEOUT, 1, data, markers)) {  //  One '\r' marker expected ("OK\r").
    log1(data);
    lastSend = millis();
    return true;
  }
  return false;
}

bool Wisol::enterCommandMode() {
  //  Enter Command Mode for sending module commands, not data.
  //  Not used for Wisol.
  return true;
}

bool Wisol::exitCommandMode() {
  //  Exit Command Mode so we can send data.
  //  Not used for Wisol.
  return true;
}

bool Wisol::getID(String &id, String &pac) {
  //  Get the SIGFOX ID and PAC for the module.
  if (useEmulator) { id = device; return true; }
  String data = "";
  if (!sendCommand(String(CMD_GET_ID) + CMD_END, 1, data, markers)) return false;
  id = data;
  device = id;
  if (!sendCommand(String(CMD_GET_PAC) + CMD_END, 1, data, markers)) return false;
  pac = data;
  log2(F(" - Wisol.getID: returned id="), id + ", pac=" + pac);
  return true;
}

bool Wisol::getTemperature(float &temperature) {
  //  Returns the temperature of the SIGFOX module.
  if (useEmulator) { temperature = 36; return true; }
  String data = "";
  log1("getTemperature"); ////
  if (!sendCommand(String(CMD_GET_TEMPERATURE) + CMD_END, 1, data, markers)) return false;
  temperature = data.toInt() / 10.0;
  log2(F(" - Wisol.getTemperature: returned "), temperature);
  return true;
}

bool Wisol::getVoltage(float &voltage) {
  //  Returns the power supply voltage.
  if (useEmulator) { voltage = 12.3; return true; }
  String data = "";
  if (!sendCommand(String(CMD_GET_VOLTAGE) + CMD_END, 1, data, markers)) return false;
  voltage = data.toFloat() / 1000.0;
  log2(F(" - Wisol.getVoltage: returned "), voltage);
  return true;
}

bool Wisol::getHardware(String &hardware) {
  //  TODO
  log1(F(" - Wisol.getHardware: ERROR - Not implemented"));
  hardware = "TODO";
  return true;
}

bool Wisol::getFirmware(String &firmware) {
  //  TODO
  log1(F(" - Wisol.getFirmware: ERROR - Not implemented"));
  firmware = "TODO";
  return true;
}

bool Wisol::getParameter(uint8_t address, String &value) {
  //  Read the parameter at the address.
  log2(F(" - Wisol.getParameter: address=0x"), toHex((char) address));
  log1(F(" - Wisol.getParameter: ERROR - Not implemented"));
  log4(F(" - Wisol.getParameter: address=0x"), toHex((char) address), F(" returned "), value);
  return true;
}

bool Wisol::getPower(int &power) {
  //  Get the power step-down.
  log1(F(" - Wisol.getPower: ERROR - Not implemented"));
  power = 0;
  return true;
}

bool Wisol::setPower(int power) {
  //  TODO: Power value: 0...14
  log1(F(" - Wisol.setPower: ERROR - Not implemented"));
  return true;
}

bool Wisol::getEmulator(int &result) {
  //  Get the current emulation mode of the module.
  //  0 = Emulator disabled (sending to SIGFOX network with unique ID & key)
  //  1 = Emulator enabled (sending to emulator with public ID & key)
  //  We assume not using emulator.
  result = 0;
  return true;
}

bool Wisol::disableEmulator(String &result) {
  //  Set the module key to the unique SIGFOX key.  This is needed for sending
  //  to a real SIGFOX base station.
  //  We assume not using emulator.
  return true;
}

bool Wisol::enableEmulator(String &result) {
  //  Set the module key to the public key.  This is needed for sending
  //  to an emulator.
  log1(F(" - Wisol.enableEmulator: ERROR - Not implemented"));
  return true;
}

bool Wisol::getFrequency(String &result) {
  //  Get the frequency used for the SIGFOX module
  //  0: Europe (RCZ1)
  //  1: US (RCZ2)
  //  3: SG, TW, AU, NZ (RCZ4)
  //  Not used for Wisol.
  //  log1(F(" - Wisol.getFrequency: ERROR - Not implemented"));
  result = "3";
  return true;
}

bool Wisol::setFrequency(int zone, String &result) {
  //  Get the frequency used for the SIGFOX module
  //  0: Europe (RCZ1)
  //  1: US (RCZ2)
  //  3: AU/NZ (RCZ4)
  //  Not used for Wisol.
  //  log1(F(" - Wisol.setFrequency: ERROR - Not implemented"));
  return true;
}

bool Wisol::setFrequencySG(String &result) {
  //  Set the frequency for the SIGFOX module to Singapore frequency (RCZ4).
  log1(F(" - Wisol.setFrequencySG"));
  return setFrequency(4, result); }

bool Wisol::setFrequencyTW(String &result) {
  //  Set the frequency for the SIGFOX module to Taiwan frequency (RCZ4).
  log1(F(" - Wisol.setFrequencyTW"));
  return setFrequency(4, result); }

bool Wisol::setFrequencyETSI(String &result) {
  //  Set the frequency for the SIGFOX module to ETSI frequency for Europe (RCZ1).
  log1(F(" - Wisol.setFrequencyETSI"));
  return setFrequency(1, result); }

bool Wisol::setFrequencyUS(String &result) {
  //  Set the frequency for the SIGFOX module to US frequency (RCZ2).
  log1(F(" - Wisol.setFrequencyUS"));
  return setFrequency(2, result); }

bool Wisol::writeSettings(String &result) {
  //  TODO: Write settings to module's flash memory.
  log1(F(" - Wisol.writeSettings: ERROR - Not implemented"));
  return true;
}

/* TODO: Run some sanity checks to ensure that Wisol module is configured OK.
  //  Get network mode for transmission.  Should return network mode = 0 for uplink only, no downlink.
  Serial.println(F("\nGetting network mode (expecting 0)..."));
  transceiver.getParameter(0x3b, result);

  //  Get baud rate.  Should return baud rate = 5 for 19200 bps.
  Serial.println(F("\nGetting baud rate (expecting 5)..."));
  transceiver.getParameter(0x30, result);
*/

Wisol::Wisol(Country country0, bool useEmulator0, const String device0, bool echo):
    Wisol(country0, useEmulator0, device0, echo, WISOL_RX, WISOL_TX) {}  //  Forward to constructor below.

Wisol::Wisol(Country country0, bool useEmulator0, const String device0, bool echo,
                         uint8_t rx, uint8_t tx) {
  //  Init the module with the specified transmit and receive pins.
  //  Default to no echo.
  country = country0;
  useEmulator = useEmulator0;
  device = device0;
  //  Bean+ firmware 0.6.1 can't receive serial data properly. We provide
  //  an alternative class BeanSoftwareSerial to work around this.
  //  For Bean, SoftwareSerial is a #define alias for BeanSoftwareSerial.
  serialPort = new SoftwareSerial(rx, tx);
  if (echo) echoPort = &Serial;
  else echoPort = &nullPort;
  lastEchoPort = &Serial;
}

bool Wisol::begin() {
  //  Wait for the module to power up, configure transmission frequency.
  //  Return true if module is ready to send.
  lastSend = 0;
  for (int i = 0; i < 5; i++) {
    //  Retry 5 times.
#ifdef BEAN_BEAN_BEAN_H
    Bean.sleep(7000);  //  For Bean, delay longer to allow Bluetooth debug console to connect.
#else  // BEAN_BEAN_BEAN_H
    delay(2000);
#endif // BEAN_BEAN_BEAN_H
    String result;
    if (useEmulator) {
      //  Emulation mode.
      if (!enableEmulator(result)) continue;
    } else {
      //  Disable emulation mode.
      log1(F(" - Disabling emulation mode..."));
      if (!disableEmulator(result)) continue;

      //  Check whether emulator is used for transmission.
      log1(F(" - Checking emulation mode (expecting 0)...")); int emulator = 0;
      if (!getEmulator(emulator)) continue;
    }

    //  Read SIGFOX ID and PAC from module.
    log1(F(" - Getting SIGFOX ID..."));  String id, pac;
    if (!getID(id, pac)) continue;
    echoPort->print(F(" - SIGFOX ID = "));  Serial.println(id);
    echoPort->print(F(" - PAC = "));  Serial.println(pac);

    //  Set the frequency of SIGFOX module.
    log2(F(" - Setting frequency for country "), (int) country);
    if (country == COUNTRY_US) {  //  US runs on different frequency (RCZ2).
      if (!setFrequencyUS(result)) continue;
    } else if (country == COUNTRY_FR) {  //  France runs on different frequency (RCZ1).
      if (!setFrequencyETSI(result)) continue;
    } else { //  Rest of the world runs on RCZ4.
      if (!setFrequencySG(result)) continue;
    }
    log2(F(" - Set frequency result = "), result);

    //  Get and display the frequency used by the SIGFOX module.  Should return 3 for RCZ4 (SG/TW).
    log1(F(" - Getting frequency (expecting 3)..."));  String frequency;
    if (!getFrequency(frequency)) continue;
    log2(F(" - Frequency (expecting 3) = "), frequency);
    return true;  //  Init module succeeded.
  }
  return false;  //  Failed to init module.
}

bool Wisol::sendCommand(const String &cmd, uint8_t expectedMarkerCount,
                              String &result, uint8_t &actualMarkerCount) {
  //  We send the command string in cmd to SIGFOX.  Return true if successful.
  String data;
  //  Enter command mode.
  if (!enterCommandMode()) return false;
  if (!sendBuffer(cmd, WISOL_COMMAND_TIMEOUT, expectedMarkerCount,
                  data, actualMarkerCount)) return false;
  result = data;
  return true;
}

bool Wisol::sendString(const String &str) {
  //  For convenience, allow sending of a text string with automatic encoding into bytes.  Max 12 characters allowed.
  //  Convert each character into 2 bytes.
  log2(F(" - Wisol.sendString: "), str);
  String payload;
  for (unsigned i = 0; i < str.length(); i++) {
    char ch = str.charAt(i);
    payload.concat(toHex(ch));
  }
  //  Send the encoded payload.
  return sendMessage(payload);
}

bool Wisol::isReady()
{
  // Check the duty cycle and return true if we can send data.
  // IMPORTANT WARNING. PLEASE READ BEFORE MODIFYING THE CODE
  //
  // The Sigfox network operates on public frequencies. To comply with
  // radio regulation, it can send radio data a maximum of 1% of the time
  // to leave room to other devices using the same frequencies.
  //
  // Sending a message takes about 6 seconds (it's sent 3 times for
  // redundancy purposes), meaning the interval between messages should
  // be 10 minutes.
  //
  // Also make sure your send rate complies with the restrictions set
  // by the particular subscription contract you have with your Sigfox
  // network operator.
  //
  // FAILING TO COMPLY WITH THESE CONSTRAINTS MAY CAUSE YOUR MODEM
  // TO BE BLOCKED BY YOUR SIFGOX NETWORK OPERATOR.
  //
  // You've been warned!

  unsigned long currentTime = millis();
  if (lastSend == 0) return true;  //  First time sending.
  const unsigned long elapsedTime = currentTime - lastSend;
  //  For development, allow sending every 2 seconds.
  if (elapsedTime <= 2 * 1000) {
    log1(F("Must wait 2 seconds before sending the next message"));
    return false;
  }  //  Wait before sending.
  if (elapsedTime <= SEND_DELAY)
    log1(F("Warning: Should wait 10 mins before sending the next message"));
  return true;
}

static String data;

bool Wisol::reboot(String &result) {
  //  TODO: Reboot the module.
  log1(F(" - Wisol.reboot: ERROR - Not implemented"));
  return true;
}

void Wisol::echoOn() {
  //  Echo commands and responses to the echo port.
  echoPort = lastEchoPort;
  log1(F(" - Wisol.echoOn"));
}

void Wisol::echoOff() {
  //  Stop echoing commands and responses to the echo port.
  lastEchoPort = echoPort; echoPort = &nullPort;
}

void Wisol::setEchoPort(Print *port) {
  //  Set the port for sending echo output.
  lastEchoPort = echoPort;
  echoPort = port;
}

void Wisol::echo(const String &msg) {
  //  Echo debug message to the echo port.
  log2(F(" - "), msg);
}

bool Wisol::receive(String &data) {
  //  TODO
  log1(F(" - Wisol.receive: ERROR - Not implemented"));
  return true;
}

String Wisol::toHex(int i) {
  //  Convert the integer to a string of 4 hex digits.
  byte *b = (byte *) &i;
  String bytes;
  for (int j=0; j<2; j++) {
    if (b[j] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[j], 16));
  }
  return bytes;
}

String Wisol::toHex(unsigned int ui) {
  //  Convert the integer to a string of 4 hex digits.
  byte *b = (byte *) &ui;
  String bytes;
  for (int i=0; i<2; i++) {
    if (b[i] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Wisol::toHex(long l) {
  //  Convert the long to a string of 8 hex digits.
  byte *b = (byte *) &l;
  String bytes;
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Wisol::toHex(unsigned long ul) {
  //  Convert the long to a string of 8 hex digits.
  byte * b = (byte *) &ul;
  String bytes;
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Wisol::toHex(float f) {
  //  Convert the float to a string of 8 hex digits.
  byte *b = (byte *) &f;
  String bytes;
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Wisol::toHex(double d) {
  //  Convert the double to a string of 8 hex digits.
  byte *b = (byte *) &d;
  String bytes;
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Wisol::toHex(char c) {
  //  Convert the char to a string of 2 hex digits.
  byte *b = (byte *) &c;
  String bytes;
  if (b[0] <= 0xF) bytes.concat('0');
  bytes.concat(String(b[0], 16));
  return bytes;
}

String Wisol::toHex(char *c, int length) {
  //  Convert the string to a string of hex digits.
  byte *b = (byte *) c;
  String bytes;
  for (int i=0; i<length; i++) {
    if (b[i] <= 0xF) bytes.concat('0');
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

uint8_t Wisol::hexDigitToDecimal(char ch) {
  //  Convert 0..9, a..f, A..F to decimal.
  if (ch >= '0' && ch <= '9') return (uint8_t) ch - '0';
  if (ch >= 'a' && ch <= 'z') return (uint8_t) ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'Z') return (uint8_t) ch - 'A' + 10;
  log2(F(" - Wisol.hexDigitToDecimal: Error: Invalid hex digit "), ch);
  return 0;
}

//  Convert nibble to hex digit.
static const char nibbleToHex[] = "0123456789abcdef";

void Wisol::logBuffer(const __FlashStringHelper *prefix, const char *buffer,
                            uint8_t *markerPos, uint8_t markerCount) {
  //  Log the send/receive buffer for debugging.  markerPos is an array of positions in buffer
  //  where the '>' marker was seen and removed.
  echoPort->print(prefix);
  int m = 0, i = 0;
  for (i = 0; i < strlen(buffer); i = i + 2) {
    if (m < markerCount && markerPos[m] == i) {
      echoPort->print("0x");
      echoPort->write((uint8_t) nibbleToHex[END_OF_RESPONSE / 16]);
      echoPort->write((uint8_t) nibbleToHex[END_OF_RESPONSE % 16]);
      m++;
    }
    echoPort->write((uint8_t) buffer[i]);
    echoPort->write((uint8_t) buffer[i + 1]);
  }
  if (m < markerCount && markerPos[m] == i) {
    echoPort->print("0x");
    echoPort->write((uint8_t) nibbleToHex[END_OF_RESPONSE / 16]);
    echoPort->write((uint8_t) nibbleToHex[END_OF_RESPONSE % 16]);
    m++;
  }
  echoPort->write('\n');
}
