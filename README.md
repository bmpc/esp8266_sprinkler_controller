# ESP8266 sprinkler controller

The goal of this project is to control a lawn irrigation system using an [ESP8266](https://en.wikipedia.org/wiki/NodeMCU) and a solenoid for each sprinkler station.

The ESP8266 nodemcu board connects to an [MQTT](https://mqtt.org/) broker on the local WiFi network so that it can listen for events to turn on/off each sprinkler station on the irrigation system.

A fail safe timeout is enforced by the esp8266 in the case a STOP message is not received from the MQTT broker to turn off the irrigation for a particular station. 

<div style="text-align:center">
  <img src="circuit.png" alt="ESP8266 circuit" width="60%" />
</div>

## MQTT Topics

### Subscribe

```cpp
// Topic: {x} is the station number 
"lawn-irrigation/station{x}/set"

// Payload format
{"on","off"}|{"duration in milliseconds"}

// Payload examples
"on|18000"
"off"
```

### Publish
```cpp
// Topic: {x} is the station number 
"lawn-irrigation/station{x}/state"

// Payload format
{"on","off"}
```

## Home Assistant

The esp8266 sprinkler controller can be controlled from [Home Assistant](https://www.home-assistant.io/) using an [MQTT switch](https://www.home-assistant.io/integrations/switch.mqtt/) for each station. This way, different programs for watering the lawn can be configured with **automations** that can even control the irrigation schedule based on the local rain history.

In my particular case I have three sprinkler stations. You can find an example on how to configure these MQTT switches on your HA configuration.yaml here: [configuration.yaml](/ha/configuration.yaml)

## Circuit

The circuit is very simple. Because the solenoids require a 9v power supply, we will power our circuit with a 9v battery. A voltage regulator is used to step down from 9v to 3.3v so that we can power the ESP8266. In order for the ESP8266 to control the high voltage solenoids, I used three [MOSFETs](https://en.wikipedia.org/wiki/MOSFET). For the ESP8266, I used a nodemcu v3 board as I had one of these lying around.

[I've heard](https://www.youtube.com/watch?v=IG5vw6P9iY4) that whenever switching off a solenoid, an inductive reactance (opposition to current due to inductance) can occur. To cancel this effect, protection [schottky diodes](https://en.wikipedia.org/wiki/Schottky_diode) are used to save other components of the circuit from back or forward EMF of the solenoid.

### Schematic

<div style="text-align:center">
  <img src="circuit_schematic.png" alt="schematic" width="100%" />
</div>

### BOM

```
 - ESP8266 nodemcu v3            x1
 - 1k resistor                   x3
 - SR320 (Diode)                 x3
 - IRL3705NPBF (NChannel MOSFET) x3
 - LD33V (Voltage regulator)     x1  
 - 9v battery                    x1
 - Rainbird solenoids            x3
```