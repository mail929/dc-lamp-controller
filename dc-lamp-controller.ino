#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>

// wifi
const char SSID[] = "-----";
const char PASS[] = "liamisthebest";

int status = WL_IDLE_STATUS;
WiFiServer server(80);

// mqtt
const IPAddress MQTT_SERVER(192, 168, 16, 11);
const int MQTT_PORT = 1883;
const char* DEVICE_ID = "living_room_lamp_1";
const char* STATE_TOPIC = "Home/Living Room Lamp/light_state";
const char* MODE_TOPIC = "Home/Living Room Lamp/light_mode";
const char* LEVEL_TOPIC = "Home/Living Room Lamp/light_level";
const int MQTT_UPDATE_FREQ = 100;
WiFiClient client;
PubSubClient mqtt_client(client);
int mqtt_tics = 0;

// pin connections
const int CONTROL_PIN   = 3;
const int BUTTON_PIN    = 4;
const int INDICATOR_PIN = 13;

// constants
const int OFF_BRIGHTNESS   = 0;
const int MAX_BRIGHTNESS   = 255;
const int LIGHT_LEVELS     = 10;
const int TIC_LENGTH       = 100;

// light modes
const char* const MODE_NAMES[] = { "STATIC", "FLASH", "BREATH", "PULSE" };
const int STATIC_MODE = 0;
const int FLASH_MODE  = 1;
const int BREATH_MODE = 2;
const int PULSE_MODE  = 3;

// light states
const char* const STATE_NAMES[] = { "OFF", "ON" };
const int OFF_STATE = 0;
const int ON_STATE  = 1;

// light state
int light_level = OFF_BRIGHTNESS;
int light_mode = STATIC_MODE;
int light_state = OFF_STATE;
int mode_data = OFF_BRIGHTNESS;
int tics = 0;
bool button_state = false;

void setup()
{
  // init serial
  Serial.begin(9600);
  Serial.println("Serial initialized");

  // init pins
  pinMode(CONTROL_PIN,   OUTPUT);
  pinMode(BUTTON_PIN,    INPUT);
  pinMode(INDICATOR_PIN, OUTPUT);

  analogWrite(CONTROL_PIN, OFF_BRIGHTNESS);
  digitalWrite(INDICATOR_PIN, LOW);

  // check for the presence of NIC
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println("WiFi shield not present");
    //while (true);
  }

  if (WiFi.firmwareVersion() != "1.1.0")
  {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED)
  {
    Serial.print("Attempting to connect to network named: ");
    Serial.println(SSID); 

    // Connect to WPA/WPA2 network
    status = WiFi.begin(SSID, PASS);
    delay(10000);
  }

  mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt_client.setCallback(onUpdate);
  server.begin();
  printWifiStatus();
  
  // boot up sequence
  for (int i = 0; i < 3; ++i)
  {
    digitalWrite(INDICATOR_PIN, HIGH);
    delay(500);
    digitalWrite(INDICATOR_PIN, LOW);
    delay(500);
  }
}

