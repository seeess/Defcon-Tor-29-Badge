# Defcon 29 Tor Badge

![Tor Badge Picture](https://i.imgur.com/zQhVDHG.jpg)

## Quick Overview

This badge sold at the "Hacker Warehouse" vendor booth during Defcon 29.

This electronic badge acts as a mini lie detector. It has two primary sensors, a [GSR sensor](https://en.wikipedia.org/wiki/Electrodermal_activity) and a [heart rate sensor](https://www.rohm.com/electronics-basics/sensor/pulse-sensor). These are two of the sensors used in a polygraph machine, GSR is also used in an [e-meter](https://en.wikipedia.org/wiki/E-meter). The output of these sensors with some statistics are graphed to each of the two 1.3" OLED screens.  
Use it to interrogate your friends, or to practice how to get through your next fed-job interview.

Be gentle with the finger cuff cable's JST 2.0 connector. When disconnecting the cable pull on the white plastic connector, not the cables. I also recommend holding the board-side JST connector when connecting and disconnecting. And I would not wrap the cable around the badge tightly, it could damage the thin wires. If your cable breaks you can strip and re-crimp a JST connector, find me at defcon and I can re-crimp it for you. If it breaks after defcon and you don't want to re-crimp the cable, you can purchase a [Grove GSR sensor kit](https://www.seeedstudio.com/Grove-GSR-sensor-p-1614.html) which includes this finger cuff cable. 

I recommend you tie a knot in the lanyard behind your neck to adjust the length of this or other badges you will be wearing to prevent them from banging into each other.

---
Full video overview on youtube: https://www.youtube.com/watch?v=aaRbma9SlGY

---

## How To Use It

### Heart Rate Sensor

The reverse-mounted green LED shines light into your finger, that is detected by the sensor just under the green LED. The coarse sensitivity of this sensor is automatically adjusted by the hardware. This means that before your finger is placed on the sensor it can detect your hand moving up and down a few inches above the badge. **But it takes ~7 seconds to provide a valid reading after placing your finger on the sensor.**

Here are some tips for an accurate reading:

* Wait ~7 seconds after placing your finger on the sensor. The displayed reading should move from max/min to somewhere in the middle of the screen after this initial period.
* Don't press too hard, if the peaks of the output are very small, you're pressing too hard. Pulses can't be detected if the peaks are not large enough.
* Conversely, if the output is reaching the peak of the display and clipping, apply slightly more pressure.
* I suggest placing the middle of the pad of your index finger centered over the light sensor (not the LED). But any part of your body with a high capillary count should work (ear lobes, nose, others...).
* If you have problems try to block other light from reaching the light sensor. Use a fatter finger, and place your finger flat against the board, to cover the light sensor completely (not the LED).
* 5 peaks must be detected before a BPM can be calculated. The xiao's blue LED will blink, and the heart icon will animate when valid peaks are detected.
* Delta-BPM is calculated by finding the difference from the current BPM reading, and the BPM reading 10 peaks ago. You must have 10 continuous pulses after the BPM is computed before the delta-BPM can be calculated (~15 total peaks).

Initially you may have trouble getting valid output. But after you get the hang of how much pressure you need to apply, and wait the initial ~7 seconds, it should be easy to get a valid reading. Most problems relate to applying too much pressure, you only need to "rest" your finger on the sensor, don't apply heavy pressure. 

### GSR Sensor

The GSR sensor measures resistance of electricity traveling between the two finger cuff contacts. As your body reacts to nervousness or stress it releases more sweat, which changes the resistance of your body between the contacts, and the GSR reading will drop. 

Here are some tips for an accurate reading:

* Place the finger cuffs on your index and ring finger of the same hand. The sensors work best when the contacts are in the middle of the pad of your fingers. 
* Do not change the amount of pressure on the contacts (like touching a table or the badge with those fingers). What works the best is letting your fingers dangle while keeping them still.
* Do not touch the board besides the heart rate sensor (which is isolated with a clear sticker). Touching the board's traces can throw off the GSR reading.
* After placing the finger cuffs on your fingers, adjust the potentiometer so the GSR reading is around 512. Using the calibration mode is not required, you can do this while in the normal "Lie Detector" mode. If you’re having trouble getting a valid reading, you probably don’t want your potentiometer set to the max or min values if you’re not noticing the GSR value isn’t sensitive enough.
* To trigger the GSR sensor you can:
    * Sharply inhale and sharply exhale. It works best when you make yourself nervous or scared while doing this (create that inner tingling feeling), instead of simply breathing
    * Flex your butthole or kegel muscles for a few seconds, then release
    * Have someone pinch you, or generally piss you off
* After triggering the sensor it takes ~30 seconds for the GSR reading to return to the previous value. Triggering the GSR sensor a few times in a row can work, but at a certain point your body won't output more sweat. 
* The delta-GSR value is calculated from the difference in the current GSR reading, and the GSR reading 30 samples ago (roughly half the width of the screen). You're looking for delta-GSR values under -15ish to consider the sensor "triggered". But it is pretty easy to just watch the graph.
* The GSR graph's pixels equal 1 value. As the value goes up and down and the GSR reading nears the edge of the screen, the graph will "shift" up or down. This allows the badge to have a "zoomed in" view of the GSR reading, while also tracking the more drastic changes to the GSR reading.
* The ambient temperature can affect the GSR reading, because the temperature affects how you sweat. If you are in too hot or cold of a location the GSR readings may not react well.
* If you don't think your finger cuffs are working, you can touch the metal contacts together on the finger cuffs, and the GSR reading should drop drastically.

Remember you should wait ~30 seconds after triggering the GSR sensor for the reading to return to the previous value before attempting to trigger it again. This is also why polygraph examiners wait between asking you questions. 

---

## Cheat Modes

Once you enable "cheat modes" in the option menu, you can use two different cheats in the normal "Lie Detector" mode.

### Cheat 1: Trigger A "Fake Lie"

Pressing the right button in the "Lie Detector" mode will cause the GSR reading to artificially drop by ~30, which looks similar to when someone actually triggers the GSR sensor. After it drops the reading will slowly move back toward the actual current GSR reading so there is not a sudden displayed jump back to the actual GSR reading. 

When this cheat is active the bottom right pixel of the right screen is lit (near the heart icon). This pixel and cheat stays enabled until the displayed value returns to the actual GSR reading. Only one cheat can be active at a time.

### Cheat 2: Prevent A Lie

Pressing and holding the up button in the "Lie Detector" mode will prevent the GSR reading from dropping too quickly, which will hide any sudden drops in the displayed GSR reading. If you press and hold the up button and the actual GSR reading drops quickly, the displayed GSR will instead slowly fall (delta-GSR should stay above -12 or so).

When this cheat is active the bottom right pixel of the left screen is lit (to the right of the delta-GSR display). Releasing the up button will exit this cheat mode, but only after the displayed GSR value reaches the actual GSR value. This prevents a sudden jump back to the actual GSR reading. Only one cheat can be active at a time. 

---

## Option Modes

**Screen brightness** can be changed between high, medium, and low.

**Sleep mode** will power-down the screens cause the MCU to go into low power mode. To exit sleep mode hold the up button. But I'm sure you forgot that, so turning the badge off and on also exits sleep mode. I added this mode in the prototype phase when we didn't have a power switch, it is now mostly useless unless your power switch is covered by a SAO or it is broken off.

**Calibrate GSR** shows instructions on how to calibrate the GSR sensor. This can also be done in the regular "Lie Detector" mode instead. Values don't have to be exactly 512, but they shouldn't be too far away either. Sensitivity is lost on the extreme ends of the GSR values. 

**Cheat Modes** enables and disables the two available cheat modes in the "Lie Detector" mode. 

---

## Bling Modes

**Fake Pulse** shows a fake heart rate on both screens. It blinks the green heart rate LED and the blue xiao LED during each pulse. Pressing Up or Down toggles the LEDs if you don't want them to blink.

**Logo Scroll** shows the Tor logo. The up and down buttons change the speed of the scrolling (including the ability to freeze it in position). Pressing the right button inverts the colors. 

**Bad Defcon Advice** shows 46 different sarcastic advice for defcon that are not great ideas. These were mostly stolen from twitter [#baddefconadvice](https://twitter.com/search?q=%23baddefconadvice). These auto increment every 60 seconds or so. The right and up button moves to the next advice, the down button goes back one. 

**Name Scroll** allows you to enter your nickname and have it scroll across the badge. You can use up to 16 characters, of nearly any printable ascii character. This is saved to flash after you enter it. If your nickname isn't a full 16 characters, press and hold the right button after you enter the last character, then the entered string will begin to scroll. The up and down buttons change the scroll speed (including the ability to freeze it in position), and the right button inverts the colors (only after all of the characters have been written to the screen).

---

## Hardware

It is run off a SAMD21 cortex M0+ [arduino based xiao](https://www.seeedstudio.com/Seeeduino-XIAO-Arduino-Microcontroller-SAMD21-Cortex-M0+-p-4426.html), that you can connect to via USB-C. It is run off two AA batteries boosted to 3.3 volts. The xiao has 4 onboard LEDs, and two pads to jump to get back into [bootloader mode](https://wiki.seeedstudio.com/Seeeduino-XIAO/#enter-bootloader-mode) if something goes really wrong (only jump the RST pins if you plan on re-flashing the xiao). 

![hardwaredoc](https://i.imgur.com/MNn30Hw.png)
*The OLED covers with a green tab have not been removed in the above picture*

There is a small on/off switch between the finger cuff JST connector and the SAO header. If this breaks off you can jump the R24 pads to bypass the switch. 

The heart rate sensor has a clear vinyl sticker over the reverse mount LED hole and the light sensor. This helps protect the sensor, and provides a more reliable reading by electrically isolating the sensor's contacts. If this gets dirty or comes off, you can replace it with clear tape.

The two 1.3" i2c blue OLEDs are on the same bus, and are larger (and more expensive) than the usual 0.96" size. On the back of the OLEDs there is a 4.7kohm resistor that selects the i2c address each OLED uses. The left OLED uses 0x78, and the right OLED uses 0x7A. If you need to replace an OLED I suggest buying the type that has this address selection resistor on the back.  
The OLEDs are held in place by two 10mm M3 screws, spacers, and nuts. The top holes are not used because the pins secure the top well enough, and some are blocked by the battery cover.

If the potentiometer looks like it was cut down with a dremel, that is because it was. We avoided most pandemic part shortages except for the potentiometer, and had to buy a crazy long one. It is easy to manipulate and has hard stops, versus a flat surface mount one that requires a jewelers screw driver. Plus it can help protect the screens against impact from other badges. If this breaks off you can solder a 100kohm resistor to R13, you will lose GSR adjustability bit it should at least work.

There is one keyed [1.69bis SAO header](https://hackaday.com/2019/03/20/introducing-the-shitty-add-on-v1-69bis-standard/). Only the 3v3 and ground pins are connected to the SAO header, but if you bridge the pads above and below the header it will connect the i2c pins. These pins are not connected by default to prevent any interference from shitty-SAOs on the i2c bus that is used to drive both screens. 

All pins of the xiao are used. But pin A1 is used to enable and disable the 3v3 boost regulator, and is not strictly necessary. You can repurpose this pin by disconnecting R23 on the back of the board.

There are two small 0.1" proto areas, one has a 3v3 and ground pin. 

---

## In The Box

* Electronic badge
* Custom lanyard
* Finger cuff cable
* Two sets of AA batteries
* Stickers
* Googly eyes

I should have a few sets of pin headers if you want to solder them on. But be careful with other badges making contact with the unshielded pins. 

---

## Powering The Badge

Normally the badge will be powered by two AA batteries that are vreg boosted to 3.3 volts. However the xiao also has its own onboard 5v -> 3.3v regulator ([XC6206P332MR](https://www.digikey.com/en/products/detail/torex-semiconductor-ltd/XC6206P332MR-G/7386004)) which is used when connected via USB-C. You can power the badge directly over USB-C, without any batteries. 

If both 5v (USB) and 3.3v (battery) power is applied, the badge will detect this (while in the menu system), and it will disable the battery's 3.3v boost regulator in an attempt to save battery power. If the 3.3v boost regulator is disabled the xiao's onboard orange LED will be lit, otherwise the 3.3v boost regulator for the batteries is enabled.

If you have a problem with the board's 3.3 boost regulator, you could apply ~3.6-6v to the 5v pin of the xiao to use its vreg instead. 

If the screen freezes and the buttons don't do anything, your AA batteries are likely dead. Replace them or try powering the badge off USB-C.

---

## Usefulness of Polygraph Machines

I'm not the authority on polygraph machines, but the determination of if a subject is lying or not is up to the examiner and it is subjective. The examiner generally tries to convince the subject that the polygraph can't be defeated, which is not the case. 

In 1988 the [Employee Polygraph Protection Act](https://en.wikipedia.org/wiki/Employee_Polygraph_Protection_Act) was passed that mostly prevents you from having to go through a polygraph for a job (except for gov employees). "Probable lie" questions are asked to establish a baseline are biased against the innocent. Polygraphs are generally not admissible in court.

The point of this badge was not to be able to accurately output "truth" or "lie" when a subject is connected (which isn't possible with any polygraph machine). Instead the point was to expose more people to how they work, and allow people to practice GSR polygraph countermeasures (physical, mental, chemical, and behavioral). Real polygraph machines include additional sensors like a breathing sensor, and blood pressure. 

---

## Connecting To A Computer

The stock firmware outputs the raw data over serial USB (115200/8/n/1) to allow further analysis and visualization of the data on a computer. 

To easily see this data you can install the arduino IDE. 

1. Under Tools -> Port, select the right COM port (You may have to first install the xiao board package, see the "[How To Flash](https://github.com/seeess/Defcon-Tor-29-Badge#how-to-flash-the-badge)" section)
2. Under Tools -> Serial Monitor you can see the raw text
    
    Thresh:619 HB:462 BPM:61 GSR:555  
    Thresh:616 HB:480 BPM:61 GSR:555  
    Thresh:615 HB:508 BPM:61 GSR:555  
    Thresh:614 HB:549 BPM:61 GSR:555  
    Thresh:615 HB:614 BPM:61 GSR:554  

3. To visualize the data select Tools -> Serial Plotter. The serial data is formatted specifically for the arduino serial plotter so it can graph each variable separately. 

![Serial Plotter](https://imgur.com/CgL80pl.jpg)

The "Thresh" value is something not available to graph on the badge's OLEDs directly. This threshold is used to determine when peaks are detected in the heartbeat graph. You can comment out the threshold, or any other variable out of the code if you want a cleaner visualization. 

---

## Flash Storage

After you enter your name in the "Name Scroll" mode, it will be saved to flash. This will be loaded next time you enter "Name Scroll" mode, and works through power cycles. If something strange happens the best way to clear this flash bank is to reflash the badge. 

---

## How To Flash The Badge

1. Download the arduino IDE
2. Install the [board package for the seeeduino](https://wiki.seeedstudio.com/Seeeduino-XIAO/#software) and select the board.
3. Tools -> Port to select the correct COM port
4. Tools -> Manage Libraries and install the three necessary libraries (you don't have to grab them from github, it is easier to use the built in "manage libraries" feature of arduino's IDE)
    * https://github.com/bitbank2/ss_oled
    ![ss_oled](https://i.imgur.com/VzN2rGQ.png)
    * https://github.com/adafruit/Adafruit_SleepyDog
    ![Adafruit SleepyDog Library](https://i.imgur.com/RpNBQEY.png)
    * https://github.com/khoih-prog/FlashStorage_SAMD
    ![FlashStorage_SAMD](https://i.imgur.com/E3zojXP.png)
5. Compile and upload the code

---

## Bill Of Materials

TBD

---

## Badge Challenge

Gigs created a badge challenge that had a prize of one of the three prototype purple badges, and an extra white Tor Badge. Start with the lanyard. He has walkthrough of the challenge on [his site](https://gigsatdc.com/dc29/torbadge_walkthrough.php).

Additionally there was a second hardware related mini-challenge Gigs added to the silkscreen of the back of the badge under the battery holder. If you removed the battery holder you'd see "First person to message @see_ess on twitter with the text "no step on snek" will get a prize". But no one seemed to find this message, so the prize of a defcon 27 Tor Badge/SAO goes unclaimed. 

*All profits from the sale of this badge go directly to the Tor project, just like 2 years ago. I am not funded by anyone, I front all costs of development myself. I do not charge for my time or effort, I only hope to recoup any material costs.
If you choose to purchase a badge, thanks for supporting Tor.*





