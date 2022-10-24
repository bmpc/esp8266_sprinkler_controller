# ESP8266 sprinkler controller

A lawn irrigation controller using an [ESP8266 - WeMos D1 Mini Pro clone](https://www.wemos.cc/en/latest/d1/d1_mini_pro.html) and solenoids for each sprinkler station.

The sprinkler stations can be controlled and programed to be triggered at a certain schedule using [Home Assistant](https://www.home-assistant.io/).

The communication between the ESP8266 board and Home Assistant is done using an [MQTT](https://mqtt.org/) broker on the local WiFi network. The ESP8266 listens to events in order to turn on/off and to program the schedule for each sprinkler station.

The controller is powered by a 9V battery as this is also the voltage needed to activate the solenoids. Therefore, the efficiency of the circuit is very important. The ESP8266 needs to be in deep sleep as much as possible and only wake up for activating/deactivating sprinkler stations.

There is also an interface mode that can be activated using Home Assistant. When in this mode, the ESP8266 will not go into deep sleep and is ready to receive commands to activate/deactivate and configure the system. This mode is useful if, for some reason, there is a need to turn on spriknkler stations manually.

The controller is also equiped with a reset button which will force the system to wake if needed.

<div style="text-align:center">
  <img src="sprinkler_controller_v1.png" alt="Controller circuit" width="60%" />
</div>

## MQTT Topics

### Subscribe

| Topic                                | Payload format                                      | Payload example            | Retained |
| ------------------------------------ | --------------------------------------------------- | -------------------------- | -------- | 
| `lawn-irrigation/station{x}/set`     | `{"on","off"}\|{"duration in milliseconds"}`        | `"on\|18000" ; "off"`      | false    |
| `lawn-irrigation/station{x}/config`  | `{"cron expression"}\|{"duration in milliseconds"}` | `"0 30 6 1-31/2 * *\|900"` | true     |
| `lawn-irrigation/interface-mode/set` | `{"on","off"}`                                      | `"on" ; "off"`             | true     |
| `lawn-irrigation/enabled/set`        | `{"on","off"}`                                      | `"on" ; "off"`             | true     |


### Publish

| Topic                                   | Payload format | Payload example | Retained |
| --------------------------------------- | -------------- | --------------- | -------- | 
| `lawn-irrigation/station{x}/state`      | `{"on","off"}` | `"on" ; "off"`  | false    |
| `lawn-irrigation/interface-mode/state`  | `{"on","off"}` | `"on" ; "off"`  | false    |
| `lawn-irrigation/log`                   |  `<string>`    | `"log string"`  | true     |

## Home Assistant

This is a screenshot of my Lawnn issigation card that I have configured on my Home Assistant dashboard:

<div style="text-align:center">
  <img src="lawn_irrigation_card.png" alt="Home Assistant control card" width="60%" />
</div>


In my particular case, I have three sprinkler stations.

The esp8266 sprinkler controller is configured from [Home Assistant](https://www.home-assistant.io/) using [MQTT switchs](https://www.home-assistant.io/integrations/switch.mqtt/).

Each station is controlled by an MQTT switch. The switches automatically display the current station state and are also used to trigger the stations manually (provided that the controller is on **interface mode**). 

If a station is activated due to the configured cron expression schedule, the switch will be updated automatically with the current status as an MQTT event is sent from the controller whenever there is a station event.

Besides the station controller switches, there are two additional switches to toggle:
 - the interface mode 
 - the irrigation itself.
  
When the irrigation controller is in **interface mode**, the user can drive the stations direclty as the ESP8266 will not go into deep sleep.

The irrigation toggle swicth can be used to skip the scheduled lawn watering. For example, we can disable irrigation if rain is forecast for the next 12 hours or is has rained in the last 12h.

Appart from the switches, there are also three input text fields for each station. This is where the CRON expression and the duration for each station can be set. An automation is also required so that an MQTT event is published whenever the text field changes.

You can find an example on how to configure these MQTT switches and text fields on the HA examples included in this repository:
 - [configuration.yaml](/ha/configuration.yaml)
 - [automations.yaml](/ha/automations.yaml)

## Circuit

### Power

Because the solenoids that activate the sprinklers require 9V of power, we will power our circuit with a 9V battery (two 9V batteries in parallel to be precise). A voltage regulator is used to step down from 9v to 5v so that we can power the ESP8266 and other ICs in the circuit. The WeMos D1 mini pro actually ony requires 3.3V but it comes with it's own voltage regulator. This is not ideal, as there are two voltage regulators but in this case we require 3 different voltages and it would not be practical to have multile batteries for the same circuit.

### Latching solenoids and the L293D ICs

There are two L293D ICs in this circuit. These are Quadruple Half-H Drivers that are designed to provide bidirectional drive currents. The L293D is also designed to drive inductive loads such as solenoids and motors. These ICs allow us to drive the 9v power lines to the solenoid with a 3.3v logic signal provided by the ESP8266.

A latching solenoid uses an electrical current pulse or internal permanent magnet material to maintain a set position without the constant application of power. Latching solenoids (also known as bistable solenoids) have two standard positions; de-energized with the plunger fully extended and de-energized with the plunger held in position by permanent magnets. 

Electrical polarity is vital to proper latching a solenoid. As current flows in one direction, adds to the pull of the permanent magnet. Sending a current through the coil field in the opposite direction cancels the magnet’s attraction and releases the plunger from the latched position.

These L293D ICs are required in order to control our latching solenoids because we need to provide oposite polarities depending on if we are activating or deactivating the solenoid.

Each L293D can control two solenoids. Hence two L293D ICs were used to drive a total of 4 solenoids/sprinkler stations.

### The lack of pins and the 8-bit shift register

Because the WeMos D1 Mini Pro only provides 11 GPIO digital pins, an 8-bit shift register was included so we can have more digital lines to drive the four possible solenoids/stations.

### Power Efficiency

This is a very important topic as this circuit is powered with standard 9v batteries. Depending on the battery type, usually a 9v battery has a capacity of around 300 mAh. 
We must garantee that our circuit is as efficient as possible. Ideally, the quiescent current of the circuit should not exceed 50uA. I estimate around 300 to 400 days on double 9v batteries.

Operating normally, without any changes to improve the efficiency of the circuit, the average current is about 90mA to 130mA. This includes the ESP8266 making WiFi requests, the two L293D ICs, the 8-bit shift register, and added the voltage regulator.

If we put the ESP8266 in deep sleep mode, the current drops to about 50mA to 60mA. Still too high. The culprits are the two L293D ICs which have a very high quiescent current of about 30mA each.

Hence, to reduce the quiescent current of the L293D ICs, the power is supplied to these ICs only when there is a station that we need to turn on/off. To atchieve this, an NPN transistor is used driven by the ESP8266.

I ended up also removing the WeMos D1 Mini Pro builtin LED as it is mapped to GPIO2 and this pin is being used by our code for other purposes.

The average quiescent current of the circuit is now around 30uA.

### Schematic

<div style="text-align:center">
  <img src="circuit_schematic.png" alt="Circuit Schematic" width="100%" />
</div>

### BOM


| Part                                 | Qty |
| ------------------------------------ | --- |
| WeMos D1 Mini Pro clone (ESP8266)    | x1  |
| L293D (Quadruple Half-H Drivers)     | x2  |
| 74HC595 (8-bit shift register)       | x1  |
| MCP1702-5002E/TO (voltage regulator) | x1  |
| 1k resistor                          | x5  |
| 100 uF eletrolitic capacitor         | x3  |
| 1 uF ceramic capacitor               | x3  |
| Switch (reset)                       | x1  |
| Barrel Jack Switch                   | x1  |
| Screw terminal                       | x4  |
| 9v battery                           | x2  |
| Rainbird solenoids                   | x3  |

## Final thoughts

// TODO