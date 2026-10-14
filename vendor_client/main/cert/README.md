# Certificate Setup

This directory stores the local CA certificate required for the MQTT TLS connection.

1. Obtain the appropriate CA certificate from your MQTT broker provider.
2. Place the certificate in this directory using the expected filename, such as `ca.pem`.
3. Do not commit private credentials or certificates that you intend to keep local.
4. Verify that the ESP-IDF build configuration references the correct certificate path.

The CA certificate is not included in this repository. You can create new 'ca.pem' later.
