//  Library for sending and receiving SIGFOX messages with Arduino shield based on Radiocrafts RC1692HP-SIG.
#include <stdlib.h>
#include "SIGFOX.h"
#include "Radiocrafts.h"

static const char *CMD_READ_MEMORY = "59";  //  'Y' to read memory.
static const char *CMD_ENTER_CONFIG = "4d";  //  'M' to enter config mode.
static const char *CMD_EXIT_CONFIG = "ff";  //  Exit config mode.

static NullPort nullPort;
static int markers = 0;

/* TODO: Run some sanity checks to ensure that Radiocrafts module is configured OK.
  //  Get network mode for transmission.  Should return network mode = 0 for uplink only, no downlink.
  Serial.println(F("\nGetting network mode (expecting 0)..."));
  transceiver.getParameter(0x3b, result);

  //  Get baud rate.  Should return baud rate = 5 for 19200 bps.
  Serial.println(F("\nGetting baud rate (expecting 5)..."));
  transceiver.getParameter(0x30, result);
*/

Radiocrafts::Radiocrafts(): Radiocrafts(RADIOCRAFTS_RX, RADIOCRAFTS_TX) {}  //  Forward to constructor below.

Radiocrafts::Radiocrafts(unsigned int rx, unsigned int tx) {
  //  Init the module with the specified transmit and receive pins.
  //  Default to no echo.
  //Serial.begin(9600); Serial.print(String(F("Radiocrafts.Radiocrafts: (rx,tx)=")) + rx + ',' + tx + '\n');
  serialPort = new SoftwareSerial(rx, tx);
  echoPort = &nullPort;
  lastEchoPort = &Serial;
  _lastSend = 0;
}

bool Radiocrafts::begin() {
  //  Wait for the module to power up. Return true if module is ready to send.
  //  TODO: Check communication with SIGFOX module.
  delay(2000);
  _lastSend = 0;
  return true;
}

bool Radiocrafts::sendMessage(const String payload) {
  //  Payload contains a string of hex digits, up to 24 digits / 12 bytes.
  //  We convert to binary and send to SIGFOX.  Return true if successful.
  //  We represent the payload as hex instead of binary because 0x00 is a
  //  valid payload and this causes string truncation in C libraries.
  echoPort->print(String(F("Radiocrafts.sendPayload: ")) + payload + '\n');
  if (!isReady()) return false;  //  Prevent user from sending too many messages.
  //  Decode and send the data.
  //  First byte is payload length, followed by rest of payload.
  String message = toHex((char) (payload.length() / 2)) + payload, data = "";
  if (sendCommand(message, 0, data, markers)) {  //  No markers expected.
    echoPort->println(data);
    _lastSend = millis();
    return true;
  }
  return false;
}

bool Radiocrafts::sendCommand(const String command, const int timeout,
  const int expectedMarkerCount, String &dataOut, int &actualMarkerCount) {
  //  command contains a string of hex digits, up to 24 digits / 12 bytes.
  //  We convert to binary and send to SIGFOX.  Return true if successful.
  //  We represent the payload as hex instead of binary because 0x00 is a
  //  valid payload and this causes string truncation in C libraries.
  //  expectedMarkerCount is the number of end-of-command markers '>' we
  //  expect to see.  actualMarkerCount contains the actual number seen.
  echoPort->print(String(F("Radiocrafts.sendCommand: ")) + command + '\n');
  actualMarkerCount = 0;
  //  Start serial interface.
  serialPort->begin(MODEM_BITS_PER_SECOND);
  delay(200);
  serialPort->flush();
  serialPort->listen();

  //  Send the command: need to write/read char by char because of echo.
  const char *commandBuffer = command.c_str();
  //  Send command and read response.  Loop until timeout or we see the end of response marker.
  const unsigned long startTime = millis(); int i = 0;
  String response = "", echoSend = "", echoReceive = "";
  for (;;) {
    //  If there is data to send, send it.
    if (i < command.length()) {
      //  Convert 2 hex digits to 1 char and send.
      uint8_t txChar = hexDigitToDecimal(commandBuffer[i]) * 16 +
                       hexDigitToDecimal(commandBuffer[i + 1]);
      echoSend.concat(toHex((char) txChar) + ' ');
      serialPort->write(txChar);
      i = i + 2;
    }

    //  If timeout, quit.
    const unsigned long currentTime = millis();
    if (currentTime - startTime > timeout) break;

    //  If data is available to receive, receive it.
    if (serialPort->available() > 0)
    {
      int rxChar = serialPort->read();
      echoReceive.concat(toHex((char) rxChar) + ' ');
      if (rxChar == -1) continue;
      if (rxChar == END_OF_RESPONSE) {
        actualMarkerCount++;  //  Count the number of end markers.
        if (actualMarkerCount >= expectedMarkerCount) break;  //  Seen all markers already.
      }
      else {
        response.concat(toHex((char) rxChar));
      }
    }
  }
  serialPort->end();
  echoPort->print(">> ");  echoPort->println(echoSend);
  if (echoReceive.length() > 0) { echoPort->print("<< ");  echoPort->println(echoReceive); }
  //  If we did not see the terminating '>', something is wrong.
  if (actualMarkerCount < expectedMarkerCount) {
    if (response.length() == 0)
      echoPort->println(F("Radiocrafts.sendCommand: Error: No response"));  //  Response timeout.
    else
      echoPort->println(String(F("Radiocrafts.sendCommand: Error: Unknown response: ")) + response);
    return false;
  }
  echoPort->println(String(F("Radiocrafts.sendCommand: response: ")) + response);
  dataOut = response;

  //  TODO: Parse the downlink response.
  return true;
}

