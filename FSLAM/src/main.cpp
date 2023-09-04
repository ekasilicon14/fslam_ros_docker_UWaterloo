#include <thread>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "IOWrapper/Output3DWrapper.h"

#include <boost/thread.hpp>
#include "util/settings.h"
#include "util/globalFuncs.h"
#include "util/DatasetReader.h"
#include "util/globalCalib.h"

#include "util/NumType.h"
#include "FullSystem/FullSystem.h"


#include "IOWrapper/Pangolin/PangolinDSOViewer.h"
#include "IOWrapper/OutputWrapper/SampleOutputWrapper.h"


#include <cxxopts.hpp>


using namespace HSLAM;

void my_exit_handler(int s)
{
	printf("Caught signal %d\n",s);
	exit(1);
}

void exitThread()
{
	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = my_exit_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);

	while(true) pause();
}


int main(int argc, char **argv)
{
	boost::thread exThread = boost::thread(exitThread); // hook crtl+C.

	cxxopts::Options options("HSLAM", "Direct Indirect Feature Fusion SLAM");

	std::string source0 = "";
	std::string calib0 = "";
	std::string imu = "";
	std::string imu_calib = "";
	std::string vocabPath = "";
	std::string vignette = "";
	std::string gammaCalib = "";

	options.add_options()
        ("f,files", "Input images path - mandatory input", cxxopts::value(source0))
		("C,calib", "Camera intrinsic callibration - mandatory input", cxxopts::value(calib0))
		("i,imu", "IMU data path", cxxopts::value(imu))
		("I,imu_calib", "IMU parameter file", cxxopts::value(imu_calib))
		("v,vocab", "Path to Vocabulary file - required for loop closure", cxxopts::value(vocabPath))
		("n,vignette", "Path to photmetric calibration Vignette model", cxxopts::value(vignette))
		("g,gamma", "Path to photmetric calibration gamma response Model", cxxopts::value(gammaCalib))
		("l,loopclosure", "Enable-Disable loop closure", cxxopts::value<bool>()->default_value("false"))
		("r,reverse", "Play a sequence in reverse", cxxopts::value<bool>()->default_value("false"))
		("imu_weight", "Weight of IMU", cxxopts::value<float>()->default_value("5"))
		("imu_weight_tracker", "Weight of IMU tracking relative to visual tracking", cxxopts::value<float>()->default_value("0.5"))
		("preload", "Preload all images into memory", cxxopts::value<bool>()->default_value("false"))
		("usesampleoutput", "Replace pangolinViewer with another output wrapper", cxxopts::value<bool>()->default_value("false"))
		("nolog", "Disable logging optimization data", cxxopts::value<bool>()->default_value("false"))
		("nogui", "Disable GUI", cxxopts::value<bool>()->default_value("false"))
		("save", "Save debug images", cxxopts::value<bool>()->default_value("false"))
		("quiet", "Disable message printing", cxxopts::value<bool>()->default_value("true"))
		("nomt", "Turns off multiThreading", cxxopts::value<bool>()->default_value("false"))
		("s,startindex", "Image to start from", cxxopts::value<int>()->default_value("0"))
        ("e,endindex", "Last image to be processed", cxxopts::value<int>()->default_value("100000"))
		("m,mode", "System mode: 0: use precalibrated gamma and vignette -1: photometric mode without calibration - 2: photometric mode with perfect images", cxxopts::value<int>()->default_value("1"))
        ("preset", "Preset configuration}", cxxopts::value<int>()->default_value("0"))
		("speed", "Enforce playback Speed to real-time", cxxopts::value<float>()->default_value("0.0"))
        ("h,help", "Print usage")
    ;

	auto result = options.parse(argc, argv);

	if (result.count("help"))
    {
		std::cout << options.help() << std::endl;
		exit(0);
    }

	LoopClosure = result["loopclosure"].as<bool>();
	bool reverse = result["reverse"].as<bool>();
	bool preload = result["preload"].as<bool>();
	bool useSampleOutput = result["usesampleoutput"].as<bool>();
	setting_logStuff = !result["nolog"].as<bool>();
	disableAllDisplay = result["nogui"].as<bool>();
	debugSaveImages = result["save"].as<bool>();
	setting_debugout_runquiet = result["quiet"].as<bool>();
	multiThreading = !result["nomt"].as<bool>();

	int startIndex = result["startindex"].as<int>();
	int endIndex = result["endindex"].as<int>();
	int preset = result["preset"].as<int>();
	int mode = result["mode"].as<int>();
	float playbackSpeed = result["speed"].as<float>();
	float imu_weight_ = result["imu_weight"].as<float>();
	float imu_weight_tracker_ = result["imu_weight_tracker"].as<float>();

	if(source0.empty() || calib0.empty()) { std::cout<< "Path to images or calibration not provided! cannot function without them. exit." << std::endl; return(0);}

	if (debugSaveImages)
	{
		if(42==system("rm -rf images_out")) std::cout<<"system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n"<<std::endl;
		if(42==system("mkdir images_out")) std::cout<<"system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n"<<std::endl;
		if(42==system("rm -rf images_out")) std::cout<<"system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n"<<std::endl;
		if(42==system("mkdir images_out")) std::cout<<"system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n"<<std::endl;
	}

	switch (mode)
	{
	case 1:
		setting_photometricCalibration = 0;
		setting_affineOptModeA = 0; //-1: fix. >=0: optimize (with prior, if > 0).
		setting_affineOptModeB = 0; //-1: fix. >=0: optimize (with prior, if > 0).
		break;
	case 2:
		setting_photometricCalibration = 0;
		setting_affineOptModeA = -1; //-1: fix. >=0: optimize (with prior, if > 0).
		setting_affineOptModeB = -1; //-1: fix. >=0: optimize (with prior, if > 0).
		setting_minGradHistAdd = 3;
		break;
	default:
		break;
	}

	if(LoopClosure && !vocabPath.empty())
	{
		Vocab.load(vocabPath.c_str());

		printf("Loading Vocabulary from %s!\n", vocabPath.c_str());
		if (Vocab.empty())
		{
			printf("Failed to load vocabulary! Exit\n");
			exit(1);
		}
	}
	else
	{
		std::cout << "no vocabulary path provided! disabling loop closure." << std::endl;
		LoopClosure = false; 
	}

	if(preset == 0 || preset == 1)
	{
		printf("DEFAULT settings:\n"
				"- %s real-time enforcing\n"
				"- 2000 active points\n"
				"- 5-7 active frames\n"
				"- 1-6 LM iteration each KF\n"
				"- original image resolution\n", preset==0 ? "no " : "1x");

		playbackSpeed = (preset==0 ? 0 : 1);
	}
	else if(preset == 2 || preset == 3)
	{
		printf("FAST settings:\n"
				"- %s real-time enforcing\n"
				"- 800 active points\n"
				"- 4-6 active frames\n"
				"- 1-4 LM iteration each KF\n"
				"- 424 x 320 image resolution\n", preset==0 ? "no " : "5x");

		playbackSpeed = (preset==2 ? 0 : 5);
		preload = preset==3;
		setting_desiredImmatureDensity = 600;
		setting_desiredPointDensity = 800;
		setting_minFrames = 4;
		setting_maxFrames = 6;
		setting_maxOptIterations=4;
		setting_minOptIterations=1;

		benchmarkSetting_width = 424;
		benchmarkSetting_height = 320;

		setting_logStuff = false;
	}

	ImageFolderReader* reader = new ImageFolderReader(source0, calib0, gammaCalib, vignette);
	reader->setGlobalCalibration();
	set_frame_sz(reader->get_undist_width(), reader->get_undist_height());

	if(setting_photometricCalibration > 0 && reader->getPhotometricGamma() == 0)
	{
		printf("ERROR: dont't have photometric calibation. Need to use commandline options mode=1 or mode=2 ");
		exit(1);
	}

	if(!imu.empty() && !imu_calib.empty()){
		IMUFolderReader* imu_reader = new IMUFolderReader(imu, imu_calib);
		imu_reader->getIMUfiles_euroc();
		imu_reader->getIMUinfo_euroc();

		imu_weight = imu_weight_;
		imu_weight_tracker = imu_weight_tracker_;
		imu_use_flag = true;
		imu_track_flag = true;
	} else {
		imu_use_flag = false;
		imu_track_flag = false;
	}


	int lstart=startIndex;
	int lend = endIndex;
	int linc = 1;
	if(reverse)
	{
		printf("REVERSE!!!!");
		lstart=endIndex-1;
		if(lstart >= reader->getNumImages())
			lstart = reader->getNumImages()-1;
			
		lend = startIndex;
		linc = -1;
	}



	FullSystem* fullSystem = new FullSystem();
	fullSystem->setGammaFunction(reader->getPhotometricGamma());
	fullSystem->linearizeOperation = (playbackSpeed == 0);

	IOWrap::PangolinDSOViewer* viewer = 0;
	if(!disableAllDisplay)
    {
        viewer = new IOWrap::PangolinDSOViewer(wG[0],hG[0], false);
        fullSystem->outputWrapper.push_back(viewer);
    }



    if(useSampleOutput)
        fullSystem->outputWrapper.push_back(new IOWrap::SampleOutputWrapper());


	// to make MacOS happy: run this in dedicated thread -- and use this one to run the GUI.
    std::thread runthread([&]() {
        std::vector<int> idsToPlay;
        std::vector<double> timesToPlayAt;
        for(int i=lstart;i>= 0 && i< reader->getNumImages() && linc*i < linc*lend;i+=linc)
        {
            idsToPlay.push_back(i);
            if(timesToPlayAt.size() == 0)
            {
                timesToPlayAt.push_back((double)0);
            }
            else
            {
                double tsThis = reader->getTimestamp(idsToPlay[idsToPlay.size()-1]);
                double tsPrev = reader->getTimestamp(idsToPlay[idsToPlay.size()-2]);
                timesToPlayAt.push_back(timesToPlayAt.back() +  fabs(tsThis-tsPrev)/playbackSpeed);
            }
        }


        std::vector<ImageAndExposure*> preloadedImages;
        if(preload)
        {
            printf("LOADING ALL IMAGES!\n");
            for(int ii=0;ii<(int)idsToPlay.size(); ii++)
            {
                int i = idsToPlay[ii];
                preloadedImages.push_back(reader->getImage(i));
            }
        }

        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        clock_t started = clock();
        double sInitializerOffset=0;


        for(int ii=0;ii<(int)idsToPlay.size(); ii++)
        {
			while (Pause)
			{
				usleep(5000);
			}
				
				
            if(!fullSystem->initialized)	// if not initialized: reset start time.
            {
                gettimeofday(&tv_start, NULL);
                started = clock();
                sInitializerOffset = timesToPlayAt[ii];
            }

            int i = idsToPlay[ii];


            ImageAndExposure* img;
            if(preload)
                img = preloadedImages[ii];
            else
                img = reader->getImage(i);



            bool skipFrame=false;
            if(playbackSpeed!=0)
            {
                struct timeval tv_now; gettimeofday(&tv_now, NULL);
                double sSinceStart = sInitializerOffset + ((tv_now.tv_sec-tv_start.tv_sec) + (tv_now.tv_usec-tv_start.tv_usec)/(1000.0f*1000.0f));

                if(sSinceStart < timesToPlayAt[ii])
                    usleep((int)((timesToPlayAt[ii]-sSinceStart)*1000*1000));
                else if(sSinceStart > timesToPlayAt[ii]+0.5+0.1*(ii%2))
                {
                    printf("SKIPFRAME %d (play at %f, now it is %f)!\n", ii, timesToPlayAt[ii], sSinceStart);
                    skipFrame=true;
                }
            }

			
			if (!skipFrame) fullSystem->addActiveFrame(img, i);
			

			delete img;
	
			if(viewer!=0)
				if(viewer->isDead)
					break;

	        if(fullSystem->initFailed || setting_fullResetRequested)
            {
                if(ii < 250 || setting_fullResetRequested)
                {
                    printf("RESETTING!\n");
                    std::vector<IOWrap::Output3DWrapper*> wraps = fullSystem->outputWrapper;
                    for(IOWrap::Output3DWrapper* ow : wraps) ow->reset();
					usleep(20000); //hack - wait for display wrapper to clean up.
					if(fullSystem)
					{
						delete fullSystem;
						fullSystem = nullptr;
					}
						
					fullSystem = new FullSystem();
					fullSystem->setGammaFunction(reader->getPhotometricGamma());
					fullSystem->linearizeOperation = (playbackSpeed == 0);

					fullSystem->outputWrapper = wraps;

                    setting_fullResetRequested=false;
                }
            }

            if(fullSystem->isLost)
            {
                printf("LOST!!\n");
                break;
            }

        }
		// fullSystem->BAatExit();
		fullSystem->blockUntilMappingIsFinished();
		clock_t ended = clock();
        struct timeval tv_end;
        gettimeofday(&tv_end, NULL);


        fullSystem->printResult("result.txt");


        int numFramesProcessed = abs(idsToPlay[0]-idsToPlay.back());
        double numSecondsProcessed = fabs(reader->getTimestamp(idsToPlay[0])-reader->getTimestamp(idsToPlay.back()));
        double MilliSecondsTakenSingle = 1000.0f*(ended-started)/(float)(CLOCKS_PER_SEC);
        double MilliSecondsTakenMT = sInitializerOffset + ((tv_end.tv_sec-tv_start.tv_sec)*1000.0f + (tv_end.tv_usec-tv_start.tv_usec)/1000.0f);
        printf("\n======================"
                "\n%d Frames (%.1f fps)"
                "\n%.2fms per frame (single core); "
                "\n%.2fms per frame (multi core); "
                "\n%.3fx (single core); "
                "\n%.3fx (multi core); "
                "\n======================\n\n",
                numFramesProcessed, numFramesProcessed/numSecondsProcessed,
                MilliSecondsTakenSingle/numFramesProcessed,
                MilliSecondsTakenMT / (float)numFramesProcessed,
                1000 / (MilliSecondsTakenSingle/numSecondsProcessed),
                1000 / (MilliSecondsTakenMT / numSecondsProcessed));
        //fullSystem->printFrameLifetimes();
        if(setting_logStuff)
        {
            std::ofstream tmlog;
            tmlog.open("logs/time.txt", std::ios::trunc | std::ios::out);
            tmlog << 1000.0f*(ended-started)/(float)(CLOCKS_PER_SEC*reader->getNumImages()) << " "
                  << ((tv_end.tv_sec-tv_start.tv_sec)*1000.0f + (tv_end.tv_usec-tv_start.tv_usec)/1000.0f) / (float)reader->getNumImages() << "\n";
            tmlog.flush();
            tmlog.close();
        }

    });


	if(viewer != 0)
	    viewer->run();

	runthread.join();

	for(IOWrap::Output3DWrapper* ow : fullSystem->outputWrapper)
	{
		ow->join();
		delete ow;
	}



	printf("DELETE FULLSYSTEM!\n");
	if (fullSystem)
	{
		delete fullSystem;
		fullSystem = nullptr;
	}

	printf("DELETE READER!\n");
	delete reader;

	printf("EXIT NOW!\n");
	return 0;
}
