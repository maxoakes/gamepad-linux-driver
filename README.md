# Linux Logitech F310 Gamepad Driver
## An out-of-tree kernel module for Linux
* Includes a regular version that is mapped to the operating-system correct controller inputs
* Also includes well as a proof-of-concept driver that binds cursor and scrollwheel movement to the joysticks, and select keyboard and mouse buttons to the gamepad buttons
    * located in gamepad_driver_mapped
    * For some reason, the mouse movement does not work in a VM for me