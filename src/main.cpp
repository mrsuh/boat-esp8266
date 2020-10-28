#include <Arduino.h>
#include <ArduinoJson.h>
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define connectionLedPin 12
#define moveLedPin 13
#define powerLedPin 14

#define driverPin 1
#define helmPin 2

#define DRIVER_MIN_PULSE_LENGTH 1000
#define DRIVER_MAX_PULSE_LENGTH 2000

bool connected = false;
Servo driver;
Servo helm;
IPAddress IP;
StaticJsonDocument<200> doc;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char *ssid = "boat-esp8266";
const char *password = "password";

int x = 0;
int y = 0;
bool moveInitialized = false;
bool moveUserActive = false;
unsigned int moveInterval = 50;
unsigned int moveTimeout = 600;
unsigned int lastMoveTime = 0;
unsigned int currentTime = 0;
unsigned int emergencyTimeout = 0;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len) {
    if (type == WS_EVT_CONNECT) {
        connected = true;
        connectionLedEnabled(true);
        lastMoveTime = 0;
        Serial.printf("Client connected [%s][%u]\n", server->url(), client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        connected = false;
        connectionLedEnabled(false);
        Serial.printf("Client disconnected [%s][%u]\n", server->url(), client->id());
    } else if (type == WS_EVT_ERROR) {
        connected = false;
        connectionLedEnabled(false);
        Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *) arg), (char *) data);
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *) arg;
        if (info->final && info->index == 0 && info->len == len) {
            data[len] = 0;

            DeserializationError error = deserializeJson(doc, (char *) data);

            if (error) {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                return;
            }

            lastMoveTime = millis();
            moveInitialized = false;

            moveInterval = doc["moveInterval"];
            if(moveInterval < 50) {
                moveInterval = 50;
            }

            moveTimeout = doc["moveTimeout"];
            if(moveTimeout < moveInterval * 2) {
                moveTimeout = moveInterval * 2;
            }

            emergencyTimeout = doc["emergencyTimeout"];
            if(emergencyTimeout <= 0) {
                emergencyTimeout = 240 * 1000;
            }
            moveUserActive = doc["active"];
            x = doc["x"];
            y = doc["y"];
        }
    }
}

const char indexhtml[]
PROGMEM = R"rawliteral(
<!DOCTYPE html>
<head>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge,chrome=1">
    <meta name="viewport" content="width=device-width, initial-scale=1 maximum-scale=1 user-scalable=no">
    <title>Boat</title>
    <style>
        html {
            margin: 0;
            padding: 0;
            height: 100%;
        }

        body {
            margin: 0;
            padding: 0;
            height: 100%;
            background-color: rgba(65, 105, 225, 0);
        }

        #info {
            margin-bottom: 1%;
        }

        #info.connected {
            background-color: rgb(65, 105, 225);
            color: white;
        }

        #info.disconnected {
            background-color: rgb(220, 20, 60);
            color: white;
        }

        .hide {
            display: none;
        }

        #settings-link {
            float: right;
            padding-right: 1%;
        }

        #control-block {
            width: 100%;
            height: 95%;
            border: 3px dashed black;
            border-radius: 10px;
            box-sizing: border-box;
            -moz-box-sizing: border-box;
            -webkit-box-sizing: border-box;
        }

        a {
            color: white;
        }

        a:visited {
            color: white;
        }

    </style>
</head>

<body>
<div id="info" class="disconnected">
    <span>[v1.1]</span>
    <span id="status">Disconnected</span>
    <span id="coords">(0/0)</span>
    <a href="javascript:toSettingsLink()" id="settings-link">settings</a>
    <input type="hidden" id="settings">
</div>
<div id="settings-block" class="hide">
    <table>
        <tr>
            <td>
                <label for="auto-move">Auto move</label>
            </td>
            <td>
                <select id="auto-move">
                    <option value="1">on</option>
                    <option value="0" selected>off</option>
                </select>
            </td>
        </tr>
        <tr>
            <td>
                <label for="speed">Speed(%)</label>
            </td>
            <td>
                <input type="number" id="speed" value="0">
            </td>
        </tr>
        <tr>
            <td>
                <label for="move-interval">Move interval(ms)</label>
            </td>
            <td>
                <input type="number" id="move-interval" value="50">
            </td>
        </tr>
        <tr>
            <td>
                <label for="move-timeout">Move timeout(ms)</label>
            </td>
            <td>
                <input type="number" id="move-timeout" value="600">
            </td>
        </tr>
        <tr>
            <td>
                <label for="emergency-timeout">Emergency timeout(ms)</label>
            </td>
            <td>
                <input type="number" id="emergency-timeout" value="240000">
            </td>
        </tr>
        <tr>
            <td>
                <label for="debug">Debug</label>
            </td>
            <td>
                <select id="debug">
                    <option value="1">on</option>
                    <option value="0" selected>off</option>
                </select>
            </td>
        </tr>
    </table>
