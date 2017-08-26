
# YAACA

yaaca is an application, written in Python and C, for acquisition of
images/videos with the ZWO ASI series of astronomical webcam.

It depends on Numeric Python and GTK3 gi interface available for
Python2. The packages needed on Arch Linux are:

```shell
pacman -S python-numpy python-gobject gtk3 gcc make libusb-1.0 python-cairo
```

instead on Ubuntu with python2 as defult:

```shell
apt-get install gir1.2-gtk-3.0 gcc make libusb-1.0 python-numpy python-gobject python-gobject- cairo python-gi python-gi-cairo
```

It is build around a C interface to the ZWO provided library. You can
communicate with this via JSON (the idea is to use the same on Android
Webview), see taaca_server.h for the API. The image processing engine
is written in Python, but uses NumPy for speed.

## Installing from HEAD

There are prepackaged archives available in the packages directory.

The easiest way to get latest version is to clone from github
repository and compile the C part and launch it:

```shell
git clone https://github.com/chripell/yaaca
cd yaaca
make
./yaaca.py
```

## Using yaaca

First of all, make sure that the correct udevd rules for the ASI
cameras are installed. Otherwise you won't be able to use them
as non-root user and the kernel USB buffer might be too small.

After starting yaaca, you need to open an ASI camera from the File
menu. If there aren't any listened, verify the connection, exit and
reenter the application. In the same menu you can find the option to
save and load full camera configuration. In the same menu you can
find options to snapshot what you currently see on the screen and
to run an external solver for the field (solve-field must be in
the path).

In the Camera menu you can find the command to start and stop the
acquisition. Below There is the record toggle (this can be also
activated with the r button: yaaca tries to achieve mouse-less
usability). The video stream is saved in *.ser* files, which are
supported by most astronomical software. On Linux you can use the very
good
[ser_player](https://github.com/cgarry/ser-player/blob/master/README.md)
to see them. Also you can use the supplied *sertoppm.py* to convert to
single pictures. Together with the saved file you will find also a
*.txt* file with full info from the camera. The Long Exposure toggle
selects between 2 capture mode of the camera. Quite frankly, I don't
see many differences.

Settings exposes all the controls for the current model of the
camera. The changes to text field will be committed when you press
Return, Click Apply or press Alt+A (the button accelerator). The
checkbox near the text field enables or disables auto mode. If the
setting is outside allowed range, it will be cropped into it when
committed. Radio buttons changes are applied immediately. On the
bottom you can find the Reload button that rereads the values from the
camera and Close to shut down the dialog box.

Roi/Mode is similar to the Settings dialog, but focus on image format
and offers also the possibility to change the prefix where to save the
videos. Additionally you have the "Recenter ROI" button that allows to
use the central part of the sensor for better image quality.

View menu specifies how the image will be displayed and so doesn't
change the way it is saved. Zoom specifies the zoom factor of the main
area. Below you can choose to do a fast debayer (4 pixels are blended
in 1) or full debayer. This is useful only for color cameras of
course. Next section is for enabling the cross pointer and the
histogram. The cross pointer also have a boxed areas (that can be
resized with keys b and n) that specifies which part of the picture
will be used by various operations (like SAA and histogram).

The part part below in the view menu allows you to stack multiple
images on the fly. With *SAA*, the area defined by the box is used to
calculate the offset of the captured image to the first, reference,
one. The image is then shifted and added to the reference and the
result is shown. The amount of shifting is visible in the right
pane. The dark frame is built while *Add Dark* is selected. Of course
you must cover your telescope before selecting it. The reset entries
zero the accumulated frames (the number of light/dark is available in
the right pane).

Gamma stretch applies a gamma function to enhance the visibility of
faint details. The last part of the view menu allows you to select the
view mode. It can be:

* *Show Processed*, stretching of the histogram, gamma stretch and
  other processing is visible.
  
* *Show SAA/Dark*, the accumulated image with dark subtracted is
  shown.
  
* *Show Raw*, raw image from the camera is displayed. This is useful
  for slow computers or as a sanity check about what is saved (only
  the debayering is applied, set it to *Raw* if you don't want to see
  it as well).

The application window is dived in 2 part. The main of the left shows
the image acquired (or elaborated by SAA or such) and can be zoomed
via the View menu. On the right there are various sub-windows. The top
one is a full view of the acquired image and shows which part that is
displayed in the main one (if it doesn't entirely fit). You can click
around to change the displayed area in the main window (identified by
a rectangle). Below there are some important parameter about the
capture and the keys needed to change them. o-p, k-l are for exposure
time and q-a, w-s for gain. r turns on/off recording. Below there is
the histogram of the acquired image. You can use z-x and c-v to
stretch the values that you want to display (which are shown as the
red part of the histogram). Next comes an information are with the
position of the cross pointer, the value of the pixel beneath it and
the size of the box.

Note that you can use cursor keys to send pulses to the ST4 port (and
so move the telescope if it is connected).

