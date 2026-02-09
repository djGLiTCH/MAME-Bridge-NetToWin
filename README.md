**MAME Bridge NetToWin**

Created by DJ GLiTCH

This tool is designed to enable "output windows" as an additional add-on tool when you have "output network" set in MAME, as natively you can only choose "windows" or "network" in MAME settings (mame.ini).

To enable simultaneous dual output, configure MAME to output "network" (find the relevant "output" line in "mame.ini" and set this to "output network").

If "network" output is detected on 127.0.0.1:8000 for MAME, then this tool will interpret all state outputs received by the TCP server and relay them to windows by simulating the native MAME "output windows" method, which is used by tools like LEDBlinky or MameHooker.

I have previously proven that simultaneous dual output is possible natively in MAME with a custom build of MAME (I called this "output netwin"), but as I am not confident in my ability to maintain this code within the main release branch of MAME, I thought this tool would work better as it can be used with all future vanilla releases of MAME without code customisation.

The original source code and latest release / version can be found at:
https://github.com/djGLiTCH/MAME-Bridge-NetToWin

If you are using LEDBlinky (v8.2.2 or lower), then you will need to ensure all ROMs are loaded with the command "-output network" when launching ROMs with MAME, as LEDBlinky can revert the output setting in mame.ini from "network" to "windows" despite making mame.ini read-only. This may not be an issue in newer version of LEDBlinky if they make use of network output functionality in MAME, but while it relies on windows output this will remain the case.

NOTE: I would recommend using this tool with Hook Of The Reaper over MameHooker, as that is what I have tested with. Hook Of The Reaper (https://github.com/6Bolt/Hook-Of-The-Reaper) uses "network" output, which is why LEDBlinky can access the "windows" output created by this tool.
