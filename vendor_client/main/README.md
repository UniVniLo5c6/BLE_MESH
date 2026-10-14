## Configuration

Before building the firmware, configure your local Wi-Fi and MQTT broker settings.

### 1. Wi-Fi Configuration

Open the source file containing the Wi-Fi configuration and replace the placeholder values with your own:

- `WIFI_SSID`: Your Wi-Fi network name.
- `WIFI_PASS`: Your Wi-Fi password.

### 2. MQTT Configuration

Configure the MQTT broker connection:

- `MQTT_URI`: The MQTT broker URI, including the protocol and port.
- `MQTT_USER`: Your MQTT username.
- `MQTT_PASS`: Your MQTT password.

This project uses MQTT over TLS. Ensure that the broker URI and CA certificate match your broker configuration.

### 3. CA Certificate

Obtain the appropriate CA certificate for your MQTT broker and place it at the path expected by the project.

The certificate is not included in this repository. Update the ESP-IDF build configuration if necessary so that the certificate is embedded into the firmware.

### 4. Build and Flash

After completing the configuration, build and flash the firmware using ESP-IDF.

**Security Notice:** Do not commit Wi-Fi passwords, MQTT passwords, private keys, or other credentials to version control.