bool Radiocrafts::sendCommand(String cmd, int expectedMarkerCount,
                              String &result, int &actualMarkerCount) {
  //  cmd contains a string of hex digits, up to 24 digits / 12 bytes.
  //  We convert to binary and send to SIGFOX.  Return true if successful.
  String data = "";
  if (!sendCommand(cmd, COMMAND_TIMEOUT, expectedMarkerCount,
                   data, actualMarkerCount)) return false;
  result = data;
  return true;
}

bool Radiocrafts::sendString(const String str) {
  //  For convenience, allow sending of a text string with automatic encoding into bytes.  Max 12 characters allowed.
  //  Convert each character into 2 bytes.
  echoPort->print(F("Radiocrafts.sendString: "));  echoPort->println(str);
  String payload = "";
  for (unsigned i = 0; i < str.length(); i++) {
    char ch = str.charAt(i);
    payload.concat(toHex(ch));
  }
  //  Send the encoded payload.
  return sendMessage(payload);
}

bool Radiocrafts::isReady()
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
  if (_lastSend == 0) return true;  //  First time sending.
  const unsigned long elapsedTime = currentTime - _lastSend;
  //  For development, allow sending every 5 seconds.
  if (elapsedTime <= 5 * 1000) {
    echoPort->println(F("Must wait 5 seconds before sending the next message"));
    return false;
  }  //  Wait before sending.
  if (elapsedTime <= SEND_DELAY)
    echoPort->println(F("Warning: Should wait 10 mins before sending the next message"));
  return true;
}

static String data = "";

bool Radiocrafts::enterCommandMode() {
  //  Enter Command Mode for sending module commands, not data.
  //  TODO: Confirm response = '>'
  if (!sendCommand("00", 1, data, markers)) return false;
  echoPort->println(F("Radiocrafts.enterCommandMode: OK "));
  return true;
}

bool Radiocrafts::exitCommandMode() {
  //  Exit Command Mode so we can send data.
  if (!sendCommand(toHex('X'), 0, data, markers)) return false;
  echoPort->println(F("Radiocrafts.exitCommandMode: OK "));
  return true;
}

bool Radiocrafts::getID(String &id, String &pac) {
  //  Get the SIGFOX ID and PAC for the module.
  if (!sendCommand(toHex('9'), 1, data, markers)) return false;
  //  Returns with 12 bytes: 4 bytes ID (LSB first) and 8 bytes PAC (MSB first).
  if (data.length() != 12 * 2) {
    echoPort->println(String(F("Radiocrafts.getID: Unknown response: ")) + data);
    return false;
  }
  id = data.substring(6, 8) + data.substring(4, 6) + data.substring(2, 4) + data.substring(0, 2);
  pac = data.substring(8, 8 + 16);
  echoPort->println(String(F("Radiocrafts.getID: returned id=")) + id + ", pac=" + pac);
  return true;
}

bool Radiocrafts::getTemperature(int &temperature) {
  //  Returns the temperature of the SIGFOX module.
  if (!sendCommand(toHex('U'), 1, data, markers)) return false;
  if (data.length() != 2) {
    echoPort->println(String(F("Radiocrafts.getTemperature: Unknown response: ")) + data);
    return false;
  }
  temperature = hexDigitToDecimal(data.charAt(0)) * 16 +
                   hexDigitToDecimal(data.charAt(1)) - 128;
  echoPort->print(F("Radiocrafts.getTemperature: returned "));  echoPort->println(temperature);
  return true;
}

