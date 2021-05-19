#include "stdio.h"
#include "opencv/highgui.h"
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <string>
#include <iostream>
#include <cstdio>
#include <tr1/memory>
#include <ctime>
#include <stdlib.h>
#include <signal.h>
#include <fstream>

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

/*-------------------------------------------------------------------------------------------------------
 
 * capture_rpi.cpp is based on Thomas Jacquin's capture.cpp from his Allsky project to take sky pictures with a Rapberry Pi and ZWO camera.
 * 
 * This version is for a rapberry pi and raspberry pi camera.  For simplicity, interaction with the raspberry pi is through the command "raspistill".
 * 
 * Due to the differences between the ZWO and Raspberry Pi cameras, some functionality if missing or different.
 * 
 * Max Exposure as an input parameter doesn't have a Raspberry Pi camera equivilent
 * Max Gain as an input parameter doesn't have a Raspberry Pi camera equivilent
 * Gamma is translated to contrast fo rthe raspistill command
 * Binning is not supported by the raspberry pi camera
 * Image Type is not supported by the raspberry pi camera
 * USB bandwidth is not supported by the raspberry pi camera as communication is not via USB
 * Gain is translated to be ISO  (100,400, 800) on the raspberry pi camera.
 * rasberry pi cameras do not report back a sensor temperature
 * 
 * As the raspberry pi camera is less sensitive, the exposure time (10 sec for v2 and 6 sec for v1), brightness and contrast values should be high.  In addition the image stretching in saveImageNight.sh is required.
 * 
 * 
 * TO DO:
 * changing the camera settings using the allsky-portal while a raspistill command is executing can cause a kernel panic
 * more testing
 * 
*/

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

IplImage *pRgb = 0;
//cv::Mat pRgb;
char nameCnt[128];
char const * fileName="image.jpg";
int quality[3] = {CV_IMWRITE_PNG_COMPRESSION, 200, 0};
bool bMain=true, bDisplay=false;
std::string dayOrNight;

bool bSaveRun = false, bSavingImg = false;
pthread_mutex_t mtx_SaveImg;
pthread_cond_t cond_SatrtSave;

class RPI_CAMERA_INFO {
	public:

		std::string Name;
		int MaxWidth;
		int MaxHeight;
};

class RPI_IMAGE_PARAMETERS {
	public:
		int width=0;
		int height=0;
		int bin=1;
		int Image_type=1;
		int rpiBandwidth=40;
		int rpiExposure=5000000;
		int rpiMaxExposure=10000;
		int rpiAutoExposure=0;
		int rpiGain=150;
		int rpiMaxGain=200;
		int rpiAutoGain=0;
		int delay=10; // Delay in milliseconds. Default is 10ms
		int daytimeDelay=5000; // Delay in milliseconds. Default is 5000ms
		int rpiWBR=65;
		int rpiWBB=85;
		int rpiGamma=50;
		int rpiBrightness=50;
		int rpiFlip=0;
		char const * latitude="60.7N";	//GPS Coordinates of Whitehorse, Yukon where the code was created
		char const * longitude="135.05W";
		int preview=0;
		int time=1;
		int darkframe=0;
		int daytimeCapture=1;
		int help=0;
		std::string rpiFileName;

};
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

void cvText(IplImage* img, const char* text, int x, int y, double fontsize, int linewidth, int linetype, int fontname, int fontcolor[], int imgtype)
{
	cv::Mat mat_img(img );
//	if (imgtype == ASI_IMG_RAW16) {
//		cv::putText(mat_img, text, cvPoint(x, y), fontname, fontsize, cvScalar(fontcolor[0],fontcolor[1],fontcolor[2]), linewidth, linetype);
//	} else {
		cv::putText(mat_img, text, cvPoint(x, y), fontname, fontsize, cvScalar(fontcolor[0],fontcolor[1],fontcolor[2], 255), linewidth, linetype);
//	}
}

char* getTime(){
	static int seconds_last = 99;
	static char TimeString[128];
	timeval curTime;
	gettimeofday(&curTime, NULL);
	if (seconds_last == curTime.tv_sec)
    return 0;

	seconds_last = curTime.tv_sec;
	strftime(TimeString, 80, "%Y%m%d %H:%M:%S", localtime(&curTime.tv_sec));
    return TimeString;
}

