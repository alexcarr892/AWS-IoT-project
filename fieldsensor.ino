#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <time.h>

/* ================= WIFI ================= */
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

/* ================= AWS IOT ================= */
const char* AWS_ENDPOINT = "a2714g0o311sy3-ats.iot.us-east-1.amazonaws.com";
const int   AWS_PORT     = 8883;

const char* CLIENT_ID  = "field-sensor-1";
const char* MQTT_TOPIC = "farm/field/groundtemp/field-sensor-1";

/* ================= CERTS ================= */

// Amazon Root CA 1 — download from https://www.amazontrust.com/repository/AmazonRootCA1.pem
static const char AWS_ROOT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// Device certificate — paste your AWS IoT device certificate here
static const char DEVICE_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
YOUR_DEVICE_CERTIFICATE_HERE
-----END CERTIFICATE-----
)EOF";

// RSA Private Key — paste your AWS IoT private key here (keep this secret, never commit real keys)
static const char PRIVATE_KEY[] = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
YOUR_PRIVATE_KEY_HERE
-----END RSA PRIVATE KEY-----
)EOF";

/* ================= OBJECTS ================= */
WiFiClientSecure net;
PubSubClient client(net);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

/* ================= FUNCTIONS ================= */

void connectWiFi() {
  Serial.print("Connecting to WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
}

void syncTime() {
  Serial.print("Syncing time");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);

  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println("\nTime synced");
}

void connectAWS() {
  net.setHandshakeTimeout(30);
  net.setCACert(AWS_ROOT_CA);
  net.setCertificate(DEVICE_CERT);
  net.setPrivateKey(PRIVATE_KEY);

  client.setServer(AWS_ENDPOINT, AWS_PORT);

  Serial.print("Connecting to AWS IoT");

  int attempts = 0;
  while (!client.connected()) {
    if (client.connect(CLIENT_ID)) {
      Serial.println("\n✅ Connected to AWS IoT");
      return;
    } else {
      Serial.print(".");
      Serial.print(" rc=");
      Serial.print(client.state());
      attempts++;

      if (attempts >= 10) {
        Serial.println("\n❌ FAILED to connect to AWS IoT");
        return;
      }

      delay(1000);
    }
  }
}

void publishSensorData() {
  if (!client.connected()) {
    Serial.println("❌ MQTT NOT CONNECTED — SKIPPING PUBLISH");
    return;
  }

  float groundTemp = mlx.readObjectTempC();
  float airTemp    = mlx.readAmbientTempC();

  if (isnan(groundTemp) || isnan(airTemp)) {
    Serial.println("❌ Sensor read failed");
    return;
  }

  String payload = "{";
  payload += "\"deviceId\":\"field-sensor-1\",";
  payload += "\"groundSurfaceTempC\":" + String(groundTemp, 2) + ",";
  payload += "\"airTempC\":"           + String(airTemp, 2)    + ",";
  payload += "\"location\":\"test-field\"";
  payload += "}";

  if (client.publish(MQTT_TOPIC, payload.c_str())) {
    Serial.println("✅ Published to AWS IoT:");
    Serial.println(payload);
  } else {
    Serial.println("❌ Publish FAILED");
  }
}

/* ================= ARDUINO ================= */

void setup() {
  Serial.begin(115200);
  delay(2000);

  setCpuFrequencyMhz(240);
  WiFi.setSleep(false);

  Wire.begin(21, 22);

  if (!mlx.begin()) {
    Serial.println("❌ MLX90614 not detected — check wiring");
    while (1);
  }

  Serial.println("MLX90614 ready");

  connectWiFi();
  syncTime();
  connectAWS();
}

void loop() {
  if (!client.connected()) {
    connectAWS();
  }

  client.loop();
  publishSensorData();

  delay(15000);  // publish every 15 seconds
}
