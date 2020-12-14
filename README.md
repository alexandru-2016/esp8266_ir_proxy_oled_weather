# esp8266_ir_proyx_oled_weather

Listen to an IR signal from a remote, and convert it to another IR signal.

I.e. control your generic IR remote speakers with a Samsung remote.

On top of this it integrates an OLED display for:
- showing the current volume while receiving commands
- after a short interval, display a "weather screen saver" that scrolls the current temperature

Set the following variable:
WIFI_SSID, WIFI_PWD - Wifi credential
OPEN_WEATHER_MAP_APP_ID - API key from openweathermap.org
OPEN_WEATHER_MAP_LOCATION_ID - Location ID from openweathermap.org
