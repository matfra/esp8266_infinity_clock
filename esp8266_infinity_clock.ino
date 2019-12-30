/*
  Inspired from:
  
  Udp NTP Client
  created 4 Sep 2010
  by Michael Margolis
  modified 9 Apr 2012
  by Tom Igoe
  updated for the ESP8266 12 Apr 2015
  by Ivan Grokhotkov

  BasicOTA:
  Rui Santos
  Arduino IDE example: Examples > Arduino OTA > BasicOTA.ino

  Strandtest

*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

const char* ssid = "<your SSID>";
const char* password = "<your password>";
const int ESP_BUILTIN_LED = 2; // Status LED
const int LED_PIN = 14; // D5
const int FIRST_LED = 2; // LED on the 12AM position
const int TIMEZONE = -8; // TZ different in hours (PST = -8, GMT = 0, CET = 1)

//#define DEBUG
#ifdef DEBUG
const int LED_COUNT = 8; // How many LED on the clock?
const char* HOSTNAME = "clockdev";
const bool CCW = false; // The LED order on my strip are counter clockwise
const unsigned long REFRESH_RATE = 1000;
const int BRIGHTNESS = 10; // from 1 to 255
#else
const int LED_COUNT = 44; // How many LED on the clock?
const char* HOSTNAME = "clock";
const bool CCW = true; // The LED order on my strip are counter clockwise
const unsigned long REFRESH_RATE = 80;
const int BRIGHTNESS = 100; // from 1 to 255
#endif

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)




// Variables and const related to NTP
unsigned int localPort = 2390;      // local port to listen for UDP packets
/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// Initialize the variable containing the time to 0
unsigned long millisSinceTwelve = 43200001;

// Start TCP server on port 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;
// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;


// These are the default colors we start with. They can be updated via Web UI
char* rgbHour = "0010DD";
char* rgbMin = "FF3030";
char* rgbSec = "DD8800";

void setup() {

  // Setup Serial
  Serial.begin(115200);
  Serial.println("Booting");
  
  // Setup WiFi
  WiFi.mode(WIFI_STA);
  // Advertise the hostname to the DHCP server
  WiFi.hostname(HOSTNAME);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  
  //Setup OTA
  ArduinoOTA.setHostname(HOSTNAME);
  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  
  // Print local IP infos
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup onboard LED, used for system diagnosis
  pinMode(ESP_BUILTIN_LED, OUTPUT);

  // Setup the NeoPixel LED
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(BRIGHTNESS); // Set BRIGHTNESS between 1 and 255




  // Start HTTP server
  server.begin();
}



void loop() {
  // First thing is to get the uptime
  unsigned long beginTime = millis();
 
  // Every loop, we listen for incoming OTA
  ArduinoOTA.handle();

  // Handle Incoming connections
  WiFiClient client = server.available();

  // Every 12 hours we fetch the time via NTP
  if ( millisSinceTwelve > ( 1000 * 60 * 60 * 12 ) ) {
    Serial.println("Fetching time");
    unsigned long secsSince1900 = get_time();
    Serial.print("Seconds since Jan 1 1900: ");
    Serial.println(secsSince1900);
    
    // We ditch the date as we don't need it, although it could be useful for proper 
    // DST support. Currently we just add TZ offset manually
    millisSinceTwelve = 1000 * (( secsSince1900 + ( 3600 * TIMEZONE ))  % 43200L);       
  }

  // Calculate the useful values
  unsigned int secsSinceTwelve = millisSinceTwelve / 1000L;
  unsigned int ms = millisSinceTwelve % 1000; 
  uint8_t hour = ( secsSinceTwelve / 3600 ) % 12;

  // TODO change the required define DEBUG to a Serial auto detection
#ifdef DEBUG
  uint8_t minute = ( secsSinceTwelve % ( 60 * 60 )) / 60;
  uint8_t second = secsSinceTwelve % 60;    
  Serial.println(""); 
  Serial.print("The time is ");    
  Serial.print(hour); // print the hour (86400 equals secs per day)
  Serial.print(":");
  if (minute < 10) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    Serial.print("0");
  }
  Serial.print(minute); // print the minute (3600 equals secs per minute)
  Serial.print(":");
  if (second < 10) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    Serial.print("0");
  }
  Serial.print(second); // print the second
  Serial.print(".");
  Serial.println(ms); // print the millis
#endif

  // Initialize the table to hold the pixel color
  uint8_t redArray[LED_COUNT];
  uint8_t greenArray[LED_COUNT];
  uint8_t blueArray[LED_COUNT];
  init_array(redArray, greenArray, blueArray);


  if (client) {
    handleWebRequest(client, rgbHour, rgbMin, rgbSec);
  }

  hour_to_slice_of_pixels(hour, rgbHour, redArray, greenArray, blueArray);
  sec_to_minute_pixel(secsSinceTwelve % 3600, rgbMin, redArray, greenArray, blueArray);
  smooth_time_to_pixels(round((millisSinceTwelve % 60000L) / 100) , rgbSec, redArray, greenArray, blueArray);
  
  sendPixels(redArray, greenArray, blueArray);


  // Here is the entire clock part of this. Seems to work good enough
  // We sleep the duration of our set refresh interval - the etimated runtime of this loop
  unsigned long loopTime = millis() - beginTime;
  
#ifdef DEBUG
  Serial.print("Loop time: ");
  Serial.println(loopTime); 
#endif

  if (loopTime > REFRESH_RATE) {
    millisSinceTwelve += loopTime ;
  } else {
    millisSinceTwelve += REFRESH_RATE ;
    delay( REFRESH_RATE - loopTime );
  }
}

int hexStringToInt(char* hexstring) {
  int result = (int)strtol(hexstring, NULL, 16);
  return result;
}

void str_split(char* str, char* dest, int start, int end) {
  int j = 0;
  for (int i = start; i <= end; i++ ) {
    dest[j] = str[i];
    j++;    
  }
  dest[j] = '\0';     
}

void byte_to_str(char* buff, uint8_t val) {  // convert an 8-bit byte to a string of 2 hexadecimal characters
  buff[0] = nibble_to_hex(val >> 4);
  buff[1] = nibble_to_hex(val);
}

char nibble_to_hex(uint8_t nibble) {  // convert a 4-bit nibble to a hexadecimal character
  nibble &= 0xF;
  return nibble > 9 ? nibble - 10 + 'A' : nibble + '0';
}

int getRed(char* color) {
  char buffer[2] ;
  buffer[0]=color[0];
  buffer[1]=color[1];
  return hexStringToInt(buffer);;
}

int getGreen(char* color) {
  char buffer[2] ;
  buffer[0]=color[2];
  buffer[1]=color[3];
  return hexStringToInt(buffer);;
}  

int getBlue(char* color) {
  char buffer[2] ;
  buffer[0]=color[4];
  buffer[1]=color[5];
  return hexStringToInt(buffer);;
}  

void handleWebRequest(WiFiClient client, char* rgbHour, char* rgbMin, char* rgbSec) {
  // If a new client connects,
  Serial.println("New Client.");          // print a message out in the serial port
  String currentLine = "";                // make a String to hold incoming data from the client
  currentTime = millis();
  previousTime = currentTime;
  while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop while the client's connected
    currentTime = millis();         
    if (client.available()) {             // if there's bytes to read from the client,
      char c = client.read();             // read a byte, then
      Serial.write(c);                    // print it out the serial monitor
      // TODO: Get rid of the header string all together and just keep the char
      header += c;
      if (c == '\n') {                    // if the byte is a newline character
        // if the current line is blank, you got two newline characters in a row.
        // that's the end of the client HTTP request, so send a response:
        if (currentLine.length() == 0) {

          if (header.indexOf("GET /color/") >= 0) {
            char path[20];
            header.toCharArray(path, 20);
            char color[6];
            str_split(path, color, 13, 18);
            Serial.println("Received color: ");
            Serial.println(color); 
            switch (path[11]) {
              case 'h':
                strcpy(rgbHour, color);
                Serial.println("Updating color for hours");
                break;
              case 'm':
                strcpy(rgbMin, color);
                Serial.println("Updating color for min");
                break;
              case 's':
                strcpy(rgbSec, color);
                Serial.println("Updating color for sec");
                break;
              default:
                Serial.println("Did not recognize any time");
            }
            client.println("HTTP/1.1 204 No Content");
            client.println("Connection: close");           
            
          } else if (header.indexOf("GET / HTTP") >= 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
          // Display the HTML web page
            client.println("<!doctype html>");
            client.println("<html><head>");
            client.println("<title>Infinity Clock color picker</title>");
            client.println("<style>input { width: 100%; height: 25%;}</style>");
            client.println("<style>h1 { color: white; text-align: center; font-family: monospace}</style>");
            client.println("</head>");
            client.println("<body style='background-color: black;'>");
            client.println("<div style='height: 100vh;'>");
            client.println("<h1>hours</h1>");
            client.printf("<input id='h' value='#%s' type='color'>\n", rgbHour);
            client.println("<h1>minutes</h1>");
            client.printf("<input id='m' value='#%s' type='color'>\n", rgbMin);
            client.println("<h1>seconds</h1>");
            client.printf("<input id='s' value='#%s' type='color'>\n", rgbSec);
            client.println("</div>");
            client.println("<script>");
            client.println(" const Http = new XMLHttpRequest();");
            client.println("  function send(color) {");
            client.println("    url=['', 'color', color.id, color.value.slice(1)].join('/');");
            client.println("    console.log(url);");
            client.println("    Http.open('GET', url);");
            client.println("    Http.send();");
            client.println("    Http.onreadystatechange = (e) => {");
            client.println("      console.log(Http.responseText)");
            client.println("    }");
            client.println("  }");
            client.println("      var hBox = document.getElementById('h');");
            client.println("      hBox.addEventListener('change', function() {send(hBox)});");
            client.println("      var mBox = document.getElementById('m');");
            client.println("      mBox.addEventListener('change', function() {send(mBox)});");
            client.println("      var sBox = document.getElementById('s');");
            client.println("      sBox.addEventListener('change', function() {send(sBox)});");
            client.println("</script>");
            client.println("</body>");
            client.println("</html>");
            
          } else {
            client.println("HTTP/1.1 404 Not Found");
            client.println("Connection: close");
            
          }

          // The HTTP response ends with another blank line
          client.println();
          // Break out of the while loop
          break;
        } else { // if you got a newline, then clear currentLine
          currentLine = "";
        }
      } else if (c != '\r') {  // if you got anything else but a carriage return character,
        currentLine += c;      // add it to the end of the currentLine
      }
    }
  }
  // Clear the header variable
  header = "";
  // Close the connection
  client.stop();
  Serial.println("Client disconnected.");
  Serial.println("");
}

void init_array(uint8_t redArray[], uint8_t greenArray[], uint8_t blueArray[]) {
  // Initialize the "framebuffer" to 0 (black)
    for(uint8_t i=0; i<LED_COUNT; i++) {
    redArray[i]=0;
    greenArray[i]=0;
    blueArray[i]=0;
  }
}

//TODO Distance should return a result in degres instead of absolute value
unsigned int distance(unsigned int a, unsigned int b, unsigned int base) {
  return (base + b - a) % base;
}

// TODO This exp function should is currently calibtrated for 44 LED and 0.1s resolution
uint8_t dim_color(uint8_t color, unsigned int dist) {
  return ceil( color / pow( 1.05, (double) dist));
}

// Create a nice trail of dimmed pixels behind the active second
void smooth_time_to_pixels(unsigned int time_in_deciseconds, char* color, uint8_t redArray[], uint8_t greenArray[], uint8_t blueArray[]) {
  uint8_t red = getRed(color);
  uint8_t green = getGreen(color);
  uint8_t blue = getBlue(color);
    
  for(int i = 0; i < LED_COUNT; i++) {
      unsigned int dist = distance(floor(600*(1+i)/(LED_COUNT) ), time_in_deciseconds, 600); 
      // Add the values to existing values in the tables.  
      redArray[i]=min(255, dim_color(red, dist) + redArray[i]);
      greenArray[i]=min(255, dim_color(green, dist) + greenArray[i]);
      blueArray[i]=min(255, dim_color(blue, dist) + blueArray[i]);
  }  
}

// Simple display the minute on the clock that is the closest to where it should actually be
void sec_to_minute_pixel(unsigned int seconds_since_jour_begin, char* color, uint8_t redArray[], uint8_t greenArray[], uint8_t blueArray[]) {
  uint8_t red = getRed(color);
  uint8_t green = getGreen(color);
  uint8_t blue = getBlue(color);
  uint8_t i = ceil(seconds_since_jour_begin * (LED_COUNT - 1 ) / 3600);
  redArray[i]=min(255, red + redArray[i]);
  greenArray[i]=min(255, green + greenArray[i]);
  blueArray[i]=min(255, blue + blueArray[i]);
}

// Display hours in an antire slice to ease reading the time
void hour_to_slice_of_pixels(uint8_t time_value, char* color, uint8_t redArray[], uint8_t greenArray[], uint8_t blueArray[]) {
  uint8_t red = getRed(color);
  uint8_t green = getGreen(color);
  uint8_t blue = getBlue(color);
  uint8_t base = 12;
  int t = time_value % base; // Make sure we are in base 12
  int first_led;
  int last_led;
  if ( LED_COUNT > base) { // If more LED than hours/minute/secondes we want to illuminate the entire slice on a clock, like a slice of pie.
    int slice_int_size = floor(LED_COUNT / base);
    first_led = round(t * LED_COUNT / base);
    last_led = first_led + max(slice_int_size - 1, 1);

  } else { 
    first_led = ceil(t * LED_COUNT / base) - 1;
    last_led = first_led;
  }
  
  for(int i = first_led; i <= last_led; i++) {
    redArray[i]=min(255, red + redArray[i]);
    greenArray[i]=min(255, green + greenArray[i]);
    blueArray[i]=min(255, blue + blueArray[i]);
  }
}


void sendPixels(uint8_t redArray[], uint8_t greenArray[], uint8_t blueArray[]) {
  int rotationDirection = 1;
  if(CCW == true) {
    rotationDirection = -1;
  }
   
  for (uint8_t i=0; i<LED_COUNT; i++) {
    int realPixelNum = ( 2 * LED_COUNT - FIRST_LED + ( i * rotationDirection ) ) % LED_COUNT;
    strip.setPixelColor(realPixelNum, redArray[i], greenArray[i], blueArray[i]);  
  }
  strip.show();
}


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


long get_time() {
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
    //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);

  int cb = udp.parsePacket();
  if (!cb) {
    return 0;
  } else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    return highWord << 16 | lowWord;
  }
}
