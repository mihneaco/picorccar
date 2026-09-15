# Improvements

## Switch transport to a dedicated radio board instead of Wi-Fi

Wi-Fi adds overhead (AP/STA bring-up, lwIP, CYW43 servicing) that a dedicated radio wouldn't need.

## Explore display + camera, phone control app

Add a display and a basic camera to the car; write a control app for phone as an alternative to the joystick controller.

## Explore switching to an RTOS instead of bare metal

# Bugs

## Wi-Fi connectivity drops after 5-6 m

May already be fixed by the dedicated-radio switch above.

## Wi-Fi connectivity drops after continous running for a couple of minutes