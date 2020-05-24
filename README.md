## hoverboard-firmware-hack - bobby car edition

This repo contains a 4-wheel-drive-bobbycar-optimized version of the hoverboard mainboard firmware by rene-dev, crinq and NiklasFauth.

![bobbycar pic](https://raw.githubusercontent.com/larsmm/hoverboard-firmware-hack-bbcar/master/pic1.jpg)

### Features
* controlled by 2 potis on the steering wheel: 1. forward, 2. break or backward or turbo mode
* 4 driving modes with different speed, acceleration and features
* acceleration and breaking ramps, turbo ramp
* everything else is identical to: https://github.com/NiklasFauth/hoverboard-firmware-hack

### Driving modes
You can activate them by holding one or more of the potis while poweron. (km/h @ full 12s battery):
* Mode 1 - Child: left poti, max speed ~3 km/h, very slow backwards, no turbo
* Mode 2 - STVO: no poti, max speed ~<6 km/h (verify it), slow backwards, no turbo
* Mode 3 - Fun: right poti, max speed ~12 km/h, no turbo
* Mode 4 - Power: both potis, max speed ~22 km/h, with turbo ~29 km/h

After poweron it beeps the mode in 1 to 4 fast beeps. default mode is nr. 2.

### Turbo
Field weakening is only availible in mode 4. It can only be activated if you are already at 80% of top speed. To activate it, press the backwards poti to get additional 40% more power. :) Please be very careful, this speed is dangarous.

### Power and battery
Peak power is around 34A = 1800W at 12 lithium battery cells. more power does not make much sense. the wheels are not able to get the power onto the ground. cooling of the board is no problem, the wheels will get too hot earlier. you will get around 20 km out of a 8 Ah 12s battery.

### Build and flash
* install Visual Studio Code
* install PlatformIO IDE within VSCode extension system
* File --> open folder: hoverboard-firmware-hack-bbcar
* use "Build", "Upload" and "Serial Monitor" buttons

### more info
* https://larsm.org/allrad-e-bobby-car/
* https://figch.de/index.php?nav=bobbycar