bool Radiocrafts::getVoltage(float &voltage) {
  //  Returns one byte indicating the power supply voltage.
  if (!sendCommand(toHex('V'), 1, data, markers)) return false;
  if (data.length() != 2) {
    echoPort->println(String(F("Radiocrafts.getVoltage: Unknown response: ")) + data);
    return false;
  }
  voltage = 0.030 * (hexDigitToDecimal(data.charAt(0)) * 16 +
                hexDigitToDecimal(data.charAt(1)));
  echoPort->print(F("Radiocrafts.getVoltage: returned "));  echoPort->println(voltage);
  return true;
}

bool Radiocrafts::getHardware(String &hardware) {
  //  TODO
  echoPort->println(F("Radiocrafts.getHardware: ERROR - Not implemented"));
  hardware = "TODO";
  return true;
}

bool Radiocrafts::getFirmware(String &firmware) {
  //  TODO
  echoPort->println(F("Radiocrafts.getFirmware: ERROR - Not implemented"));
  firmware = "TODO";
  return true;
}

bool Radiocrafts::getParameter(uint8_t address, String &value) {
  //  Read the parameter at the address.
  echoPort->print(F("Radiocrafts.getParameter: address=0x"));  echoPort->println(toHex((char) address));
  if (!sendCommand(String(CMD_READ_MEMORY) +   //  Read memory ('Y')
                   toHex((char) address),  //  Address of parameter
                   2,  //  Expect 1 marker for command, 1 for response.
                   data, markers)) return false;
  value = data;
  echoPort->print(F("Radiocrafts.getParameter: address=0x"));  echoPort->print(toHex((char) address));
  echoPort->print(F(" returned "));  echoPort->println(value);
  return true;
}

bool Radiocrafts::getPower(int &power) {
  //  Get the power step-down.
  if (!getParameter(0x01, data)) return false;  //  Address of parameter = RF_POWER (0x01)
  power = (int) data.toInt();
  echoPort->print(F("Radiocrafts.getPower: returned "));  echoPort->println(power);
  return true;
}

bool Radiocrafts::setPower(int power) {
  //  TODO: Power value: 0...14
  echoPort->println(F("Radiocrafts.receive: ERROR - Not implemented"));
  return true;
}

bool Radiocrafts::getEmulator(int &result) {
  //  Get the current emulation mode of the module.
  //  0 = Emulator disabled (sending to SIGFOX network with unique ID & key)
  //  1 = Emulator enabled (sending to emulator with public ID & key)
  if (!getParameter(0x28, data)) return false;  //  Address of parameter = PUBLIC_KEY (0x28)
  result = (int) data.toInt();
  return true;
}

bool Radiocrafts::disableEmulator(String &result) {
  //  Set the module key to the unique SIGFOX key.  This is needed for sending
  //  to a real SIGFOX base station.
  if (!sendCommand(String(CMD_ENTER_CONFIG) +   //  Tell module to receive address ('M').
      "28" + //  Address of parameter = PUBLIC_KEY (0x28)
      "00",  //  Value of parameter = Unique ID & key (0x00)
       1, data, markers)) { sendCommand(CMD_EXIT_CONFIG, 1, data, markers); return false; }
  result = data;
  sendCommand(CMD_EXIT_CONFIG, 1, data, markers);  //  Exit config mode.
  return true;
}

bool Radiocrafts::enableEmulator(String &result) {
  //  Set the module key to the public key.  This is needed for sending
  //  to an emulator.
  if (!sendCommand(String(CMD_ENTER_CONFIG) +   //  Tell module to receive address ('M').
      "28" + //  Address of parameter = PUBLIC_KEY (0x28)
      "01",  //  Value of parameter = Public ID & key (0x00)
      1, data, markers)) { sendCommand(CMD_EXIT_CONFIG, 1, data, markers); return false; }
  result = data;
  sendCommand(CMD_EXIT_CONFIG, 1, data, markers);  //  Exit config mode.
  return true;
}

bool Radiocrafts::getFrequency(String &result) {
  //  Get the frequency used for the SIGFOX module
  //  0: Europe (RCZ1)
  //  1: US (RCZ2)
  //  3: SG, TW, AU, NZ (RCZ4)
  if (!sendCommand(toHex('Y') + "00", 1, data, markers)) return false;
  result = data;
  return true;
}

