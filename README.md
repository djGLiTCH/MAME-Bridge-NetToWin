**MAME Bridge NetToWin**

This tool is designed to enable "output windows" as an additional add-on tool when you have "output network" set in MAME, as natively you can only choose "windows" or "network" in MAME settings (mame.ini).

To enable simultaneous dual output, configure MAME to output "network" (find the relevant "output" line in "mame.ini" and set this to "output network"). If you have LEDBlinky, you may need to ensure all ROMs are loaded with the command "-output network", as LEDBlinky can revert the output setting in mame.ini from "network" to "windows".

If "network" output is detected on 127.0.0.1:8000 for MAME, then this tool will interpret all state outputs received by the TCP server and relay them to windows by simulating the native MAME "output windows" method.

I did prove that simultaneous dual output was possible natively in MAME with a custom build of MAME (I called this "output netwin"), but as I am not confident in my ability to maintain this code within the main release branch of MAME, I thought this tool would work better as it can be used with all future vanilla releases of MAME without code customisation.

https://github.com/djGLiTCH/MAME-Bridge-NetToWin
