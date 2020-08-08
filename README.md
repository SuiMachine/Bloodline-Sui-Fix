# Bloodline - Sui's fix
A basic fix to the god awful window handling in Bloodline (2004/2005 Czech video game), which essentially was bugging out explorer.exe. Also added added an ability to override aspect ratio for camera.

![wtf.png](/wtf.png)

Requirements
----
* [Microsoft Visual C++ Redistributable 2017 (x86)](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads)

Notes
----
* Tested only with Polish release of the game.
* Do not use "**RestrictFolderCreation**" option in combination with "**Unofficial patch**" for the game. 

Warning
----
* **Do not by any means run** this game with elevated privileges (that means **no "run as administrator"** and **no running this game in compatibility mode with Windows 95/98/Me/XP**).
* On the internet you can find an "**Unofficial patch**" for the game (do not confuse with Sui's Fix). This one especially **you should not run** with **elevated privileges**  as it creates, reads and **deletes** files in ``C:\Windows``. If you decide to use it - make sure you are running it with normal user privileges - that means **no "run as administrator"** and **compatibility mode of Windows Vista** or higher (XP, Me, 98 etc. all elevate process privileges) - this will virtualize all files that should go to ``C:\Windows`` to ``%LOCALAPPDATA%\VirtualStore\Windows\`` keeping your system safe. Also again - do not use "**RestrictFolderCreation**" with "**Unofficial patch**".