bool Radiocrafts::setFrequency(int zone, String &result) {
  //  Get the frequency used for the SIGFOX module
  //  0: Europe (RCZ1)
  //  1: US (RCZ2)
  //  3: AU/NZ (RCZ4)
  if (!sendCommand(String(CMD_ENTER_CONFIG) +   //  Tell module to receive address ('M').
    "00" + //  Address of parameter = RF_FREQUENCY_DOMAIN (0x0)
    toHex((char) (zone - 1)),  //  Value of parameter = RCZ - 1
    1, data, markers)) { sendCommand(CMD_EXIT_CONFIG, 1, data, markers); return false; }
  result = data;
  sendCommand(CMD_EXIT_CONFIG, 1, data, markers);  //  Exit config mode.
  return true;
}

//  Set the frequency for the SIGFOX module to Singapore frequency (RCZ4).
bool Radiocrafts::setFrequencySG(String &result) {
  echoPort->println(F("Radiocrafts.setFrequencySG"));
  return setFrequency(4, result); }

//  Set the frequency for the SIGFOX module to Taiwan frequency (RCZ4).
bool Radiocrafts::setFrequencyTW(String &result) {
  echoPort->println(F("Radiocrafts.setFrequencyTW"));
  return setFrequency(4, result); }

//  Set the frequency for the SIGFOX module to ETSI frequency for Europe (RCZ1).
bool Radiocrafts::setFrequencyETSI(String &result) {
  echoPort->println(F("Radiocrafts.setFrequencyETSI"));
  return setFrequency(1, result); }

//  Set the frequency for the SIGFOX module to US frequency (RCZ2).
bool Radiocrafts::setFrequencyUS(String &result) {
  echoPort->println(F("Radiocrafts.setFrequencyUS"));
  return setFrequency(2, result); }

bool Radiocrafts::writeSettings(String &result) {
  //  TODO: Write settings to module's flash memory.
  echoPort->println(F("Radiocrafts.writeSettings: ERROR - Not implemented"));
  return true;
}

bool Radiocrafts::reboot(String &result) {
  //  TODO: Reboot the module.
  echoPort->println(F("Radiocrafts.reboot: ERROR - Not implemented"));
  return true;
}

//  Echo commands and responses to the echo port.
void Radiocrafts::echoOn() {
  echoPort = lastEchoPort;
  echoPort->println(F("Radiocrafts.echoOn"));
}

//  Stop echoing commands and responses to the echo port.
void Radiocrafts::echoOff() {
  lastEchoPort = echoPort; echoPort = &nullPort;
}

void Radiocrafts::setEchoPort(Print *port) {
  //  Set the port for sending echo output.
  lastEchoPort = echoPort;
  echoPort = port;
}

void Radiocrafts::echo(String msg) {
  //  Echo debug message to the echo port.
  echoPort->println(msg);
}

bool Radiocrafts::receive(String &data) {
  //  TODO
  echoPort->println(F("Radiocrafts.receive: ERROR - Not implemented"));
  return true;
}

String Radiocrafts::toHex(int i) {
  byte * b = (byte*) & i;
  String bytes = "";
  for (int j=0; j<2; j++) {
    if (b[j] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[j], 16));
  }
  return bytes;
}

String Radiocrafts::toHex(unsigned int ui) {
  byte * b = (byte*) & ui;
  String bytes = "";
  for (int i=0; i<2; i++) {
    if (b[i] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Radiocrafts::toHex(long l) {
  byte * b = (byte*) & l;
  String bytes = "";
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Radiocrafts::toHex(unsigned long ul) {
  byte * b = (byte*) & ul;
  String bytes = "";
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Radiocrafts::toHex(float f) {
  byte * b = (byte*) & f;
  String bytes = "";
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Radiocrafts::toHex(double d) {
  byte * b = (byte*) & d;
  String bytes = "";
  for (int i=0; i<4; i++) {
    if (b[i] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

String Radiocrafts::toHex(char c) {
  byte *b = (byte*) & c;
  String bytes = "";
  if (b[0] <= 0xF) bytes.concat("0");
  bytes.concat(String(b[0], 16));
  return bytes;
}

String Radiocrafts::toHex(char *c, int length) {
  byte * b = (byte*) c;
  String bytes = "";
  for (int i=0; i<length; i++) {
    if (b[i] <= 0xF) bytes.concat("0");
    bytes.concat(String(b[i], 16));
  }
  return bytes;
}

uint8_t Radiocrafts::hexDigitToDecimal(char ch) {
  //  Convert 0..9, a..f, A..F to decimal.
  if (ch >= '0' && ch <= '9') return (uint8_t) ch - '0';
  if (ch >= 'a' && ch <= 'z') return (uint8_t) ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'Z') return (uint8_t) ch - 'A' + 10;
  echoPort->print(F("Radiocrafts.hexDigitToDecimal: Error: Invalid hex digit "));
  echoPort->println(ch);
  return 0;
}

