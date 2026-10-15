# Bloodline - Sui's fix
A basic fix to the god awful window handling in Bloodline (2004/2005 Czech video game), which essentially was bugging out explorer.exe. I have also added an ability to override aspect ratio for camera.

![wtf.png](/wtf.png)

Eventually with help of GLM 5.3, I have added a part of hooks for ini initialization, that was responsible for creating garbage folders all over your drive due to incorrect null string termination and asset path caching - I have verified the generated code to the best of my ability.

Requirements
----
* [Microsoft Visual C++ Redistributable 2017 (x86)](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads)

Notes
----
* Tested only with Polish release of the game.