</div>
<div id="debug-block"></div>
<div id="control-block"></div>
</body>

<script>
    let encodeQueryData = function(data) {
        const ret = [];
        for (let d in data) {
            if (!data.hasOwnProperty(d)) {
                continue;
            }

            ret.push(encodeURIComponent(d) + '=' + encodeURIComponent(data[d]));
        }
        return ret.join('&');
    };

    let updateQueryParametersFromElements = function() {
        let data = {};
        ['speed', 'move-interval', 'move-timeout', 'emergency-timeout', 'settings'].forEach(function(val) {
            data[val] = document.getElementById(val).value;
        });
        let autoMoveElement = document.getElementById('auto-move');
        data['auto-move']   = autoMoveElement.options[autoMoveElement.selectedIndex].value;

        let debugElement = document.getElementById('debug');
        data['debug']    = debugElement.options[debugElement.selectedIndex].value;

        window.history.pushState('page', 'Boat', '?' + encodeQueryData(data));
        parameters = new URLSearchParams(window.location.search);
    };

    let updateElementsFromQueryParameters = function() {
        let parameters = new URLSearchParams(window.location.search);
        ['auto-move', 'speed', 'move-interval', 'move-timeout', 'emergency-timeout', 'debug', 'settings'].forEach(
            function(val) {
                if (parameters.has(val)) {
                    document.getElementById(val).value = parameters.get(val);
                }
            });

        if (parameters.has('settings') && parseInt(parameters.get('settings')) === 1) {
            settingsBlockElement.className = '';
            settingsLinkElement.innerText  = 'hide';
        }
    };

    let attachEventListeners = function() {
        ['auto-move', 'speed', 'move-interval', 'move-timeout', 'emergency-timeout', 'debug'].forEach(function(val) {
            document.getElementById(val).addEventListener('change', function() {
                updateQueryParametersFromElements();
            });
        });

        document.getElementById('move-interval').addEventListener('change', function() {
            initInterval();
        });

        controlBlockElement.addEventListener('touchforcechange', function(e) {
            e.preventDefault();
            e.stopPropagation();
        }, {passive: false});

        controlBlockElement.addEventListener('touchstart', function(e) {
            active = true;
            e.preventDefault();
            e.stopPropagation();
            coords = mathCoords(e);
            showCoords(coords);
            drawBackground(coords);
        }, {passive: false});

        controlBlockElement.addEventListener('touchmove', function(e) {
            e.preventDefault();
            e.stopPropagation();
            active = true;
            coords = mathCoords(e);
            showCoords(coords);
            drawBackground(coords);
        }, {passive: false});

        controlBlockElement.addEventListener('touchend', function(e) {
            e.preventDefault();
            e.stopPropagation();
            active   = false;
            coords.x = 0;
            coords.y = 0;
        }, {passive: false});

        window.onerror = function(error, url, line) {
            debug(JSON.stringify({error: error, url: url, line: line}));
        };
    };

    let debug = function(message) {
        if (parseInt(parameters.get('debug')) === 0) {
            return;
        }

        let block       = document.createElement('div');
        block.innerText = Date.now() + ' ' + message;
        debugBlockElement.appendChild(block);

        while (debugBlockElement.children.length > 10) {
            debugBlockElement.removeChild(debugBlockElement.firstChild);
        }
    };

    let connect = function() {
        ws           = new WebSocket('ws://192.168.4.1/ws');
        ws.onopen    = function(e) {
            showStatus(true);
            debug('WS Connected');
        };
        ws.onclose   = function(e) {
            showStatus(false);
            debug('WS Closed' + JSON.stringify(e));
        };
        ws.onmessage = function(e) {
            debug('WS Message' + e.data);
        };
        ws.onerror   = function(e) {
            showStatus(false);
            debug('WS Error' + JSON.stringify(e));
        };
    };

    let showStatus = function(connected) {
        if (connected) {
            statusElement.innerText = 'Connected';
            infoElement.className   = 'connected';
        } else {
            statusElement.innerText = 'Disconnected';
            infoElement.className   = 'disconnected';
        }
    };

    let showCoords = function({x, y}) {
        coordsElement.innerText = '(' + x + '/' + y + ')';
    };

    let move = function({x, y}) {
        if (ws && ws.readyState === WebSocket.OPEN) {

            debug('WS Send ' + JSON.stringify({x: x, y: y}));

            let data = {
                x:                x,
                y:                y,
                moveInterval:     parseInt(parameters.get('move-interval')),
                moveTimeout:      parseInt(parameters.get('move-timeout')),
                emergencyTimeout: parseInt(parameters.get('emergency-timeout')),
                active:           active,
            };

            ws.send(JSON.stringify(data));
        }
    };

    let mathCoords = function(event) {
        let touchobj = event.targetTouches[event.targetTouches.length - 1];

        let touchX = touchobj.clientX;
        let touchY = touchobj.clientY;

        let windowWidthHalf = window.innerWidth / 2;
        let windowHeight    = window.innerHeight;

        let x = parseInt((touchX > windowWidthHalf ? touchX - windowWidthHalf : (windowWidthHalf - touchX) * -1) * 100 /
            windowWidthHalf);
        let y = parseInt((windowHeight - touchY) * 100 / windowHeight);

        if (parseInt(parameters.get('auto-move')) === 1 && active) {
            y = parseInt(parameters.get('speed'));
        }

        return {x: x, y: y};
    };

    let drawBackground = function({x, y}) {

        let absX     = Math.abs(x);
        let absY     = Math.abs(y);
        let maxValue = absX > absY ? absX : absY;

        controlBlockElement.style.backgroundColor = 'rgba(65, 105, 225,' + (maxValue / 100).toFixed(2) + ')';

        if (maxValue === 0) {
            controlBlockElement.style.transition = 'background-color 300ms linear';
        } else {
            controlBlockElement.style.transition = '';
        }
    };

    let initInterval = function() {
        if (interval) {
            clearInterval(interval);
        }

        let moveInterval = parseInt(parameters.get('move-interval'));
        interval         = setInterval(function() {

            showCoords(coords);
            drawBackground(coords);
            move({x: coords.x, y: coords.y, active: active});
        }, moveInterval);
    };

    let toSettingsLink = function() {
        let parameters = new URLSearchParams(window.location.search);

        if (parseInt(parameters.get('settings')) === 1) {
            parameters.set('settings', '0');
        } else {
            parameters.set('settings', '1');
        }

        window.location.search = '?' + parameters.toString();
    };

    let parameters = new URLSearchParams(window.location.search);
    let active     = false;
    let interval   = null;
    let coords     = {x: 0, y: 0};

    let controlBlockElement  = document.getElementById('control-block');
    let coordsElement        = document.getElementById('coords');
    let infoElement          = document.getElementById('info');
    let statusElement        = document.getElementById('status');
    let debugBlockElement    = document.getElementById('debug-block');
    let settingsBlockElement = document.getElementById('settings-block');
    let settingsLinkElement  = document.getElementById('settings-link');

    updateElementsFromQueryParameters();
    updateQueryParametersFromElements();
    attachEventListeners();
    connect();
    initInterval();

