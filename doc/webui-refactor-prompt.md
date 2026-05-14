# WebUI Refactor

- The goal is to create a nicer, more like perinet.io firmware looking webui
- colors should stay the same: white, gray and orange

## Structure

- The webui keeps its tabs, but the tab menu should move and become a sidebar on the left side of the screen
- The remaining webui should be split into 4 sections, listed from top to bottom in screen space
    1. Topmost section: firmware logo
    2. Firmware Name (for home screen) or tab name for any other screen
    3. Main screen content
    4. Footer containing useful hints (leave empty for now)
- Each screen can modify the content of these sections, so we could add a new screen "TEST" that fills all 4 sections with different things than other screens

## Screens

### Home Screen
- Display a small dashboard of the sensor value

### Information Screen
- Displays device information similar to how it is displayed currently
- Displays the partition information as well

### Update Screen
- Only allows the user to upload a firmware file in order to update the IoT node
- Upload progress should be displayed (should be displayed as well via custom alert like popup)
- Update success / failure should be displayed as well via custom alert like popup

### Configuration Screen
- Contains all configuration options as subsections
- First subsection is WiFi where currently configured SSID and Passwort are displayed, a save button and a reset button
- Bottom most subsection contains a reset button to reset ALL configuration data to its default values and a reboot button that simply reboots the iot device.
- It should be very very easy to add additional configuration section later on, like mqtt or event configuration or whatever. Like really easy, just copy paste an additional section, change element types and target endpoints in the HTML / JS.