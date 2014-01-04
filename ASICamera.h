
#ifndef ASICAMERA_H
#define ASICAMERA_H
#define ASICAMERA_API 

enum Control_TYPE{
	CONTROL_GAIN = 0,
	CONTROL_EXPOSURE,
	CONTROL_GAMMA,
	CONTROL_WB_R,
	CONTROL_WB_B,
	CONTROL_BRIGHTNESS,
	CONTROL_BANDWIDTHOVERLOAD,
};


enum IMG_TYPE{ //Supported image type
	IMG_RAW8=0,
	IMG_RGB24,
	IMG_RAW16,
	IMG_Y8,
};

enum GuideDirections{
	guideNorth=0,
	guideSouth,
	guideEast,
	guideWest
};

enum BayerPattern{
	BayerRG=0,
	BayerBG,
	BayerGR,
	BayerGB
};

#ifndef _cplusplus
#define bool char
#define BayerPattern int
#define Control_TYPE int
#define IMG_TYPE int
#define GuideDirections int
#endif

#ifdef _cplusplus
extern "C" {
#endif

// get number of Connected ASI cameras.
int getNumberOfConnectedCameras(); 
// Open  the  camera. camIndex 0 means the first one.
bool openCamera(int camIndex);
//don't forget to closeCamera if you opened one
void closeCamera();
// init the  camera. 
bool initCamera();
//Is it a color camera?
bool isColorCam();
//get the pixel size of the camera
double getPixelSize();
// what is the bayer pattern
BayerPattern getColorBayer();
//get the camera name. camIndex 0 means the first one.
char* getCameraModel(int camIndex);


// is control supported by current camera
bool isAvailable(Control_TYPE control) ;   
// is control supported auto adjust
bool isAutoSupported(Control_TYPE control) ;		
// get control current value and auto status
int getValue(Control_TYPE control, bool *pbAuto)  ;    
// get minimal value of control
int getMin(Control_TYPE control) ;  
// get maximal  value of control
int getMax(Control_TYPE control) ;  
// set current value and auto states of control
void setValue(Control_TYPE control, int value, bool autoset); 

int getMaxWidth();  // max image width
int getMaxHeight(); // max image height
int getWidth(); // get current width
int getHeight(); // get current heigth
int getStartX(); // get ROI start X
int getStartY(); // get ROI start Y

float getSensorTemp(); //get the temp of sensor ,only ASI120 support
unsigned long getDroppedFrames(); //get Dropped frames 
bool SetMisc(bool bFlipRow, bool bFlipColumn);	//Flip x and y
void GetMisc(bool * pbFlipRow, bool * pbFlipColumn); //Get Flip setting	

//whether the camera support bin2 or bin3
bool isBinSupported(int binning); 
//whether the camera support this img_type
bool isImgTypeSupported(IMG_TYPE img_type); 
//get the current binning method
int getBin(); 

//call this function to change ROI area after setImageFormat
//return true when success false when failed
bool setStartPos(int startx, int starty); 
// set new image format - 
//ASI120's data size must be times of 1024 which means width*height%1024=0
bool setImageFormat(int width, int height,  int binning, IMG_TYPE img_type);  
//get the image type current set
IMG_TYPE getImgType(); 

//start capture image
void startCapture(); 
//stop capture image
void stopCapture();


// wait waitms capture a single frame -1 means wait forever, success return true, failed return false
bool getImageData(unsigned char* buffer, int bufSize, int waitms);

//ST4 guide support. only the module with ST4 port support this
void pulseGuide(GuideDirections direction, int timems);

#ifdef _cplusplus
}
#endif
#endif

