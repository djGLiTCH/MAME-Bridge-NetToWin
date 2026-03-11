**MAME Bridge NetToWin**

Created by DJ GLiTCH

---

This tool is designed to enable simultaneous output of network and windows in MAME without modifying the core MAME source code.

To achieve this, the MAME Bridge NetToWin tool will handle "output windows" as an additional add-on when you have "output network" set in MAME. The reason this tool exists is because natively you can only choose "windows" or "network" in MAME settings (mame.ini), not both at once.

To enable simultaneous dual output, configure MAME to output "network" (find the relevant "output" line in "mame.ini" and set this to "output network").

If "network" output is detected on 127.0.0.1:8000 for MAME, then this tool will interpret all state outputs received by the TCP server and relay them to windows by simulating the native MAME "output windows" method, which is used by tools like LEDBlinky or MameHooker.

I have previously proven that simultaneous dual output is possible natively in MAME with a custom build of MAME (I called this "output netwin"), but as I am not confident in my ability to maintain this code within the main release branch of MAME, I thought this tool would work better as it can be used with all future vanilla releases of MAME without code customisation.

MAME Bridge NetToWin (Download Link / GitHub):

https://github.com/djGLiTCH/MAME-Bridge-NetToWin

---

LEDBlinky (https://www.ledblinky.net/) Specific Notes:

If you are using LEDBlinky (v8.2.2 or lower), then you will need to ensure all ROMs are loaded with the command "-output network" when launching ROMs with MAME, as LEDBlinky can revert the output setting in mame.ini from "network" to "windows" (even if mame.ini is set to read-only).

This may not be an issue in newer version of LEDBlinky if they make use of network output functionality in MAME, but while it relies on windows output this will remain the case.

---

Launchbox Launch Parameter Settings:

The steps below will add network output to MAME when launching ROMs through Launchbox. This will override whatever output setting is used in your mame.ini file (which is needed since LEDBlinky can change the mame.ini setting to "output windows", but we need "output network" for HOTR, and MAME Bridge NetToWin will relay the messages from network to windows so LEDBlinky can work as intended in parallel).

1. Open Launchbox
2. Select Tools > Manage > Emulators
3. Select MAME (it will highlight once selected), then select "Edit..."
4. Select Details in the left pane, then in the "Default Command-Line Parameters", you will want to add "-output network" (without quotations), then select "OK"
5. Close the Emulator window
6. Restart Launchbox (not needed, but good practice)

---

General Notes:

I would recommend using this tool with Hook Of The Reaper over MameHooker, as that is what I have tested with. Hook Of The Reaper (https://github.com/6Bolt/Hook-Of-The-Reaper) uses "network" output, which is why LEDBlinky can access the "windows" output created by this tool, and both can work together without any communication clashes.

Feel free to experiment with other combinations, but please know I may not be able to assist with any issues you may encounter.