void loop()
{
  mqtt_client.loop();
    
  // check for new button press
  bool new_button_state = digitalRead(BUTTON_PIN);
  if (new_button_state == LOW && button_state == HIGH)
  {
    // toggle light
    if (light_state == OFF_STATE)
    {
      light_state = ON_STATE;
    }
    else
    {
      light_state = OFF_STATE;
    }
  }
  button_state = new_button_state;

  int tics_per_side = 0;
  int delta_per_side = 0;
    
  // set light value
  switch (light_mode)
  {
  case STATIC_MODE:
    light_level = mode_data;
    break;
    
  case FLASH_MODE:
    if (tics >= 2000 / TIC_LENGTH)
    {
      tics = 0;
    }
    if (tics >= 1000 / TIC_LENGTH)
    {
      light_level = mode_data;
    }
    else
    {
      light_level = OFF_BRIGHTNESS;
    }
    break;
    
  case BREATH_MODE:
    tics_per_side = 1000 / TIC_LENGTH;
    delta_per_side = mode_data / tics_per_side;
    if (tics >= 4000 / TIC_LENGTH)
    {
      tics = 0;
    }
    else if (tics >= 3000 / TIC_LENGTH)
    {
      // decreasing
      light_level -= delta_per_side;
    }
    else if (tics >= 2000 / TIC_LENGTH)
    {
      // on
      light_level = mode_data;
    }
    else if (tics >= 1000 / TIC_LENGTH)
    {
      // increasing
      light_level += delta_per_side;
    }
    else
    {
      // min
      light_level = OFF_BRIGHTNESS;
    }
    break;
    
  case PULSE_MODE:
    tics_per_side = 2000 / TIC_LENGTH;
    delta_per_side = mode_data / tics_per_side;
    if (tics > 2 * tics_per_side)
    {
      tics = 0;
    }
    else if (tics > tics_per_side)
    {
      // decreasing
      light_level -= delta_per_side;
    }
    else
    {
      // increasing
      light_level += delta_per_side;
    }
    break;
  }

  // keep brightness within bounds
  if (light_level > MAX_BRIGHTNESS)
  {
    light_level = MAX_BRIGHTNESS;
  }
  else if (light_level < OFF_BRIGHTNESS)
  {
    light_level = OFF_BRIGHTNESS;
  }

  // output
  if (light_state == ON_STATE)
  {
    analogWrite(CONTROL_PIN, light_level);
    digitalWrite(INDICATOR_PIN, LOW);
    //Serial.println(light_level);
  }
  else
  {
    analogWrite(CONTROL_PIN, 0);
    digitalWrite(INDICATOR_PIN, HIGH);
  }

  // mqtt
  if (mqtt_tics == 0)
  {
    if (!mqtt_client.connected())
    {
      Serial.println("Reconnecting");
      mqtt_client.connect(DEVICE_ID);
      mqtt_client.subscribe(STATE_TOPIC);
      mqtt_client.subscribe(MODE_TOPIC);
      mqtt_client.subscribe(LEVEL_TOPIC);
    }
    mqtt_client.publish(STATE_TOPIC, STATE_NAMES[light_state]);
    mqtt_client.publish(MODE_TOPIC, MODE_NAMES[light_mode]);
    char buff[3];
    mqtt_client.publish(LEVEL_TOPIC, itoa(mode_data, buff, 10));
  }
  ++mqtt_tics;
  if (mqtt_tics == MQTT_UPDATE_FREQ)
  {
    mqtt_tics = 0;
  }

  // tic
  delay(TIC_LENGTH);
  ++tics;
}

void onUpdate(char* topic, byte* payload, unsigned int len)
{
  // TODO look into preventing updates from Arduino coming through
  //Serial.print("Got update on ");
  //Serial.print(topic);
  //Serial.print(" of length ");
  //Serial.println(len);

  // convert payload to char array
  char value[len+1];
  for (int i = 0; i < len; ++i)
  {
    value[i] = (char) payload[i];
  }
  value[len] = '\0';

  // convert to upper case
  String val_str = String(value);
  val_str.toUpperCase();
  val_str.toCharArray(value, len+1);

  // check topic
  if (strcmp(topic, MODE_TOPIC) == 0)
  {
    // get index of given light mode
    for (int i = 0; i < 4; ++i)
    {
      if (strcmp(MODE_NAMES[i], value) == 0)
      {
        if (light_mode != i)
        {
          light_mode = i;
          Serial.print("Light mode updated to: ");
          Serial.println(i);
        }
        break;
      }
    }
  }
  else if (strcmp(topic, STATE_TOPIC) == 0)
  {
    // get index of given light state
    for (int i = 0; i < 2; ++i)
    {
      if (strcmp(STATE_NAMES[i], value) == 0)
      {
        if (light_state != i)
        {
          light_state = i;
          if (light_state == ON_STATE && mode_data == 0) {
            mode_data = MAX_BRIGHTNESS;
          }
          Serial.print("Light state updated to: ");
          Serial.println(i);
        }
        break;
      }
    }
  }
  else if (strcmp(topic, LEVEL_TOPIC) == 0)
  {
    int level = val_str.toInt();
    if (level != mode_data)
    {
      mode_data = level;
      Serial.print("Light level updated to: ");
      Serial.println(level);
    }
  }
}

void printWifiStatus()
{
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(rssi);
  Serial.println(" dBm");
  
  Serial.print("Brightness: ");
  Serial.print(100 * light_level / 255);
  Serial.println("%");
}
