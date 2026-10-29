# Beam DMX Channel Reference

## Proton 54-led beam

| Channel | Function       | Value Range | Explanation                                  |
|---------|----------------|-------------|----------------------------------------------|
|    1    | Master dimmer  |     0-255   | Overall brightness control.                  |
|    2    | Red dimmer     |     0-255   | Red color brightness.                        |
|    3    | Green dimmer   |     0-255   | Green color brightness.                      |
|    4    | Blue dimmer    |     0-255   | Blue color brightness.                       |
|    5    | Strobe effect  |     0-3     | Strobe disabled.                             |
|    5    | Strobe effect  |     4-255   | Strobe effect enabled.                       |
|    6    | Automatic mode |     0-49    | No effect; channels 1-7 stay in manual mode. |
|    6    | Automatic mode |    50-99    | Step-change color mode.                      |
|    6    | Automatic mode |   100-149   | Smooth color fade mode.                      |
|    6    | Automatic mode |   150-199   | Pulse-change mode.                           |
|    6    | Automatic mode |   200-255   | Sound-activated mode.                        |
|    7    | Master control |     0-255   | Controls the fixture's custom channels.      |

* Color calculation:
```
    R = Red dimmer * Master dimmer / 255
    G = Green dimmer * Master dimmer / 255
    B = Blue dimmer * Master dimmer / 255
```