</script>
)rawliteral";

void setup() {
    Serial.begin(115200);

    IPAddress ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 254);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(ip, gateway, subnet);

    WiFi.softAP(ssid, password);

    IP = WiFi.softAPIP();

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", indexhtml);
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    server.begin();

    pinMode(powerLedPin, OUTPUT);
    pinMode(connectionLedPin, OUTPUT);
    pinMode(moveLedPin, OUTPUT);

    helm.attach(helmPin);
    helm.write(70);

    driver.attach(driverPin);
    driver.writeMicroseconds(DRIVER_MIN_PULSE_LENGTH);

    digitalWrite(powerLedPin, HIGH);
    digitalWrite(connectionLedPin, LOW);
    digitalWrite(moveLedPin, LOW);
}

void moveLedEnabled(bool enabled) {
    if (enabled) {
        digitalWrite(moveLedPin, HIGH);
    } else {
        digitalWrite(moveLedPin, LOW);
    }
}

void connectionLedEnabled(bool enabled) {
    if (enabled) {
        digitalWrite(connectionLedPin, HIGH);
    } else {
        digitalWrite(connectionLedPin, LOW);
    }
}

void driverControl(int value) {

    if (value < 20) {
        driver.writeMicroseconds(DRIVER_MIN_PULSE_LENGTH);

        return;
    }

    if(value > 70) {
        driver.writeMicroseconds(DRIVER_MAX_PULSE_LENGTH);

        return;
    }

    driver.writeMicroseconds((int)map(value, 20, 70, (DRIVER_MIN_PULSE_LENGTH + 80), DRIVER_MAX_PULSE_LENGTH));
}


void helmControl(int value) {
    if(value >= 0) {
        helm.write((int)map(value, 0, 100, 85, 140));
    } else {
        helm.write((int)map(value, -100, 0, 1, 85));
    }
}

void loop() {

  if (lastMoveTime == 0) {
       driverControl(0);
       helmControl(0);
       return;
   }

   currentTime = millis();

  if(currentTime - lastMoveTime > emergencyTimeout) {
        moveLedEnabled(false);
        moveInitialized = false;
        driverControl(25);

        return;
    }

    if (!connected) {
        return;
    }

    if (currentTime - lastMoveTime < moveInterval && moveInitialized) {
        return;
    }

    if (currentTime - lastMoveTime > moveTimeout) {
        moveLedEnabled(false);
        moveInitialized = false;
        driverControl(0);
        helmControl(0);

        return;
    }

    helmControl(x);
    driverControl(y);
    moveInitialized = true;
    moveLedEnabled(moveUserActive);
}