std::string exec(std::string cmd) {
    std::tr1::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while (!feof(pipe.get())) {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}

void* Display(void* params)
{

	IplImage *pImg = (IplImage *)params;
	cvNamedWindow("video", 1);
	while(bDisplay)
	{
		cvShowImage("video", pImg);
		cvWaitKey(100);
	}
	cvDestroyWindow("video");
	printf("Display thread over\n");
	return (void*)0;

    return EXIT_SUCCESS;
}

void* SaveImgThd(void * para)
{
	while(bSaveRun)
	{
		pthread_mutex_lock(&mtx_SaveImg);
		pthread_cond_wait(&cond_SatrtSave,&mtx_SaveImg);
		bSavingImg = true;
		if(pRgb){
			cvSaveImage( fileName, pRgb, quality);
			if (dayOrNight == "NIGHT"){
				system("scripts/saveImageNight.sh &");
			} else {
				system("scripts/saveImageDay.sh &");
			}
		}
		bSavingImg = false;
		pthread_mutex_unlock(&mtx_SaveImg);

	}

	printf("save thread over\n");
	return (void*)0;
}

void IntHandle(int i)
{
	bMain = false;
}

void calculateDayOrNight(const char* latitude, const char* longitude)
{
	std::string sunwaitCommand = "sunwait poll exit set civil ";
	sunwaitCommand.append(latitude);
	sunwaitCommand.append(" ");
	sunwaitCommand.append(longitude);
	dayOrNight = exec(sunwaitCommand.c_str());
	dayOrNight = exec(sunwaitCommand.c_str());
	dayOrNight.erase(std::remove(dayOrNight.begin(), dayOrNight.end(), '\n'), dayOrNight.end());
}

void writeToLog(int val) {
	std::ofstream outfile;
  	outfile.open("log.txt", std::ios_base::app);
  	outfile << val;
  	outfile << "\n";
}

int getRpiCameraProperties(RPI_CAMERA_INFO& info) {
	
	std::string WidthHeight="";
	std::string raspistillCommand="";
	
	info.Name = "Raspberry PI Camera ";
	raspistillCommand = "raspistill -n -ss 1 -v 2>&1 | awk -F'[^0-9]*' '/Width/ {printf (\"%s,%s\", $2,$3)}'";

	WidthHeight = exec(raspistillCommand.c_str());
	info.MaxWidth = stoi(WidthHeight.substr(0,WidthHeight.find(',')));
	info.MaxHeight = stoi(WidthHeight.substr(WidthHeight.find(',')+1, WidthHeight.length()));

    
    if (info.MaxWidth == 2592) {
		info.Name.append("v1");
	} else if (info.MaxWidth == 3280) {
		info.Name.append("v2");
	} else if (info.MaxWidth == 0) { //probably means that hardware wasn't found or software wasnot installed
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
	
}

int raspiCapture( int exposure, RPI_IMAGE_PARAMETERS p)
{
   std::string commandString = ""; //"raspistill -n -st -o image_tmp.jpg -awb off";
   std::string commandOutput = "";
   std::string tmpFileName = "";

   printf("Exposure: %d \n", exposure);
   
   tmpFileName.append("tmp_" + p.rpiFileName);
   
   commandString.append("raspistill -n -awb off -o " + tmpFileName); 
   commandString.append(" -w "+ std::to_string(p.width) + " -h " + std::to_string(p.height));
   commandString.append(" -awbg " + std::to_string((float)p.rpiWBR/50) + "," + std::to_string((float)(float)p.rpiWBB/50));
   commandString.append(" -q " + std::to_string(quality[1]));
   
   if ( !p.rpiAutoGain) {

   // Gain on ZWO camera is translated to ISO for raspistill command.  ASI Gain values to ISO is arbitrary
       if (p.rpiGain > 70) {
	       commandString.append(" -ISO 800");
       } else if (p.rpiGain > 0 && p.rpiGain <= 70) {
	   	   commandString.append(" -ISO 400");
       } else {
	   	   commandString.append(" -ISO 100");
       }
    }

    if ( !p.rpiAutoExposure && exposure > 0 ) {
		commandString.append(" -ss " + std::to_string(exposure) + " -ex off");
	}

// even if autoexposure at night need to pump up brightness and contrast (gamma)
    if (dayOrNight == "NIGHT"){
	   commandString.append(" -br " + std::to_string(p.rpiBrightness) + " -co " + std::to_string(p.rpiGamma) + " -sh 50");
    } 
   
// flips
   if (p.rpiFlip % 2 == 1) { 
	   commandString.append(" -hf");
   }
     if (p.rpiFlip > 1) { 
	   commandString.append(" -vf");
   } 
   
   commandString.append(" 2>&1");

   printf("%s\n", commandString.c_str());
   
   commandOutput = exec(commandString.c_str());
   
//   printf("Command Results:%s\n\n\n", commandOutput.c_str());

   if (commandOutput.find("ailed") != std::string::npos) {
	   return EXIT_FAILURE;
   }

   if (pRgb)  { cvReleaseImage(&pRgb);}

   pRgb = cvLoadImage(tmpFileName.c_str(), -1);
 
   return EXIT_SUCCESS;
}
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

int  main(int argc, char* argv[])
{

	signal(SIGINT, IntHandle);
	pthread_mutex_init(&mtx_SaveImg, 0);
	pthread_cond_init(&cond_SatrtSave, 0);

	int fontname[] = {CV_FONT_HERSHEY_SIMPLEX, CV_FONT_HERSHEY_PLAIN, CV_FONT_HERSHEY_DUPLEX, CV_FONT_HERSHEY_COMPLEX,
 		CV_FONT_HERSHEY_TRIPLEX, CV_FONT_HERSHEY_COMPLEX_SMALL, CV_FONT_HERSHEY_SCRIPT_SIMPLEX, CV_FONT_HERSHEY_SCRIPT_COMPLEX};
	int fontnumber = 0;
	int iStrLen, iTextX = 15, iTextY = 25;
	char const * ImgText="";
	double fontsize = 0.6;
	int linewidth =1;
	int fontcolor[3]={255,0,0};
	int linetype[3]={CV_AA, 8, 4};
	int linenumber = 0;

	char buf[1024]={0};
	char bufTime[128]={0};

	int width=0;
	int height=0;
	int bin=1;
	int Image_type=1;
	int asiBandwidth=40;
	int asiExposure=5000000;
	int asiMaxExposure=10000;
	int asiAutoExposure=0;
	int asiGain=150;
	int asiMaxGain=200;
	int asiAutoGain=0;
	int delay=10; // Delay in milliseconds. Default is 10ms
	int daytimeDelay=5000; // Delay in milliseconds. Default is 5000ms
	int asiWBR=65;
	int asiWBB=85;
	int asiGamma=50;
	int asiBrightness=50;
	int asiFlip=0;
  	char const * latitude="60.7N";	//GPS Coordinates of Whitehorse, Yukon where the code was created
	char const * longitude="135.05W";
  	int preview=0;
	int time=1;
	int darkframe=0;
	int showTemperature=0;
	int daytimeCapture=1;
	int help=0;

//	char const* bayer[] = {"RG","BG","GR","GB"};
//	int CamNum=0;
	int i;
	void* retval;
	bool endOfNight = false;
	pthread_t hthdSave = 0;
	
    RPI_CAMERA_INFO RpiCameraInfo;
    RPI_IMAGE_PARAMETERS RpiImageParameters;

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
  printf("\n");
  printf("%s ******************************************\n", KGRN);
  printf("%s *** Allsky Camera Software v0.5 | 2018 ***\n", KGRN);
  printf("%s ******************************************\n\n", KGRN);
  printf("\%sCapture images of the sky with a Raspberry Pi and an ASI Camera\n",KGRN);
  printf("\n");
  printf("%sAdd -h or -help for available options \n",KYEL);
  printf("\n");
  printf("\%sAuthor: ",KNRM);
  printf("Thomas Jacquin - <jacquin.thomas@gmail.com>\n\n");
  printf("\%sContributors:\n",KNRM);
  printf("-Knut Olav Klo\n");
  printf("-Daniel Johnsen\n");
  printf("-Yang and Sam from ZWO\n");
  printf("-Robert Wagner\n");
  printf("-Nathan Day\n\n");

  if(argc > 0)
    {
      for(i = 0; i < argc-1; i++)
	{
	 if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0){
		help = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-width") == 0){
		width = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-height") == 0){
		height = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-type") == 0){
		Image_type = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-quality") == 0){
        	quality[1] = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-exposure") == 0){
        	asiExposure = atoi(argv[i+1])*1000; i++;}
		else if(strcmp(argv[i], "-maxexposure") == 0){
	       	asiMaxExposure = atoi(argv[i+1]); i++;}
		else if(strcmp(argv[i], "-autoexposure") == 0){
         	asiAutoExposure = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-gain") == 0){
        	asiGain = atoi(argv[i+1]); i++;}
		else if(strcmp(argv[i], "-maxgain") == 0){
					asiMaxGain = atoi(argv[i+1]); i++;}
		else if(strcmp(argv[i], "-autogain") == 0){
         	asiAutoGain = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-gamma") == 0){
        	asiGamma = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-brightness") == 0){
        	asiBrightness = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-bin") == 0){
        	bin = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-delay") == 0){
        	delay = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-daytimeDelay") == 0){
        	daytimeDelay = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-wbr") == 0){
        	asiWBR = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-wbb") == 0){
        	asiWBB = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-text") == 0){
        	ImgText = (argv[i+1]); i++;}
         else if(strcmp(argv[i], "-textx") == 0){
        	iTextX = atoi(argv[i+1]); i++;}
         else if(strcmp(argv[i], "-texty") == 0){
        	iTextY = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-fontname") == 0){
        	fontnumber = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-fontcolor") == 0){
        	fontcolor[0] = atoi(argv[i+1]); i++;
		fontcolor[1] = atoi(argv[i+1]); i++;
		fontcolor[2] = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-fonttype") == 0){
        	linenumber = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-fontsize") == 0){
	  	fontsize = atof(argv[i+1]); i++;}
         else if(strcmp(argv[i], "-fontline") == 0){
        	linewidth = atoi(argv[i+1]); i++;}
         else if(strcmp(argv[i], "-flip") == 0){
        	asiFlip = atoi(argv[i+1]); i++;}
         else if(strcmp(argv[i], "-usb") == 0){
        	asiBandwidth = atoi(argv[i+1]); i++;}
         else if(strcmp(argv[i], "-filename") == 0){
        	fileName = (argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-latitude") == 0){
        	latitude = argv[i+1]; i++;}
         else if(strcmp(argv[i], "-longitude") == 0){
        	longitude = argv[i+1]; i++;}
 	 else if(strcmp(argv[i], "-preview") == 0){
        	preview = atoi(argv[i+1]); i++;}
 	 else if(strcmp(argv[i], "-time") == 0){
        	time = atoi(argv[i+1]); i++;}
	 else if(strcmp(argv[i], "-darkframe") == 0){
                darkframe = atoi(argv[i+1]); i++;}
	else if(strcmp(argv[i], "-showTemperature") == 0){
               showTemperature = atoi(argv[i+1]); i++;}
		else if(strcmp(argv[i], "-daytime") == 0){
	       	daytimeCapture = 1; i++;} //atoi(argv[i+1]); i++;}
				}
  }

  if (help == 1) {
	  printf("%sAvailable Arguments: \n",KYEL);
	  printf(" -width  		  - Default = Camera Max Width \n");
	  printf(" -height     		  - Default = Camera Max Height \n");
	  printf(" -exposure		  - Default = 5000000 - Time in µs (equals to 5 sec) \n");
	  printf(" -maxexposure		  - Default = 10000 - Time in ms (equals to 10 sec) \n");
	  printf(" -autoexposure	  - Default = 0 - Set to 1 to enable auto Exposure \n");
	  printf(" -gain			  - Default = 50 \n");
	  printf(" -maxgain			  - Default = 200 \n");
	  printf(" -autogain		  - Default = 0 - Set to 1 to enable auto Gain \n");
	  printf(" -gamma			  - Default = 50 \n");
	  printf(" -brightness		  - Default = 50 \n");
	  printf(" -wbr			  	  - Default = 50   - White Balance Red \n");
	  printf(" -wbb			  	  - Default = 50   - White Balance Blue \n");
	  printf(" -bin        		  - Default = 1    - 1 = binning OFF (1x1), 2 = 2x2 binning, 4 = 4x4 binning\n");
	  printf(" -delay      		  - Default = 10   - Delay between images in milliseconds - 1000 = 1 sec.\n");
	  printf(" -daytimeDelay      - Default = 5000   - Delay between images in milliseconds - 5000 = 5 sec.\n");
	  printf(" -type = Image Type - Default = 0    - 0 = RAW8,  1 = RGB24,  2 = RAW16 \n");
	  printf(" -quality		  	  - Default PNG=3, JPG=95, Values: PNG=0-9, JPG=0-100\n");
	  printf(" -usb = USB Speed	  - Default = 40   - Values between 40-100, This is BandwidthOverload \n");
	  printf(" -filename		  - Default = IMAGE.PNG \n");
	  printf(" -flip        	  - Default = 0    - 0 = Orig, 1 = Horiz, 2 = Verti, 3 = Both\n");
	  printf("\n");
	  printf(" -text        		  - Default =      - Character/Text Overlay. Use Quotes.  Ex. -c \"Text Overlay\"\n");
	  printf(" -textx        		  - Default = 15   - Text Placement Horizontal from LEFT in Pixels\n");
	  printf(" -texty = Text Y     	  - Default = 25   - Text Placement Vertical from TOP in Pixels\n");
	  printf(" -fontname = Font Name	- Default = 0    - Font Types (0-7), Ex. 0 = simplex, 4 = triplex, 7 = script\n");
	  printf(" -fontcolor = Font Color	- Default = 255 0 0  - Text blue (BRG)\n");
	  printf(" -fonttype = Font Type	- Default = 0    - Font Line Type,(0-2), 0 = AA, 1 = 8, 2 = 4\n");
	  printf(" -fontsize 		  - Default = 0.5  - Text Font Size\n");
	  printf(" -fontline		  - Default = 1    - Text Font Line Thickness\n");
	//printf(" -bgc = BG Color   	  - Default =      - Text Background Color in Hex. 00ff00 = Green\n");
	//printf(" -bga = BG Alpha   	  - Default =      - Text Background Color Alpha/Transparency 0-100\n");
	  printf("\n");
	  printf("\n");
	  printf(" -lat = Latitude   	  - Default = 60.7N (Whitehorse)   - Latitude of the camera.\n");
	  printf(" -lon = Longitude  	  - Default = 135.05W (Whitehorse) - Longitude of the camera\n");
	  printf("\n");
	  printf(" -preview        	  - set to 1 to preview the captured images. Only works with a Desktop Environment \n");
	  printf(" -time		  	  - Adds the time to the image. Combine with Text X and Text Y for placement \n");
	  printf(" -darkframe         - Set to 1 to disable time and text overlay \n");
		printf(" -showTemperature         - Set to 1 to display the sensor temperature on the image \n");

	  printf("%sUsage:\n", KRED);
	  printf(" ./capture -width 640 -height 480 -exposure 5000000 -gamma 50 -type 1 -bin 1 -filename Lake-Laberge.PNG\n\n");
  }
	printf("%s", KNRM);

	const char * ext = strrchr(fileName, '.');
	if (strcmp(ext+1, "jpg") == 0 || strcmp(ext+1, "JPG") == 0 || strcmp(ext+1, "jpeg") == 0 || strcmp(ext+1, "JPEG") == 0) {
	    quality[0] = CV_IMWRITE_JPEG_QUALITY;
		if(quality[1] == 200){
			quality[1] = 95;
		}
	}
	else{
		quality[1] = 3;
	}


	int numDevices = 1; // There can only be one Raspberry Pi Camera
	if(numDevices <= 0){
	   printf("\nNo Connected Camera...\n");
	   width=1;	//Set to 1 when NO Cameras are connected to avoid error: OpenCV Error: Insufficient memory
	   height=1;	//Set to 1 when NO Cameras are connected to avoid error: OpenCV Error: Insufficient memory
	} else
	   printf("\nListing Attached Cameras:\n");




	if (getRpiCameraProperties(RpiCameraInfo) != EXIT_SUCCESS)
	{
		printf("Open Camera ERROR, Check that you have hardware and software installed\n");
	}
	printf("- %s\n", RpiCameraInfo.Name.c_str());

   
    
	printf("\n%s Information:\n",RpiCameraInfo.Name.c_str());

	printf("- Resolution:%dx%d\n", RpiCameraInfo.MaxWidth, RpiCameraInfo.MaxHeight);
    printf("- Color Camera\n");


	if(width == 0 || height == 0)
	{
		width = RpiCameraInfo.MaxWidth;
		height = RpiCameraInfo.MaxHeight;
	}

//	long ltemp = 0;  No Sensor temp on Raspberry Pi Camera


// No binning on RPI camera

	bin = 1;
	
	// Adjusting variables for chosen binning
	height = height/bin;
	width = width/bin;
	iTextX = iTextX/bin;
	iTextY = iTextY/bin;
	fontsize = fontsize/bin;
	linewidth = linewidth/bin;

 	const char * sType = "ASI_IMG_RGB24";
    
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

printf("%s",KGRN);
	printf("\nCapture Settings: \n");
	printf(" Image Type: %s\n",sType);
	printf(" Resolution: %dx%d \n",width,height);
	printf(" Quality: %d \n",quality[1]);
	printf(" Exposure: %1.0fms\n",round(asiExposure/1000));
	printf(" Max Exposure: %dms\n",asiMaxExposure);
	printf(" Auto Exposure: %d\n",asiAutoExposure);
	printf(" Gain: %d\n",asiGain);
	printf(" Max Gain: %d\n",asiMaxGain);
	printf(" Auto Gain: %d\n",asiAutoGain);
	printf(" Brightness: %d\n",asiBrightness);
	printf(" Gamma: %d\n",asiGamma);
	printf(" WB Red: %d\n",asiWBR);
	printf(" WB Blue: %d\n",asiWBB);
	printf(" Binning: %d\n",bin);
	printf(" Delay: %dms\n",delay);
	printf(" Daytime Delay: %dms\n",daytimeDelay);
	printf(" USB Speed: %d\n",asiBandwidth);
	printf(" Text Overlay: %s\n",ImgText);
	printf(" Text Position: %dpx left, %dpx top\n",iTextX,iTextY);
	printf(" Font Name:  %d\n",fontname[fontnumber]);
	printf(" Font Color: %d , %d, %d\n",fontcolor[0], fontcolor[1], fontcolor[2]);
	printf(" Font Line Type: %d\n",linetype[linenumber]);
	printf(" Font Size: %1.1f\n",fontsize);
	printf(" Font Line: %d\n",linewidth);
	printf(" Flip Image: %d\n",asiFlip);
	printf(" Filename: %s\n",fileName);
	printf(" Latitude: %s\n",latitude);
	printf(" Longitude: %s\n",longitude);
    printf(" Preview: %d\n",preview);
	printf(" Time: %d\n",time);
	printf(" Darkframe: %d\n",darkframe);
	printf(" Show Temperature: %d\n",showTemperature);
printf("%s",KNRM);

//	ASISetROIFormat(CamNum, width, height, bin, (ASI_IMG_TYPE)Image_type);

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------



    RpiImageParameters.width = width;
    RpiImageParameters.height = height;
    RpiImageParameters.rpiExposure = asiExposure;
    RpiImageParameters.rpiGain = asiGain;
    RpiImageParameters.rpiAutoExposure = asiAutoExposure;
    RpiImageParameters.rpiAutoGain = asiAutoGain;
    RpiImageParameters.rpiWBR = asiWBR;
    RpiImageParameters.rpiWBB = asiWBB;
    RpiImageParameters.rpiGamma = asiGamma;
    RpiImageParameters.rpiBrightness = asiBrightness;
    RpiImageParameters.rpiFlip = asiFlip;
    RpiImageParameters.rpiFileName = fileName;

	pthread_t thread_display=0;
	if (preview == 1) {
		bDisplay = 1;
		pthread_create(&thread_display, NULL, Display, (void*)pRgb);
	}

	if(!bSaveRun)
	{
		bSaveRun = true;
		if(pthread_create(&hthdSave, 0, SaveImgThd, 0)!=0)
			bSaveRun = false;
	}

	while(bMain)
	{
		// Find out if it is currently DAY or NIGHT
		calculateDayOrNight(latitude, longitude);
		int expTime = round(asiExposure/1000000);
		int currentExposure = asiExposure;

	    iStrLen = strlen(buf);

		if (dayOrNight == "NIGHT"){
			printf("\n");
			if (asiAutoExposure == 1)
				printf("Saving auto exposed images every %d ms\n\n", delay);
			else {
				printf("Saving %d", expTime);
				printf("s exposure images every %d ms\n\n", delay);
			}
			printf("Press Ctrl+C to stop\n\n");


			printf("Starting nighttime capture");
			printf("\n");
			
			// for Raspberry Pi Camera, the command "raspistill" is used to take the picture it and will be read  into OpenCV to add text, etc.  The code could be rewritten to use the camera libraries

			while(bMain && dayOrNight == "NIGHT"){
// take a picture
				if(raspiCapture(round(currentExposure), RpiImageParameters) == EXIT_SUCCESS){
					
			        CvRect rect = cvRect(iTextX, iTextY - 15, iStrLen* 11, 20);
			        cvSetImageROI(pRgb , rect);
			        cvSet(pRgb, CV_RGB(180, 180, 180));
			        cvResetImageROI(pRgb);

					sprintf(bufTime, "%s", getTime());
					if (time == 1 ){
						ImgText = bufTime;
					}
					if (darkframe != 1 ){
						cvText(pRgb, ImgText, iTextX, iTextY, fontsize, linewidth, linetype[linenumber], fontname[fontnumber], fontcolor, Image_type);

					}
					if(pRgb){
						printf("Saving...");
						printf(bufTime);
						printf("\n");
						if(!bSavingImg)
						{
							pthread_mutex_lock(& mtx_SaveImg);
							pthread_cond_signal(&cond_SatrtSave);
							pthread_mutex_unlock(& mtx_SaveImg);
						}
					}
// raspistill doesn't return autogain and autoexposure values					
					if (asiAutoGain == 1){
//						long autoGain = 0;
//						ASIGetControlValue(CamNum, ASI_GAIN, &autoGain, &bAuto);
//						printf("Auto Gain value: %.0f\n", (float)autoGain);
//						writeToLog(autoGain);
					}
					if (asiAutoExposure == 1){
						long autoExp = 0;
//						ASIGetControlValue(CamNum, ASI_EXPOSURE, &autoExp, &bAuto);
//						printf("Auto Exposure value: %.0f ms\n", (float)autoExp/1000);
//						writeToLog(autoExp);

						// Delay applied before next exposure
						if (autoExp < asiMaxExposure*1000) {
							printf("Sleeping: %d ms\n", asiMaxExposure - (int)(autoExp/1000) + delay);
							usleep(asiMaxExposure*1000-autoExp + delay*1000);
						}
						else {
							usleep(delay*1000);
						}
					}
					else {
						usleep(delay*1000);
					}
					calculateDayOrNight(latitude, longitude);
				} else {
					printf("Camera Capture Failed - Check for proper installation of hardware and software");
				}
				
			}
			endOfNight = true;

			// Sleep 2 seconds (for SEG fault debugging)
			usleep(2000000);

		} else if (dayOrNight == "DAY") {
			printf("\n");
			printf("Saving auto exposed images every %d ms\n\n", daytimeDelay);
			printf("Press Ctrl+C to stop\n\n");
			if (endOfNight == true){
				system("scripts/endOfNight.sh &");
				endOfNight = false;
			}
			if (daytimeCapture != 1){
				printf(" It's daytime... we're not saving images");
				printf("\n");
				usleep(daytimeDelay*1000);
			} else {
				printf("Starting daytime capture");
				printf("\n");

				while(bMain && dayOrNight == "DAY"){
					if(raspiCapture(0, RpiImageParameters) == EXIT_SUCCESS){
// Not able to get last autoexp stats for smooth transition to night

						sprintf(bufTime, "%s", getTime());
						if (time == 1 ){
							ImgText = bufTime;
						}

						if (darkframe != 1 ){
							cvText(pRgb, ImgText, iTextX, iTextY, fontsize, linewidth, linetype[linenumber], fontname[fontnumber], fontcolor, Image_type);
						}

						if(pRgb){
							printf("Capturing daytime image...");
							printf(bufTime);
							printf("\n");
							if(!bSavingImg)
							{
								pthread_mutex_lock(& mtx_SaveImg);
								pthread_cond_signal(&cond_SatrtSave);
								pthread_mutex_unlock(& mtx_SaveImg);
							}
						}
						usleep(daytimeDelay*1000);
					} else {
					    printf("Camera Capture Failed - Check for proper installation of hardware and software");
    				}
					calculateDayOrNight(latitude, longitude);
				}

				// Sleep 2 seconds before starting night time capture
				usleep(2000000);
			}
		}
	}


	if(bDisplay)
	{
		bDisplay = 0;
   		pthread_join(thread_display, &retval);
	}

	if(bSaveRun)
	{
		bSaveRun = false;
		pthread_mutex_lock(&mtx_SaveImg);
		pthread_cond_signal(&cond_SatrtSave);
		pthread_mutex_unlock(& mtx_SaveImg);
		pthread_join(hthdSave, 0);

	}
	cvReleaseImage(&pRgb);
	printf("main function over\n");
	return 1;

}
